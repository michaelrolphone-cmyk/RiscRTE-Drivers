"""Synthetic compiler contract fixtures; not a target build or Runtime rerun."""
import hashlib,importlib.util,io,json,runpy,shlex,sys,tempfile,unittest
from contextlib import ExitStack,redirect_stdout
from pathlib import Path
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
import build_usb_cdc_native as b
import native_driver_toolchain as toolchain
import normalize_xtensa_relocations as normalizer
class Receipt(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory(prefix='cdc receipt ');self.addCleanup(self.tmp.cleanup);self.root=Path(self.tmp.name)
  self.src=self.root/'Drivers/usb_cdc_native';self.src.mkdir(parents=True);self.out=self.root/'dist/usb-cdc-acm-native-stream'
  self.sdk=self.root/'sdk/driver';self.sdk.mkdir(parents=True);(self.sdk/'used.h').write_text('used');(self.sdk/'unused.h').write_text('unused')
  (self.src/'driver.c').write_text('source');(self.src/'Descriptors.inc').write_text('parser')
  self.manifest={'type':'driver','id':'usb-cdc-acm','version':'0.1.9','driver_abi':2,'architecture':'xtensa-esp32s3','file_name':'driver.elf','requires':[{'capability':'usb.host','api':1},{'capability':'platform.clock','api':1}],'provides':[{'capability':'serial.port','api':1}],'status':'experimental-local-native-stream'}
  (self.src/'manifest.json').write_text(json.dumps(self.manifest))
  scripts=self.root/'scripts';scripts.mkdir()
  for name in ('build_usb_cdc_native.py','build_platform_clock_v1.py','native_driver_toolchain.py','normalize_xtensa_relocations.py'):(scripts/name).write_text('fixture helper')
  self.cc=self.root/'xtensa-gcc';self.cc.write_text('compiler');self.readelf=self.root/'xtensa-readelf';self.readelf.write_text('reader');self.lib=self.root/'libgcc.a';self.lib.write_text('libgcc')
  self.elf=bytearray(52);self.elf[:7]=b'\x7fELF\x01\x01\x01';self.elf[16:18]=(3).to_bytes(2,'little');self.elf[18:20]=(94).to_bytes(2,'little')
  self.mode='success';self.commands=[]
 def output(self,args,**kwargs):
  if '--version' in args:return 'synthetic compiler v1'
  if '-print-libgcc-file-name' in args:return str(self.lib)
  if '-M' in args:
   if self.mode=='scan':return 'wrong target:'
   paths=[self.src/'driver.c',self.src/'Descriptors.inc',self.sdk/'used.h']
   return 'native_cdc_dependencies: '+' \\\n '.join(str(p).replace(' ','\\ ') for p in paths)+'\n'
  if '--dyn-syms' in args:
   if self.mode=='exports':return '1: 0 0 FUNC GLOBAL DEFAULT 1 wrong_export'
   if self.mode=='imports':return '1: 0 0 FUNC GLOBAL DEFAULT 1 t5_driver_get\n2: 0 0 FUNC GLOBAL DEFAULT UND forbidden'
   return '1: 0 0 FUNC GLOBAL DEFAULT 1 t5_driver_get'
  raise AssertionError(args)
 def compile_mock(self,args,**kwargs):
  self.commands.append(args)
  if self.mode=='compiler':raise b.subprocess.CalledProcessError(1,args)
  Path(args[args.index('-o')+1]).write_bytes(b'bad' if self.mode=='elf' else self.elf)
  if self.mode=='mutation':(self.src/'Descriptors.inc').write_text('changed during compile')
 def invoke(self):
  with ExitStack() as stack:
   for name,value in [('ROOT',self.root),('SRC',self.src),('OUT',self.out)]:stack.enter_context(patch.object(b,name,value))
   stack.enter_context(patch.object(b,'compiler',return_value=str(self.cc)));stack.enter_context(patch.object(b,'normalize'))
   stack.enter_context(patch.object(b.subprocess,'check_output',side_effect=self.output));stack.enter_context(patch.object(b.subprocess,'run',side_effect=self.compile_mock));stack.enter_context(redirect_stdout(io.StringIO()))
   return b.main()
 def test_receipt_matches_artifact_and_separates_sdk_inventory_from_scan(self):
  receipt=self.invoke();files=list(self.out.glob('build-provenance-*.json'));self.assertEqual(len(files),1)
  self.assertEqual(json.loads(files[0].read_text()),receipt);self.assertIn(hashlib.sha256(files[0].read_bytes()).hexdigest(),files[0].name)
  self.assertEqual(receipt['candidate_identity']['version'],'0.1.9');self.assertEqual(receipt['output']['elf_sha256'],hashlib.sha256(self.elf).hexdigest())
  self.assertIn('sdk/driver/unused.h',receipt['inputs']);self.assertNotIn(str(self.sdk/'unused.h'),receipt['dependency_scan']['files'])
  self.assertIn(str(self.sdk/'used.h'),receipt['dependency_scan']['files']);self.assertIn('not trace',receipt['dependency_scan']['scope'])
  self.assertEqual(receipt['tools']['compiler']['sha256'],hashlib.sha256(b'compiler').hexdigest())
  expected=dict(self.manifest,size_bytes=len(self.elf),sha256=hashlib.sha256(self.elf).hexdigest(),canonical_size_bytes=None,canonical_sha256=None,candidate_only=True,byte_parity=False)
  self.assertEqual((self.out/'manifest.json').read_text(),json.dumps(expected,indent=2)+'\n');self.assertEqual((self.out/'driver.elf').read_bytes(),self.elf)
  manifest=json.loads((self.out/'manifest.json').read_text());self.assertTrue(manifest['candidate_only']);self.assertIsNone(manifest['canonical_sha256']);self.assertFalse(manifest['byte_parity'])
 def test_failures_never_emit_success_receipt(self):
  for mode in ('scan','compiler','elf','exports','imports','mutation'):
   with self.subTest(mode=mode):
    self.mode=mode
    with self.assertRaises((SystemExit,b.subprocess.CalledProcessError)):self.invoke()
    self.assertEqual(list(self.out.glob('build-provenance-*.json')),[])
    if mode=='scan':self.assertEqual(self.commands,[])
 def test_failure_does_not_relabel_prior_success(self):
  self.invoke();before={p.name:p.read_bytes() for p in self.out.glob('build-provenance-*.json')}
  self.mode='elf'
  with self.assertRaises(SystemExit):self.invoke()
  self.assertEqual({p.name:p.read_bytes() for p in self.out.glob('build-provenance-*.json')},before)
  old=json.loads(next(iter(before.values())))
  self.assertNotEqual(old['output']['elf_sha256'],hashlib.sha256((self.out/'driver.elf').read_bytes()).hexdigest())
 def test_real_dependency_scan_space_paths_and_unused_header(self):
  import subprocess
  (self.src/'driver.c').write_text('#include "used.h"\nint value;\n')
  raw=subprocess.check_output(['cc','-I'+str(self.sdk),'-M','-MT','native_cdc_dependencies',str(self.src/'driver.c')],text=True)
  paths=shlex.split(raw.split(':',1)[1].replace("\\\n"," "))
  self.assertIn(str(self.sdk/'used.h'),paths);self.assertNotIn(str(self.sdk/'unused.h'),paths)
 def test_manifest_rejection_still_precedes_compile(self):
  self.manifest['version']='0.1.8';(self.src/'manifest.json').write_text(json.dumps(self.manifest))
  with self.assertRaises(SystemExit):self.invoke()
  self.assertFalse(self.commands);self.assertFalse(self.out.exists())
 def test_compile_command_retains_original_flags(self):
  self.invoke();self.assertEqual(self.commands[0],[str(self.cc),'-std=c11','-Os','-fPIC','-mtext-section-literals','-mlongcalls','-fvisibility=hidden','-nostdlib','-nostartfiles','-shared','-I'+str(self.sdk),'-Wl,--hash-style=sysv','-Wl,--exclude-libs,ALL',str(self.src/'driver.c'),'-lgcc','-o',str(self.out/'driver.elf')])
if __name__=='__main__':unittest.main()

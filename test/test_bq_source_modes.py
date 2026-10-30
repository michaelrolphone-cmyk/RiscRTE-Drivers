"""Builder contract fixtures, not target compiler execution."""
import hashlib,io,json,sys,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
from contextlib import ExitStack,redirect_stderr
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
import build_board_power_t5s3_v2 as b

class Modes(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup);self.root=Path(self.tmp.name)
 def test_historical_source_exact_and_working_version_not_rewritten(self):
  src,record=b.select_source(ROOT,False,self.root)
  self.assertEqual(json.loads((src/'manifest.json').read_text())['version'],'0.1.6')
  self.assertEqual(record['source_tree'],b.HISTORICAL_TREE)
  self.assertEqual(json.loads((ROOT/'Drivers/bq25896/manifest.json').read_text())['version'],'0.1.9')
 def test_current_source_and_output_are_explicit(self):
  src,record=b.select_source(self.root,True,self.root)
  self.assertEqual(src,self.root/'Drivers/bq25896');self.assertEqual(record['expected_version'],'0.1.9')
  self.assertNotEqual(b.select_output(self.root,True,None),b.select_output(self.root,False,None))
 def test_current_alias_cannot_overwrite_historical(self):
  hist=self.root/'dist/board-power-t5s3-v2';hist.mkdir(parents=True)
  alias=self.root/'alias';alias.symlink_to(hist,target_is_directory=True)
  for output in (hist,hist/'child',alias):
   with self.assertRaises(ValueError):b.select_output(self.root,True,output)
 def test_retained_outputs_never_partially_replaced(self):
  out=self.root/'out';out.mkdir();stage=self.root/'stage';stage.mkdir()
  (out/'driver.elf').write_bytes(b'accepted');(stage/'a').write_bytes(b'new');(stage/'driver.elf').write_bytes(b'changed')
  with self.assertRaises(ValueError):b.publish_outputs(stage,out)
  self.assertEqual((out/'driver.elf').read_bytes(),b'accepted');self.assertFalse((out/'a').exists())
 def test_incompatible_mode_flags_rejected(self):
  with patch.object(sys,'argv',['builder','--current-source','--require-byte-parity']):
   with self.assertRaises(SystemExit) as result:b.main()
  self.assertEqual(result.exception.code,2)
 def test_wrong_historical_header_fails(self):
  stage=self.root/'stage';stage.mkdir()
  with patch.object(b.subprocess,'check_output',return_value=b'wrong header'):
   with self.assertRaisesRegex(ValueError,'historical header mismatch'):
    b.build(self.root,stage,{'expected_version':'0.1.6'},True,self.root)
 def test_failure_receipt_survives_cleanup_and_preserves_output(self):
  scripts=self.root/'scripts';scripts.mkdir()
  for name in ('build_board_power_t5s3_v2.py','build_platform_clock_v1.py','native_driver_toolchain.py','normalize_xtensa_relocations.py'):(scripts/name).write_bytes(b'helper')
  cc=self.root/'xtensa-gcc';cc.write_bytes(b'compiler');(self.root/'xtensa-readelf').write_bytes(b'readelf');lib=self.root/'libgcc.a';lib.write_bytes(b'archive')
  src=self.root/'src';src.mkdir();(src/'driver.c').write_text('source')
  manifest={'type':'driver','id':'board-power-t5s3-v2','version':'0.1.6','driver_abi':2,'architecture':'xtensa-esp32s3','file_name':'driver.elf','requires':[{'capability':'i2c.bus','api':1},{'capability':'platform.clock','api':1},{'capability':'board.power.bq25896.profile','api':1}],'provides':[{'capability':'board.power.vbus','api':1}],'status':'experimental-unpublished'}
  (src/'manifest.json').write_text(json.dumps(manifest))
  elf=bytearray(52);elf[:7]=b'\x7fELF\x01\x01\x01';elf[16:18]=(3).to_bytes(2,'little');elf[18:20]=(94).to_bytes(2,'little')
  def output(args,**kwargs):
   if args[0]=='git':
    self.assertEqual(args,['git','-C',str(self.root),'show','a7ae39994603c4545a3c014f09051ecfc12084d2:sdk/driver/header.h'])
    return b'header'
   if '--version' in args:return 'synthetic fixture'
   if '-print-libgcc-file-name' in args:return str(lib)
   if '--dyn-syms' in args:return '1: 0 0 FUNC GLOBAL DEFAULT 1 t5_driver_get'
   raise AssertionError(args)
  def run(args,**kwargs):Path(args[args.index('-o')+1]).write_bytes(elf)
  out=self.root/'accepted';out.mkdir();(out/'driver.elf').write_bytes(b'old accepted');stderr=io.StringIO()
  with ExitStack() as stack:
   for name,value in [('ROOT',self.root),('HEADERS',{'header.h':hashlib.sha256(b'header').hexdigest()}),('NORMALIZER',hashlib.sha256(b'helper').hexdigest())]:stack.enter_context(patch.object(b,name,value))
   stack.enter_context(patch.object(b,'select_source',return_value=(src,{'mode':'historical','expected_version':'0.1.6','source_tree':b.HISTORICAL_TREE})))
   stack.enter_context(patch.object(b,'compiler',return_value=str(cc)));stack.enter_context(patch.object(b,'normalize'))
   stack.enter_context(patch.object(b.subprocess,'check_output',side_effect=output));stack.enter_context(patch.object(b.subprocess,'run',side_effect=run))
   stack.enter_context(patch.object(sys,'argv',['builder','--require-byte-parity','--output',str(out)]));stack.enter_context(redirect_stderr(stderr))
   with self.assertRaises(SystemExit):b.main()
  receipt=json.loads(stderr.getvalue().split('BQ25896_BUILD_FAILURE ',1)[1])
  self.assertEqual(receipt['actual_sha256'],hashlib.sha256(elf).hexdigest());self.assertEqual(receipt['actual_size'],52)
  self.assertEqual(receipt['accepted_historical_sha256'],b.CANONICAL_SHA256);self.assertEqual(receipt['compiler_sha256'],hashlib.sha256(b'compiler').hexdigest())
  self.assertEqual(receipt['compiler'],str(cc));self.assertEqual(receipt['compiler_version'],'synthetic fixture')
  self.assertEqual(receipt['historical_build_commit'],'a7ae39994603c4545a3c014f09051ecfc12084d2')
  self.assertEqual(receipt['selected_sdk_inputs'],{'header.h':hashlib.sha256(b'header').hexdigest()})
  self.assertEqual(receipt['libgcc'],{'path':str(lib),'sha256':hashlib.sha256(b'archive').hexdigest()})
  self.assertEqual((out/'driver.elf').read_bytes(),b'old accepted')

if __name__=='__main__':unittest.main()

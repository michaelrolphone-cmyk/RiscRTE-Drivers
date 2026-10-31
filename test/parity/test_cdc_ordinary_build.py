import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
import build_usb_cdc_stream as b

class OrdinaryBuild(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name);self.output=self.root/'dist/candidate'
        for name in ('Drivers/usb_cdc_stream','Drivers/common','sdk/driver','scripts','bin'):(self.root/name).mkdir(parents=True)
        (self.root/b.SOURCE/'manifest.json').write_text(json.dumps(b.REQUIRED))
        (self.root/b.SOURCE/'driver.c').write_text('source')
        (self.root/'Drivers/common/SerialStreamPump.inc').write_text('pump')
        (self.root/'sdk/driver/header.h').write_text('header')
        for name in ('build_usb_cdc_stream.py','build_platform_clock_v1.py','native_driver_toolchain.py','normalize_xtensa_relocations.py'):(self.root/'scripts'/name).write_text(name)
        self.cc=self.root/'bin/xtensa-gcc';self.cc.write_text('compiler')
        (self.root/'bin/xtensa-readelf').write_text('readelf');self.lib=self.root/'libgcc.a';self.lib.write_text('library')
        self.data=bytearray(52);self.data[:7]=b'\x7fELF\x01\x01\x01';self.data[16:18]=(3).to_bytes(2,'little');self.data[18:20]=(94).to_bytes(2,'little')
        self.symbols='1: 0 0 FUNC GLOBAL DEFAULT 1 t5_driver_get\n2: 0 0 NOTYPE GLOBAL DEFAULT UND memcpy\n'
        self.mutate=None
        def output(args,**kwargs):
            if '-print-libgcc-file-name' in args:return str(self.lib)+'\n'
            if '--version' in args:return 'fixture compiler\n'
            return self.symbols
        def compile(args,**kwargs):
            Path(args[args.index('-o')+1]).write_bytes(self.data)
            if self.mutate:self.mutate()
        for target,value in [('compiler',lambda:str(self.cc)),('normalize',lambda p:None),('subprocess.check_output',output),('subprocess.run',compile)]:
            mock=patch.object(b,target,value) if '.' not in target else patch('build_usb_cdc_stream.'+target,value)
            mock.start();self.addCleanup(mock.stop)
    def test_success_identity_hashes_and_complete_input_snapshot(self):
        receipt=b.build(self.root,self.output)
        self.assertEqual(receipt['candidate_identity']['source_path'],b.SOURCE)
        self.assertFalse(receipt['historical_replay'])
        self.assertEqual(receipt['output']['elf_sha256'],hashlib.sha256(self.data).hexdigest())
        self.assertIn('Drivers/common/SerialStreamPump.inc',receipt['inputs'])
        self.assertTrue(json.loads((self.output/'manifest.json').read_text())['candidate_only'])
    def test_other_variant_output_is_refused(self):
        for name in ('usb-cdc-acm','usb-cdc-acm-v2','usb-cdc-acm-native-stream'):
            for suffix in ('','/nested'):
                with self.assertRaises(ValueError):b.select_output(self.root,self.root/'dist'/(name+suffix))
    def test_manifest_identity_or_dependency_drift_refused(self):
        for change in ({'version':'0.1.9'},{'id':'usb-cdc-acm-v2'},{'requires':[]}):
            (self.root/b.SOURCE/'manifest.json').write_text(json.dumps(dict(b.REQUIRED,**change)))
            with self.assertRaises(ValueError):b.build(self.root,self.output)
        self.assertFalse(self.output.exists())
    def test_changed_pump_or_tool_cannot_publish_success(self):
        for path in (self.root/'Drivers/common/SerialStreamPump.inc',self.cc):
            self.mutate=lambda p=path:p.write_text('changed-'+p.read_text())
            with self.assertRaises(ValueError):b.build(self.root,self.output)
            self.assertFalse(self.output.exists())
    def test_wrong_elf_and_unexpected_import_refused(self):
        self.data[18:20]=(62).to_bytes(2,'little')
        with self.assertRaises(ValueError):b.build(self.root,self.output)
        self.data[18:20]=(94).to_bytes(2,'little');self.symbols+='3: 0 0 NOTYPE GLOBAL DEFAULT UND nativeUsb\n'
        with self.assertRaises(ValueError):b.build(self.root,self.output)
        self.assertFalse(self.output.exists())
    def test_failure_does_not_relabel_or_replace_prior_receipt(self):
        b.build(self.root,self.output);before={p.name:p.read_bytes() for p in self.output.iterdir()}
        self.symbols+='3: 0 0 NOTYPE GLOBAL DEFAULT UND invalid\n'
        with self.assertRaises(ValueError):b.build(self.root,self.output)
        self.assertEqual(before,{p.name:p.read_bytes() for p in self.output.iterdir()})

if __name__=='__main__':unittest.main()

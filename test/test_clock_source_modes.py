import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
import build_platform_clock_v1 as clock

class ClockModes(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name)

    def test_default_source_is_exact_git_tree_not_working_copy(self):
        (self.root/'Drivers/platform_clock_v1').mkdir(parents=True)
        (self.root/'Drivers/platform_clock_v1/driver.c').write_text('changed current')
        def show(args):
            self.assertEqual(args[-1].split(':')[0],clock.HISTORICAL_TREE)
            return b'old source' if args[-1].endswith('driver.c') else b'{"version":"0.1.0"}'
        with patch.object(clock.subprocess,'check_output',side_effect=show):
            src,p=clock.source_inputs(self.root,False,self.root)
        self.assertEqual((src/'driver.c').read_bytes(),b'old source')
        self.assertEqual(p['expected_version'],'0.1.0')

    def test_current_is_explicit_and_separate(self):
        src,p=clock.source_inputs(self.root,True,self.root)
        self.assertEqual(src,self.root/'Drivers/platform_clock_v1')
        self.assertEqual(p['expected_version'],'0.1.1')
        self.assertNotEqual(clock.output_path(self.root,True,None),clock.output_path(self.root,False,None))

    def test_current_rejects_historical_output_and_descendant(self):
        for path in ('dist/platform-clock-v1','dist/platform-clock-v1/sub'):
            with self.assertRaises(ValueError):clock.output_path(self.root,True,self.root/path)

    def test_current_symlink_alias_to_historical_output_rejected(self):
        historical=self.root/'dist/platform-clock-v1';historical.mkdir(parents=True)
        alias=self.root/'alias';alias.symlink_to(historical,target_is_directory=True)
        with self.assertRaises(ValueError):clock.output_path(self.root,True,alias)
        # Explicit nonhistorical aliases resolve normally; publish still refuses
        # a differing retained file at the resolved destination.
        other=self.root/'other';other.mkdir();link=self.root/'other-link';link.symlink_to(other,target_is_directory=True)
        self.assertEqual(clock.output_path(self.root,True,link),other.resolve())

    def test_output_keeps_existing_bytes_and_fails_before_partial_copy(self):
        stage=self.root/'stage';stage.mkdir();out=self.root/'out';out.mkdir()
        (stage/'a').write_text('new');(stage/'driver.elf').write_text('different')
        (out/'driver.elf').write_text('accepted')
        with self.assertRaises(ValueError):clock.publish_outputs(stage,out)
        self.assertEqual((out/'driver.elf').read_text(),'accepted');self.assertFalse((out/'a').exists())

    def test_identical_output_is_idempotent(self):
        stage=self.root/'stage';stage.mkdir();out=self.root/'out'
        (stage/'driver.elf').write_text('accepted')
        clock.publish_outputs(stage,out);clock.publish_outputs(stage,out)
        self.assertEqual((out/'driver.elf').read_text(),'accepted')

    def test_symlink_output_not_followed(self):
        stage=self.root/'stage';stage.mkdir();out=self.root/'out';out.mkdir()
        (stage/'driver.elf').write_text('accepted');target=self.root/'accepted';target.write_text('accepted')
        (out/'driver.elf').symlink_to(target)
        with self.assertRaises(ValueError):clock.publish_outputs(stage,out)

    def test_current_cannot_claim_historical_parity(self):
        with patch.object(sys,'argv',['builder','--current-source','--require-byte-parity']):
            with self.assertRaises(SystemExit) as caught:clock.main()
        self.assertEqual(caught.exception.code,2)

    def test_parity_failure_logs_exact_evidence_without_touching_output(self):
        import hashlib,io
        from contextlib import ExitStack,redirect_stderr
        scripts=self.root/'scripts';scripts.mkdir()
        for name in ('build_platform_clock_v1.py','native_driver_toolchain.py','normalize_xtensa_relocations.py'):
            (scripts/name).write_bytes(b'helper')
        cc=self.root/'xtensa-gcc';cc.write_bytes(b'compiler')
        (self.root/'xtensa-readelf').write_bytes(b'readelf')
        lib=self.root/'libgcc.a';lib.write_bytes(b'archive')
        src=self.root/'src';src.mkdir();(src/'driver.c').write_text('source')
        manifest={'type':'driver','id':'platform-clock-v1','version':'0.1.0','driver_abi':2,'architecture':'xtensa-esp32s3','file_name':'driver.elf','requires':[],'provides':[{'capability':'platform.clock','api':1}],'status':'experimental-unpublished'}
        (src/'manifest.json').write_text(json.dumps(manifest))
        elf=bytearray(52);elf[:7]=b'\x7fELF\x01\x01\x01';elf[16:18]=(3).to_bytes(2,'little');elf[18:20]=(94).to_bytes(2,'little')
        def output(args,**kwargs):
            if args[0]=='git':return b'header'
            if '--version' in args:return 'fake version'
            if '-print-libgcc-file-name' in args:return str(lib)
            if '--dyn-syms' in args:return '1: 0 0 FUNC GLOBAL DEFAULT 1 t5_driver_get'
            raise AssertionError(args)
        def run(args,**kwargs):Path(args[args.index('-o')+1]).write_bytes(elf)
        out=self.root/'accepted';out.mkdir();(out/'driver.elf').write_bytes(b'old accepted')
        stderr=io.StringIO()
        with ExitStack() as stack:
            for name,value in [('ROOT',self.root),('HISTORICAL_HEADERS',{'clock.h':hashlib.sha256(b'header').hexdigest()}),('HISTORICAL_NORMALIZER',hashlib.sha256(b'helper').hexdigest())]:stack.enter_context(patch.object(clock,name,value))
            stack.enter_context(patch.object(clock,'compiler',return_value=str(cc)))
            stack.enter_context(patch.object(clock,'is_unified',return_value=False))
            stack.enter_context(patch.object(clock,'native_support_sources',return_value=()))
            stack.enter_context(patch.object(clock,'normalize'))
            stack.enter_context(patch.object(clock.subprocess,'check_output',side_effect=output))
            stack.enter_context(patch.object(clock.subprocess,'run',side_effect=run))
            stack.enter_context(redirect_stderr(stderr))
            with self.assertRaises(SystemExit):clock.build(src,out,{'expected_version':'0.1.0','mode':'historical','source_tree':clock.HISTORICAL_TREE},True)
        receipt=json.loads(stderr.getvalue().split('CLOCK_BUILD_FAILURE ',1)[1])
        self.assertEqual(receipt['actual_size'],52)
        self.assertEqual(receipt['actual_sha256'],hashlib.sha256(elf).hexdigest())
        self.assertEqual(receipt['accepted_historical_sha256'],clock.ACCEPTED_SHA256)
        self.assertEqual(receipt['compiler_sha256'],hashlib.sha256(b'compiler').hexdigest())
        self.assertEqual((out/'driver.elf').read_bytes(),b'old accepted')

    def test_historical_header_hash_rejection_cleans_staging(self):
        observed=[];original=clock.build_staged
        def track(src,out,provenance,required):
            observed.append(out)
            return original(src,out,provenance,required)
        with patch.object(clock,'build_staged',side_effect=track), patch.object(clock.subprocess,'check_output',return_value=b'altered historical header') as read:
            with self.assertRaisesRegex(ValueError,'historical SDK input mismatch'):
                clock.build(self.root,self.root/'output',{'expected_version':'0.1.0'},True)
        self.assertIn(clock.HISTORICAL_BUILD_COMMIT+':sdk/driver/RiscPlatformClockV1.h',read.call_args.args[0])
        self.assertFalse(observed[0].exists())
        self.assertFalse((self.root/'output').exists())

    def test_failed_staged_build_never_modifies_retained_output(self):
        out=self.root/'out';out.mkdir();(out/'driver.elf').write_text('accepted')
        with patch.object(clock,'build_staged',side_effect=SystemExit('mismatch')):
            with self.assertRaises(SystemExit):clock.build(self.root,out,{},True)
        self.assertEqual((out/'driver.elf').read_text(),'accepted')

if __name__=='__main__':unittest.main()

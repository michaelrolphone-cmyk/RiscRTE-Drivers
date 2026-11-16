"""Focused modeled build-boundary and real ELF-parser profile regression tests."""
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'scripts'))
import build_badge_native as builder
spec = importlib.util.spec_from_file_location('badge_native_verifier', ROOT/'test/test_badge_native.py')
verifier = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verifier)

class Builder(unittest.TestCase):
    def invoke(self, prototype=False, quick=False, wrong=False):
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp);out = base/'out'
            arguments = ['builder','--runtime',str(base/'runtime'),'--idf',str(base/'idf'),
                         '--cc',str(base/'bin/xtensa-esp32s3-elf-gcc'),'--output',str(out)]
            if prototype:arguments += ['--prototype']
            if quick:arguments += ['--quick-actions']
            calls=[];pins=[]
            expected = builder.selected_profile(prototype)[1]
            def pin(path, revision, label):
                pins.append((str(path),revision,label))
                if wrong and label=='Runtime':raise RuntimeError('wrong actual Runtime')
                return revision
            def output(command, **kwargs):
                if '--version' in command:return 'gcc esp-15.2.0_20251204'
                if '-dumpfullversion' in command:return '15.2.0'
                if '-dM' in command:return '#define __XTENSA_EL__ 1\n#define __XTENSA_WINDOWED_ABI__ 1'
                raise AssertionError(command)
            def run(command, log, env):
                command=list(map(str,command));calls.append(command)
                if '--build' in command and len(command)>1 and command[0]=='/tools/cmake':
                    build=out/'build'
                    for name in ['riscrte_native.elf','riscrte_native.bin','bootloader/bootloader.bin','partition_table/partition-table.bin']:
                        p=build/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(b'modeled boundary only')
            with patch.object(sys,'argv',arguments),patch.object(builder,'output',side_effect=output),patch.object(builder,'pinned_checkout',side_effect=pin),patch.object(builder,'run',side_effect=run),patch.object(builder.shutil,'which',side_effect=lambda name,**kw:'/tools/'+name):
                if wrong:
                    out.mkdir();(out/'build-proof.json').write_text('stale')
                    with self.assertRaises(RuntimeError):builder.main()
                    self.assertFalse((out/'build-proof.json').exists())
                    self.assertEqual(calls,[])
                    return
                builder.main()
            report=json.loads((out/'build-proof.json').read_text())
            self.assertEqual(report['runtime_revision'],expected)
            self.assertEqual(report['prototype'],prototype)
            self.assertEqual(report['quick_actions'],quick)
            self.assertFalse(report['hardware_qualified'])
            self.assertTrue(all(revision==expected for _,revision,label in pins if label=='Runtime'))
            config=calls[0];verify=calls[-1]
            selected=builder.BOARD.parent/'prototype/native' if prototype else builder.BOARD
            self.assertIn('-DRISC_PRODUCT_PROFILE='+str(selected/'product-profile.cmake'),config)
            self.assertIn('-DRISC_SOURCE_SHA='+expected,config)
            self.assertEqual('--prototype' in verify,prototype)
            self.assertEqual('--quick-actions' in verify,quick)
            self.assertEqual([x for x in config if x.startswith('-DRISC_BADGE_QUICK_ACTIONS=')],
                             ['-DRISC_BADGE_QUICK_ACTIONS='+('ON' if quick else 'OFF')] if prototype else [])
            self.assertIn('boards/waveshare-esp32-s3-touch-amoled-1.75c/native/BadgeIdentity.cpp',report['sources_sha256'])
            self.assertIn('boards/waveshare-esp32-s3-touch-amoled-1.75c/display-profile.h',report['sources_sha256'])
            if prototype:self.assertIn('boards/waveshare-esp32-s3-touch-amoled-1.75c/prototype/sources.lock.json',report['sources_sha256'])
            self.assertEqual(calls[1][-2:],['--','-j1'])

    def test_default_unchanged_pin_and_profile(self):self.invoke()
    def test_prototype_exact_lock(self):self.invoke(True)
    def test_quick_selection_and_validation_forwarded(self):self.invoke(True,True)
    def test_wrong_actual_runtime_refuses_before_build(self):self.invoke(True,wrong=True)
    def test_no_global_profile_mutation_across_calls(self):
        previous=(builder.BOARD,builder.RUNTIME_REVISION)
        self.invoke(True,True);self.invoke()
        self.assertEqual((builder.BOARD,builder.RUNTIME_REVISION),previous)
    def test_quick_without_prototype_refuses_before_output(self):
        with tempfile.TemporaryDirectory() as tmp,patch.object(builder,'run') as run:
            out=Path(tmp)/'out'
            with patch.object(sys,'argv',['build','--runtime',tmp,'--idf',tmp,'--cc','cc','--output',str(out),'--quick-actions']):
                with self.assertRaises(SystemExit):builder.main()
            run.assert_not_called();self.assertFalse(out.exists())
    def test_source_lock_is_exact_and_owning(self):
        for entry in [{'repository':'other/repo','commit':'a'*40},
                      {'repository':'michaelrolphone-cmyk/RiscRTE','commit':'main'}]:
            with patch.object(builder.json,'loads',return_value={'runtime':entry}):
                with self.assertRaises(ValueError):builder.selected_profile(True)


def elf_fixture(path):
    names=['risc_badge_native_identity','risc_co5300_native_display_anchor',
           'selectedDisplayBackend','nativeHardware','Port4bind','i2cOpen','gpioOpen']
    strings=b'\0';offsets=[]
    for name in names:offsets.append(len(strings));strings+=name.encode()+b'\0'
    shstrings=b'\0.shstrtab\0.strtab\0.symtab\0.flash.rodata\0'
    identity=struct.pack('<7I',1,466,466,6,0,32*1024*1024,8*1024*1024)
    symbols=bytes(16)+b''.join(struct.pack('<IIIBBH',n,0x3c000020,28 if i==0 else 0,0x11,0,4) for i,n in enumerate(offsets))
    data=bytearray(52);sections=[(0,0,0,0,0,0,0,0,0,0)]
    for name,kind,flags,address,blob,link,entsize in [
        ('.shstrtab',3,0,0,shstrings,0,0),('.strtab',3,0,0,strings,0,0),
        ('.symtab',2,0,0,symbols,2,16),('.flash.rodata',1,3,0x3c000020,identity,0,0)]:
        while len(data)%4:data.append(0)
        offset=len(data);data.extend(blob)
        sections.append((shstrings.index(name.encode()),kind,flags,address,offset,len(blob),link,1 if kind==2 else 0,4,entsize))
    while len(data)%4:data.append(0)
    shoff=len(data)
    data.extend(b''.join(struct.pack('<10I',*section) for section in sections))
    data[:52]=struct.pack('<16sHHIIIIIHHHHHH',b'\x7fELF\x01\x01\x01'+bytes(9),2,94,1,0,0,shoff,0,52,0,0,40,5,1)
    path.write_bytes(data)


class LinkedProfile(unittest.TestCase):
    def fixture(self, root, prototype, quick):
        build=root/'build';runtime=root/'runtime'
        (build/'config').mkdir(parents=True);(build/'partition_table').mkdir()
        config=dict(ESPTOOLPY_FLASHSIZE='32MB',SPIRAM=True,SPIRAM_MODE_OCT=True,
                    SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=True,LIBC_TIME_SYSCALL_USE_RTC_HRT=True,
                    SPIFFS_PAGE_SIZE=512,SPIFFS_OBJ_NAME_LEN=32,SPIFFS_META_LENGTH=0,
                    SPIFFS_USE_MAGIC=True,SPIFFS_USE_MAGIC_LENGTH=True,
                    PARTITION_TABLE_CUSTOM_FILENAME=str(verifier.BOARD/('prototype/native/partitions.csv' if prototype else 'native/partitions.csv')))
        (build/'config/sdkconfig.json').write_text(json.dumps(config))
        args=['-DRISC_NATIVE_ESP_IDF=1','-DRISC_APP_REQUIREMENT_ROWS=24',str(verifier.BOARD/'display-profile.h')]
        if prototype:args+=['-D'+v for v in ['RISC_PAIRED_APP_DATA=1','RISC_NATIVE_APP_DATA_MOUNT=1','RISC_NATIVE_STANDALONE_APP_DATA=1','RISC_NATIVE_APP_DATA_OFFSET=0x1f00000','RISC_NATIVE_APP_DATA_BYTES=0x100000','RISC_NATIVE_FLASH_BYTES=0x2000000']]
        if quick:args+=['-DRISC_RUNTIME_PREFERENCE_STORE=1','-DRISC_RUNTIME_ATOMIC_KEY_VALUE=0']
        paths=[runtime/'src/bootstrap/Runtime.cpp',runtime/'src/ports/esp32s3/NativeHardware.cpp',
               runtime/'src/ports/esp32s3/CpuPort.cpp',ROOT/'components/co5300/native/Co5300Display.cpp',
               verifier.BOARD/'native/BadgeIdentity.cpp']
        (build/'compile_commands.json').write_text(json.dumps([{'file':str(p),'arguments':args} for p in paths]))
        parts=[('nvs',1,2,0x9000,0x6000),('factory',0,0,0x10000,0x300000),
               ('bootfs',1,0x82,0x310000,0x1bf0000 if prototype else 0x1cf0000)]
        if prototype:parts.append(('appdata',1,0x41,0x1f00000,0x100000))
        (build/'partition_table/partition-table.bin').write_bytes(b''.join(
            struct.pack('<HBBII16sI',0x50aa,kind,sub,start,size,name.encode(),0)
            for name,kind,sub,start,size in parts))
        elf_fixture(build/'riscrte_native.elf');(build/'riscrte_native.bin').write_bytes(b'synthetic-not-firmware')
        return build,runtime

    def test_all_profiles_and_existing_plus_selected_mutations(self):
        for prototype,quick in [(False,False),(True,False),(True,True)]:
            with self.subTest(prototype=prototype,quick=quick),tempfile.TemporaryDirectory() as tmp:
                build,runtime=self.fixture(Path(tmp),prototype,quick)
                proof=verifier.verify(build,runtime,prototype,quick)
                self.assertFalse(proof['hardware_qualified'])
                self.assertEqual(proof['quick_actions'],quick)
                cases=verifier.negative_checks(build,runtime,prototype,quick)
                self.assertEqual(len(cases),6+4*prototype+2*quick)

    def test_wrong_profile_and_quick_evidence_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            build,runtime=self.fixture(Path(tmp),True,True)
            for p,q in [(False,False),(True,False),(False,True)]:
                with self.assertRaises(AssertionError):verifier.verify(build,runtime,p,q)

    def test_one_required_tu_cannot_hide_conflicting_selection(self):
        with tempfile.TemporaryDirectory() as tmp:
            build,runtime=self.fixture(Path(tmp),True,True)
            path=build/'compile_commands.json';original=json.loads(path.read_text())
            for index in range(len(original)):
                for flag in ['-URISC_RUNTIME_PREFERENCE_STORE','-DRISC_RUNTIME_ATOMIC_KEY_VALUE=1',
                             '-DRISC_NATIVE_APP_DATA_BYTES=0x200000','-DRISC_PAIRED_APP_DATA=1']:
                    rows=json.loads(json.dumps(original));rows[index]['arguments'].append(flag);path.write_text(json.dumps(rows))
                    with self.assertRaises(AssertionError):verifier.verify(build,runtime,True,True)

    def test_checker_cli_accepts_selected_profile(self):
        with tempfile.TemporaryDirectory() as tmp:
            build,runtime=self.fixture(Path(tmp),True,True)
            result=subprocess.run([sys.executable,str(ROOT/'test/test_badge_native.py'),'--prototype','--quick-actions',
                                   '--build',str(build),'--runtime',str(runtime)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)


class TimecardImports(unittest.TestCase):
    def test_direct_test_entry_from_foreign_cwd(self):
        with tempfile.TemporaryDirectory() as tmp:
            env={k:v for k,v in os.environ.items() if k!='PYTHONPATH'}
            result=subprocess.run([sys.executable,str(ROOT/'tests/test_timecard_evidence_gate.py'),'-v'],
                                  cwd=tmp,env=env,capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertIn('Ran 7 tests',result.stderr)
    def test_direct_script_and_script_caller_import(self):
        env={k:v for k,v in os.environ.items() if k!='PYTHONPATH'}
        with tempfile.TemporaryDirectory() as tmp:
            result=subprocess.run([sys.executable,str(ROOT/'scripts/timecard_evidence_gate.py')],
                                  cwd=tmp,env=env,capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            code="import timecard_evidence_gate as gate, execution_source_inputs as source; assert gate.validated_inputs is source.validated_inputs"
            result=subprocess.run([sys.executable,'-c',code],cwd=ROOT/'scripts',env=env,capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)

if __name__=='__main__':unittest.main()

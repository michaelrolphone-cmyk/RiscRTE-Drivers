#!/usr/bin/env python3
"""Pinned ordinary Reader consumer + current checked host + distinct CDC candidate.

No current-native-Runtime or hardware acceptance is inferred.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
CPP=('src/runtime/streams/StreamRuntime.cpp','src/native/NativeStreamBridge.cpp',
     'src/native/NativeSerialPortBridge.cpp','src/runtime/drivers/ProviderModuleV2.cpp',
     'src/runtime/drivers/ProviderGraphV2.cpp','test/drivers/usb_witness_provider_graph_test.cpp')

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def run(root, reader, mode, receipt):
    declaration=json.loads((root/'test/drivers/cdc_ordinary_consumer_inputs.json').read_text())
    inputs={}
    for name,expected in declaration['files'].items():
        path=reader/name
        if path.is_symlink() or sha(path)!=expected['sha256']:
            raise ValueError('pinned Reader consumer input differs: '+name)
        inputs['reader/'+name]=sha(path)
    current_headers=set(declaration['current_sdk_headers'])|{'RiscUsbControllerV1.h','RiscUsbProviderV1.h','RiscUsbInterruptV1.h','RiscUsbDiscoveryDiagnosticsV1.h'}
    for name in current_headers:
        inputs['drivers/sdk/driver/'+name]=sha(root/'sdk/driver'/name)
    current=('Drivers/usb_cdc_stream/driver.c','Drivers/usb_cdc_stream/manifest.json',
             'Drivers/common/SerialStreamPump.inc','Drivers/usb_host_v2/driver.c','Drivers/usb_host_v2/manifest.json')
    for name in current+('test/run_cdc_ordinary.py','test/drivers/cdc_ordinary_consumer_inputs.json','test/drivers/cdc_ordinary_test.c','test/drivers/cdc_ordinary_descriptors_test.c'):inputs['drivers/'+name]=sha(root/name)
    flags=['-Wall','-Wextra','-Werror']
    if mode=='sanitized':flags+=['-fsanitize=undefined','-fno-sanitize-recover=all']
    commands=[]
    def command(args):
        commands.append(args);subprocess.run(args,check=True)
    with tempfile.TemporaryDirectory(prefix='cdc-ordinary-test-') as temp:
        tmp=Path(temp);sdk=tmp/'sdk';sdk.mkdir()
        for name in sorted(current_headers):shutil.copyfile(root/'sdk/driver'/name,sdk/name)
        for name in declaration['sdk_additions']:
            if (sdk/name).exists():raise ValueError('Reader addition shadows current SDK: '+name)
            shutil.copyfile(reader/'sdk/driver'/name,sdk/name)
        consumed=set();scan_commands=[]
        declared={}
        for key in inputs:
            base,name=key.split('/',1)
            declared[((reader if base=='reader' else root)/name).resolve()]=key
        # Separate non-system dependency scans cover every compiled C/C++ TU.
        # They are not an object-build trace or exhaustive system closure.
        def scan(prefix,source):
            command=[*prefix,'-MM','-MT','cdc_inputs',str(source)]
            scan_commands.append(command)
            out=subprocess.check_output(command,text=True)
            for token in shlex.split(out.split(':',1)[1].replace('\\\n',' ')):
                path=Path(token).resolve()
                if path.is_relative_to(sdk):
                    name=path.name
                    if name not in current_headers and name not in declaration['sdk_additions']:raise ValueError('undeclared SDK dependency: '+name)
                    key=('drivers/sdk/driver/' if name in current_headers else 'reader/sdk/driver/')+name
                else:
                    key=declared.get(path)
                    if key is None:raise ValueError('undeclared non-system dependency: '+str(path))
                consumed.add(key)
        for name,source,extra in [('controller',reader/'test/drivers/mock_usb_witness_controller_elf.c',['-DSERIAL_FIXTURE=1']),
             ('host',root/'Drivers/usb_host_v2/driver.c',[]),('cdc',root/'Drivers/usb_cdc_stream/driver.c',[])]:
            prefix=['cc','-std=c11',*flags,'-fPIC','-fvisibility=hidden','-shared',*extra,'-I'+str(sdk)]
            scan(prefix,source)
            command([*prefix,str(source),'-o',str(tmp/(name+'.so'))])
        for name in ('cdc_ordinary_test','cdc_ordinary_descriptors_test'):
            scan(['cc','-std=c11',*flags,'-I'+str(sdk)],root/'test/drivers'/(name+'.c'))
            command(['cc','-std=c11',*flags,'-I'+str(sdk),str(root/'test/drivers'/(name+'.c')),'-ldl','-o',str(tmp/name)])
            command([str(tmp/name),str(tmp/'cdc.so')])
        include=['-I'+str(sdk),'-I'+str(reader/'lib/NativeApps/include'),'-I'+str(reader/'test/streams/stubs'),'-I'+str(reader/'test/drivers/stubs'),'-I'+str(reader/'src')]
        cpp=['c++','-std=c++17',*flags,'-DRISCRTE_TEST_INSTALLED_SERIAL_PATH',*include]
        for name in CPP+('test/drivers/installed_serial_inventory_test.cpp',):
            scan(cpp,reader/name)
        command([*cpp,*(str(reader/name) for name in CPP),'-ldl','-o',str(tmp/'consumer')])
        command([str(tmp/'consumer'),str(tmp/'controller.so'),str(tmp/'host.so'),str(tmp/'cdc.so'),'usb-cdc-acm'])
        # Same actual inventory algorithm, with failure/unknown-identity fixture.
        command([*cpp,str(reader/'test/drivers/installed_serial_inventory_test.cpp'),'-o',str(tmp/'inventory')])
        command([str(tmp/'inventory')])
        exports=subprocess.check_output(['nm','-D','--defined-only',str(tmp/'cdc.so')],text=True)
        if {line.split()[-1] for line in exports.splitlines()}!={'t5_driver_get'}:raise ValueError('unexpected CDC exports')
        for key,digest in inputs.items():
            base,name=key.split('/',1)
            if sha((reader if base=='reader' else root)/name)!=digest:raise ValueError('test input changed: '+key)
        result={'mode':mode,'reader_commit':declaration['commit'],'reader_scope':declaration['scope'],
            'inputs':inputs,'selected_dependency_scan':sorted(consumed),'sdk_policy':'current declared headers plus hash-pinned RiscPackageResourcesV1 Reader addition in temporary consumer-only SDK; no global overwrite',
            'commands':commands,'dependency_scan_commands':scan_commands,'result':'pass','limits':'simulated controller; actual pinned ordinary installed inventory/stream/graph and actual current checked host; not current native Runtime or physical acceptance'}
        if receipt:receipt.write_text(json.dumps(result,indent=2)+'\n')
        return result

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--reader-root',type=Path,required=True);p.add_argument('--drivers-root',type=Path,default=ROOT);p.add_argument('--mode',choices=('normal','sanitized'),default='normal');p.add_argument('--receipt',type=Path)
    args=p.parse_args();run(args.drivers_root.resolve(),args.reader_root.resolve(),args.mode,args.receipt)
if __name__=='__main__':main()

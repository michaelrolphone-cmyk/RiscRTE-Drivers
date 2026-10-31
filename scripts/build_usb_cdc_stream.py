#!/usr/bin/env python3
"""Build the separate ordinary CDC candidate; never replay a historical variant."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
from build_platform_clock_v1 import file_sha256, publish_outputs
from native_driver_toolchain import compiler
from normalize_xtensa_relocations import normalize

ROOT = Path(__file__).resolve().parents[1]
SOURCE = 'Drivers/usb_cdc_stream'
READER_COMMIT = '9c8783d6f20659a514bfaeb3847a77c7213dc4f6'
REQUIRED = {'type':'driver','id':'usb-cdc-acm','version':'0.1.8','driver_abi':2,
    'architecture':'xtensa-esp32s3','file_name':'driver.elf',
    'requires':[{'capability':'usb.host','api':1}],
    'provides':[{'capability':'serial.port','api':1}],'status':'u1-class-functional'}


def select_output(root, requested=None):
    output = Path(requested).resolve() if requested else (root/'dist/usb-cdc-acm-ordinary-candidate').resolve()
    for name in ('usb-cdc-acm','usb-cdc-acm-v2','usb-cdc-acm-native-stream'):
        historical = (root/'dist'/name).resolve()
        if output == historical or historical in output.parents:
            raise ValueError('ordinary candidate must not overwrite another CDC variant')
    return output


def input_snapshot(root):
    paths = {p for directory in (root/SOURCE,root/'sdk/driver') for p in directory.rglob('*') if p.is_file()}
    paths.add(root/'Drivers/common/SerialStreamPump.inc')
    paths.update(root/'scripts'/name for name in ('build_usb_cdc_stream.py','build_platform_clock_v1.py','native_driver_toolchain.py','normalize_xtensa_relocations.py'))
    return {str(p.relative_to(root)):file_sha256(p) for p in sorted(paths)}


def build(root, output):
    manifest = json.loads((root/SOURCE/'manifest.json').read_text())
    if manifest != REQUIRED:
        raise ValueError('ordinary canonical CDC candidate manifest mismatch')
    inputs = input_snapshot(root)
    cc = compiler()
    readelf = str(Path(cc).with_name(Path(cc).name.replace('gcc','readelf')))
    libgcc = subprocess.check_output([cc,'-print-libgcc-file-name'],text=True).strip()
    tools = {name:{'path':path,'sha256':file_sha256(path)} for name,path in [('compiler',cc),('readelf',readelf),('libgcc',libgcc)]}
    tools['compiler']['version'] = subprocess.check_output([cc,'--version'],text=True)
    with tempfile.TemporaryDirectory(prefix='ordinary-cdc-') as temporary:
        stage = Path(temporary);elf = stage/'driver.elf'
        command = [cc,'-std=c11','-Os','-fPIC','-mtext-section-literals','-mlongcalls',
            '-fvisibility=hidden','-nostdlib','-nostartfiles','-shared','-I'+str(root/'sdk/driver'),
            '-Wl,--hash-style=sysv','-Wl,--exclude-libs,ALL',str(root/SOURCE/'driver.c'),'-lgcc','-o',str(elf)]
        subprocess.run(command,check=True)
        normalize(elf)
        symbols = subprocess.check_output([readelf,'--dyn-syms','--wide',str(elf)],text=True)
        rows = [line.split() for line in symbols.splitlines() if len(line.split())>=8]
        exports = {f[7] for f in rows if f[4]=='GLOBAL' and f[6]!='UND' and f[3]=='FUNC'}
        imports = {f[7] for f in rows if f[6]=='UND'}
        if exports != {'t5_driver_get'} or not imports <= {'memcpy','memset'}:
            raise ValueError('unexpected ordinary CDC imports/exports')
        data = elf.read_bytes()
        if not 52<=len(data)<=256*1024 or data[:7]!=b'\x7fELF\x01\x01\x01' or int.from_bytes(data[16:18],'little')!=3 or int.from_bytes(data[18:20],'little')!=94:
            raise ValueError('invalid Xtensa shared driver')
        if input_snapshot(root)!=inputs or any(file_sha256(t['path'])!=t['sha256'] for t in tools.values()):
            raise ValueError('build inputs or tools changed during build')
        digest = hashlib.sha256(data).hexdigest()
        meta = dict(manifest,size_bytes=len(data),sha256=digest,candidate_only=True,byte_parity=False)
        (stage/'manifest.json').write_text(json.dumps(meta,indent=2)+'\n')
        receipt = {'schema':1,'mode':'ordinary-source-candidate','candidate_identity':{'id':manifest['id'],'version':manifest['version'],'source_path':SOURCE},
            'upstream_reference':{'repository':'michaelrolphone-cmyk/T5S3-Reader','commit':READER_COMMIT,'path':'Drivers/usb_cdc_v2'},
            'inputs':inputs,'input_scope':'complete source directory, shared pump, all available driver SDK headers and build helpers; SDK is an over-approximation, not a consumed-header trace',
            'tools':tools,'command':command,'output':{'elf_sha256':digest,'elf_bytes':len(data),'manifest_sha256':file_sha256(stage/'manifest.json')},
            'closure_limits':'compiler subprocesses, system headers/libraries and environment not exhaustively enumerated; no native Runtime, selection or device acceptance',
            'historical_replay':False}
        raw=json.dumps(receipt,sort_keys=True,indent=2)+'\n'
        (stage/('build-provenance-'+hashlib.sha256(raw.encode()).hexdigest()+'.json')).write_text(raw)
        publish_outputs(stage,output)
        return receipt


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    try:output=select_output(ROOT,args.output)
    except ValueError as exc:parser.error(str(exc))
    print(json.dumps(build(ROOT,output),sort_keys=True))

if __name__=='__main__':main()

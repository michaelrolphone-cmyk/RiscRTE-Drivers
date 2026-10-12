#!/usr/bin/env python3
"""Build device-independent compatibility providers, one artifact per capability."""
import argparse,hashlib,json,os,subprocess,sys
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--watch-sdk',required=True,type=Path);p.add_argument('--cc',required=True);p.add_argument('--validator',type=Path,required=True);p.add_argument('--output',type=Path);a=p.parse_args()
r=Path(__file__).resolve().parents[1];w=a.watch_sdk.resolve();out=(a.output or r/'dist/legacy-power').resolve();out.mkdir(parents=True,exist_ok=True)
sys.path.insert(0,str(w/'scripts'));from normalize_xtensa_relocations import normalize
from compact_current_elf import compact
records={}
for name in ['power_sleep','power_key','display_power']:
 source=r/'Drivers'/('legacy_'+name);folder=out/name;folder.mkdir(exist_ok=True);elf=folder/'driver.elf';exports=folder/'exports.map';exports.write_text('{ global: t5_driver_get; local: *; };\n')
 command=[a.cc,'-std=c11','-Os','-fPIC','-mtext-section-literals','-mlongcalls','-fvisibility=hidden','-ffreestanding','-fno-builtin','-nostdlib','-nostartfiles','-shared','-Wl,--no-relax','-Wl,--hash-style=sysv','-Wl,--gc-sections','-ffunction-sections','-fdata-sections','-Wl,--version-script='+str(exports),'-Wall','-Wextra','-Werror','-I'+str(r/'sdk/driver'),'-I'+str(w/'sdk/driver'),str(source/'driver.c'),'-lgcc','-o',str(elf)]
 subprocess.run(command,check=True);normalize(elf);compact(elf,a.cc,debug_path=out/'debug'/(name+'.elf'))
 subprocess.run([str(a.validator.resolve()),str(elf)],check=True)
 symbols=subprocess.check_output([a.cc.removesuffix('gcc')+'nm','-D',str(elf)],text=True)
 imports={x.split()[-1] for x in symbols.splitlines() if ' U ' in ' '+x}
 public={x.split()[-1] for x in symbols.splitlines() if len(x.split())>=3 and x.split()[-2] in ('T','D','R','B')}
 if not imports<={'memset','strcmp'} or public!={'t5_driver_get'}:raise RuntimeError(f'Unexpected ABI: {imports=}, {public=}')
 raw=elf.read_bytes()
 if raw[:7]!=b'\x7fELF\x01\x01\x01' or raw[16:20]!=b'\x03\x00\x5e\x00':raise RuntimeError('Not an ESP32-S3 shared ELF')
 (folder/'manifest.json').write_bytes((source/'manifest.json').read_bytes())
 records[name]={'bytes':len(raw),'sha256':hashlib.sha256(raw).hexdigest(),'imports':sorted(imports),'exports':sorted(public)}
 print(name+': target build and Runtime loader validation PASS')
(out/'build.json').write_text(json.dumps({'schema':1,'hardware_verified':False,'providers':records},indent=2)+'\n')

#!/usr/bin/env python3
"""Build hardware-independent PCM adapters against the deployed audio ABI."""
import argparse,hashlib,json,subprocess,sys
from pathlib import Path
from native_driver_toolchain import compiler
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--watch',type=Path,required=True);p.add_argument('--cc')
p.add_argument('--validator',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
a=p.parse_args();a.cc=compiler(a.cc);root=Path(__file__).resolve().parents[1];watch=a.watch.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
sys.path.insert(0,str(watch/'scripts'))
from compact_current_elf import compact
from normalize_xtensa_relocations import normalize
records={}
for role in ('input','output'):
 source=root/'Drivers'/('pcm_audio_'+role);dest=out/role;dest.mkdir(exist_ok=True)
 elf=dest/'driver.elf';mapping=dest/'exports.map';mapping.write_text('{ global: t5_driver_get; local: *; };\n')
 # Keep the selected complete consumer SDK first; local headers add PCM only.
 command=[a.cc,'-std=c11','-Os','-fPIC','-mtext-section-literals','-mlongcalls','-fvisibility=hidden','-ffreestanding','-fno-builtin','-nostdlib','-nostartfiles','-shared','-Wl,--no-relax','-Wl,--hash-style=sysv','-Wl,--gc-sections','-ffunction-sections','-fdata-sections','-Wl,--version-script='+str(mapping),'-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-I'+str(watch/'sdk/driver'),'-I'+str(root/'sdk/driver'),str(source/'driver.c'),'-lgcc','-o',str(elf)]
 subprocess.run(command,check=True);normalize(elf);compact(elf,a.cc,debug_path=out/'debug'/(role+'.elf'))
 subprocess.run([str(a.validator.resolve()),str(elf)],check=True)
 symbols=subprocess.check_output([a.cc.removesuffix('gcc')+'nm','-D',str(elf)],text=True)
 imports={line.split()[-1] for line in symbols.splitlines() if ' U ' in ' '+line}
 exports={line.split()[-1] for line in symbols.splitlines() if len(line.split())>=3 and line.split()[-2] in ('T','D','R','B')}
 if not imports<={'strcmp','memcpy','memset'} or exports!={'t5_driver_get'}:raise RuntimeError(f'Unexpected ABI: {imports=}, {exports=}')
 raw=elf.read_bytes()
 if raw[:7]!=b'\x7fELF\x01\x01\x01' or raw[16:20]!=b'\x03\x00\x5e\x00':raise RuntimeError('Not a shared Xtensa ELF')
 (dest/'manifest.json').write_bytes((source/'manifest.json').read_bytes())
 records[role]={'sha256':hashlib.sha256(raw).hexdigest(),'bytes':len(raw),'imports':sorted(imports)}
 print(f'{role}: target ELF and loader PASS')
(out/'build.json').write_text(json.dumps({'schema':1,'providers':records,'hardware_verified':False},indent=2)+'\n')

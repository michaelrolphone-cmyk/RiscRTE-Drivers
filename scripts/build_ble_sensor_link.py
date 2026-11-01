#!/usr/bin/env python3
"""Build/repeat the opt-in passive receiver link suffix; no radio or image writes."""
import argparse,hashlib,json,pathlib,shutil,subprocess
from normalize_xtensa_relocations import normalize
from native_driver_toolchain import compiler
R=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=pathlib.Path,required=True);p.add_argument('--compiler');a=p.parse_args()
out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);cc=compiler(a.compiler)
sha=lambda p:hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
flags=['-std=c11','-Os','-fPIC','-ffunction-sections','-fdata-sections','-fvisibility=hidden','-mtext-section-literals','-mlongcalls','-ffreestanding','-fno-builtin','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-I'+str(R/'sdk/driver')]
source=R/'Drivers/ble_sensors/driver.c';manifest=R/'Drivers/ble_sensors/manifest.json';m=json.loads(manifest.read_text())
assert m['id']=='ble-sensors' and m['version']=='0.2.0' and m['driver_abi']==2
commands=[];inputs={str(p.resolve()):sha(p) for p in (pathlib.Path(__file__),R/'scripts/native_driver_toolchain.py',R/'scripts/normalize_xtensa_relocations.py',manifest)}
records=[]
for label in ('package','repeat'):
 d=out/label;d.mkdir(exist_ok=True);mapping=d/'exports.map';mapping.write_text('{ global: t5_driver_get; local: *; };\n')
 dep=d/'source.d';obj=d/'driver.o';cmd=[cc,*flags,'-MMD','-MF',str(dep),'-c',str(source),'-o',str(obj)];commands.append(cmd);subprocess.run(cmd,check=True)
 for name in dep.read_text().replace('\\\n',' ').split(':',1)[1].split():
  f=pathlib.Path(name).resolve();h=sha(f);assert str(f) not in inputs or inputs[str(f)]==h;inputs[str(f)]=h
 elf=d/'driver.elf';cmd=[cc,*flags,'-shared','-nostdlib','-nostartfiles','-Wl,--no-relax','-Wl,--hash-style=sysv','-Wl,--gc-sections','-Wl,--exclude-libs,ALL','-Wl,--version-script='+str(mapping),str(obj),'-lgcc','-o',str(elf)];commands.append(cmd);subprocess.run(cmd,check=True);normalize(elf)
 symbols=subprocess.check_output([cc.removesuffix('gcc')+'nm','-D',str(elf)],text=True)
 imports={l.split()[-1] for l in symbols.splitlines() if ' U ' in ' '+l}
 exports={l.split()[-1] for l in symbols.splitlines() if len(l.split())>=3 and l.split()[-2] in ('T','D','B','R')}
 assert imports <= {'memcpy','memset','memcmp','strlen','strcmp','memchr'} and exports=={'t5_driver_get'},(imports,exports)
 b=elf.read_bytes();assert b[:7]==b'\x7fELF\x01\x01\x01' and b[16:20]==b'\x03\x00\x5e\x00'
 (d/'manifest.json').write_text(json.dumps(m,indent=2)+'\n')
 for notice in (R/'Drivers/ble_sensors').glob('LICENSE*'):shutil.copyfile(notice,d/notice.name)
 records.append({'path':str(elf),'bytes':len(b),'sha256':sha(elf),'imports':sorted(imports),'exports':sorted(exports)})
assert records[0]['sha256']==records[1]['sha256']
assert all(sha(p)==h for p,h in inputs.items())
receipt={'packages':records,'manifest':m,'compiler':{'path':cc,'sha256':sha(pathlib.Path(cc).resolve()),'version':subprocess.check_output([cc,'--version'],text=True).splitlines()[0]},'commands':commands,'inputs':inputs,'scope':'Repeated ESP32-S3 ABI2 ELF, relocation normalization and import/export/structure checks; actual selected Runtime/product admission separate; no hardware'}
(out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps(records),flush=True)

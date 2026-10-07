#!/usr/bin/env python3
"""Build isolated passive-sensor, telemetry, and battery-adapter ABI-v2 ELFs."""
import hashlib,json,os,shutil,subprocess
from pathlib import Path
from normalize_xtensa_relocations import normalize
ROOT=Path(__file__).resolve().parents[1]
PACKAGES={'ble-sensors':'ble_sensors','ble-telemetry':'ble_telemetry','telemetry-battery':'telemetry_battery'}
def build():
 cc=os.environ.get('NATIVE_DRIVER_CC') or shutil.which('xtensa-esp32s3-elf-gcc') or str(Path.home()/'.platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-gcc')
 flags=['-std=c11','-Os','-fPIC','-ffunction-sections','-fdata-sections','-fvisibility=hidden','-mtext-section-literals','-mlongcalls','-ffreestanding','-fno-builtin','-Wall','-Wextra','-Werror','-Wno-misleading-indentation']
 for identity,folder in PACKAGES.items():
  source=ROOT/'Drivers'/folder;out=ROOT/'dist'/identity;out.mkdir(parents=True,exist_ok=True)
  mapping=out/'exports.map';mapping.write_text('{ global: t5_driver_get; local: *; };\n')
  elf=out/'driver.elf'
  subprocess.run([cc,*flags,'-I'+str(ROOT/'sdk/driver'),'-shared','-nostdlib','-nostartfiles','-Wl,--no-relax','-Wl,--hash-style=sysv','-Wl,--gc-sections','-Wl,--exclude-libs,ALL','-Wl,--version-script='+str(mapping),str(source/'driver.c'),'-lgcc','-o',str(elf)],check=True)
  normalize(elf)
  symbols=subprocess.check_output([cc.removesuffix('gcc')+'nm','-D',str(elf)],text=True)
  imports={p.split()[-1] for p in symbols.splitlines() if ' U ' in ' '+p}
  exports={p.split()[-1] for p in symbols.splitlines() if len(p.split())>=3 and p.split()[-2] in ('T','D','B','R')}
  if imports-{'memcpy','memset','memcmp','strlen','strcmp','memchr'} or exports!={'t5_driver_get'}:raise ValueError((identity,imports,exports))
  b=elf.read_bytes()
  if b[:7]!=b'\x7fELF\x01\x01\x01' or b[16:20]!=b'\x03\x00\x5e\x00':raise ValueError('Wrong ELF target')
  m=json.loads((source/'manifest.json').read_text())
  if m['id']!=identity or m['version']!='0.1.0' or m['driver_abi']!=2:raise ValueError('Wrong manifest')
  record={'schema':1,'source_revision':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),'source_dirty':bool(subprocess.check_output(['git','status','--porcelain'],cwd=ROOT,text=True).strip()),'compiler':subprocess.check_output([cc,'--version'],text=True).splitlines()[0],'size_bytes':len(b),'sha256':hashlib.sha256(b).hexdigest(),'imports':sorted(imports),'exports':sorted(exports),'physical_verification':'not-performed'}
  (out/'manifest.json').write_text(json.dumps(m,indent=2)+'\n');(out/'build-record.json').write_text(json.dumps(record,indent=2)+'\n')
  for notice in source.glob('LICENSE*'):shutil.copyfile(notice,out/notice.name)
  print(f"{identity}: {len(b)} bytes {record['sha256']}")
if __name__=='__main__':build()

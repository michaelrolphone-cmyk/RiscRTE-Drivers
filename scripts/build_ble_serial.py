#!/usr/bin/env python3
"""Build BLE central serial over the existing, hash-verified NimBLE/HCI port."""
import argparse,hashlib,json,os,shutil,subprocess
from pathlib import Path
from build_ble_hid import inputs
from normalize_xtensa_relocations import normalize
ROOT=Path(__file__).resolve().parents[1]
def build(host=False,sanitize=False):
 include,src,pin=inputs();profile=ROOT/'Drivers/ble_serial'
 include.insert(0,profile/'port')
 src=[s for s in src if s not in (ROOT/'Drivers/ble_hid/driver.c',ROOT/'Drivers/ble_hid/hid_store.c')]+[profile/'driver.c']
 out=ROOT/('build/ble-serial-san' if sanitize else 'build/ble-serial-host') if host else ROOT/'dist/ble-serial';out.mkdir(parents=True,exist_ok=True)
 cc=os.environ.get('CC','cc') if host else os.environ.get('NATIVE_DRIVER_CC') or shutil.which('xtensa-esp32s3-elf-gcc') or str(Path.home()/'.platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-gcc')
 flags=['-std=c11','-Os','-fPIC','-ffunction-sections','-fdata-sections','-fvisibility=hidden','-D_DEFAULT_SOURCE','-Dmalloc=hid_malloc','-Dcalloc=hid_calloc','-Drealloc=hid_realloc','-Dfree=hid_free']
 flags+=['-DHID_HOST_TEST'] if host else ['-mtext-section-literals','-mlongcalls','-ffreestanding','-fno-builtin','-fstack-usage']
 if sanitize:flags+=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer']
 objs=[]
 for s in src:
  obj=out/(str(s.relative_to(ROOT)).replace('/','_')+'.o');objs.append(obj)
  warnings=['-w'] if s.is_relative_to(ROOT/'vendor') else ['-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-parameter']
  subprocess.run([cc,*flags,*warnings,*['-I'+str(p) for p in include],'-c',str(s),'-o',str(obj)],check=True)
 mapping=out/'exports.map';mapping.write_text('{ global: t5_driver_get; local: *; };\n')
 elf=out/('driver.so' if host else 'driver.elf');link=[] if host else ['-nostdlib','-nostartfiles','-Wl,--no-relax','-Wl,--hash-style=sysv','-Wl,--exclude-libs,ALL']
 subprocess.run([cc,*flags,'-shared',*link,'-Wl,--gc-sections','-Wl,--version-script='+str(mapping),*map(str,objs),'-lgcc','-o',str(elf)],check=True)
 if host:return elf
 normalize(elf);symbols=subprocess.check_output([cc.removesuffix('gcc')+'nm','-D',str(elf)],text=True)
 imports={p.split()[-1] for p in symbols.splitlines() if ' U ' in ' '+p};exports={p.split()[-1] for p in symbols.splitlines() if len(p.split())>=3 and p.split()[-2] in ('T','D','B','R')}
 allowed={'memcpy','memset','memcmp','memmove','strlen','strcmp','strncat'}
 if imports-allowed or exports!={'t5_driver_get'}:raise ValueError((imports,exports))
 raw=elf.read_bytes();assert raw[:7]==b'\x7fELF\x01\x01\x01' and raw[16:20]==b'\x03\x00\x5e\x00'
 shutil.copyfile(profile/'manifest.json',out/'manifest.json')
 record={'schema':1,'id':'ble-serial','version':'0.1.1','source_revision':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),'source_dirty':bool(subprocess.check_output(['git','status','--porcelain'],cwd=ROOT,text=True).strip()),'nimble_commit':pin['commit'],'bytes':len(raw),'sha256':hashlib.sha256(raw).hexdigest(),'imports':sorted(imports),'exports':sorted(exports),'hardware_tested':False}
 (out/'build.json').write_text(json.dumps(record,indent=2)+'\n')
 for f in ['LICENSE','NOTICE','SOURCE.json']:shutil.copyfile(ROOT/'vendor/nimble'/f,out/('NimBLE-'+f))
 print(json.dumps(record));return elf
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--host',action='store_true');p.add_argument('--sanitize',action='store_true');a=p.parse_args();build(a.host,a.sanitize)

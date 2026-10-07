#!/usr/bin/env python3
"""Build the pinned NimBLE peripheral and validate its isolated ELF contract."""
import argparse,hashlib,json,os,shutil,subprocess
from pathlib import Path
from normalize_xtensa_relocations import normalize
ROOT=Path(__file__).resolve().parents[1]
VENDOR=ROOT/'vendor/nimble'
SOURCE=ROOT/'Drivers/ble_hid'

def inputs():
    pin=json.loads((VENDOR/'SOURCE.json').read_text())
    actual={str(p.relative_to(VENDOR)) for p in VENDOR.rglob('*') if p.is_file() and p.name!='SOURCE.json'}
    if actual!=set(pin['files']):raise ValueError('Vendored source inventory differs')
    for p,h in pin['files'].items():
        if hashlib.sha256((VENDOR/p).read_bytes()).hexdigest()!=pin.get('patches',{}).get(p,{}).get('sha256',h):raise ValueError('Modified upstream source: '+p)
    include=[SOURCE/'port',ROOT/'sdk/driver']+sorted(p for p in VENDOR.rglob('include') if p.is_dir())
    src=[]
    for p in ('nimble/host/src','nimble/host/util/src','nimble/src','ext/tinycrypt/src','nimble/host/services/gap/src','nimble/host/services/gatt/src'):
        src+=sorted((VENDOR/p).glob('*.c'))
    # Optional console introspection isn't part of the host/protocol path.
    src=[s for s in src if s.name!='ble_gatts_lcl.c']
    src += [VENDOR/'nimble/transport/src/transport.c']
    src += [VENDOR/'porting/nimble/src'/p for p in ('os_msys.c','os_mempool.c','os_mbuf.c','mem.c','endian.c','nimble_port.c')]
    src += [SOURCE/'port/ble_port.c',SOURCE/'hid_store.c',SOURCE/'driver.c']
    return include,src,pin

def build(host=False,sanitize=False):
    include,src,pin=inputs();out=ROOT/('build/ble-hid-san' if sanitize else 'build/ble-hid-host') if host else ROOT/'dist/ble-hid'
    out.mkdir(parents=True,exist_ok=True)
    cc=os.environ.get('CC','cc') if host else os.environ.get('NATIVE_DRIVER_CC') or shutil.which('xtensa-esp32s3-elf-gcc') or str(Path.home()/'.platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-gcc')
    flags=['-std=c11','-Os','-fPIC','-ffunction-sections','-fdata-sections','-fvisibility=hidden','-D_DEFAULT_SOURCE','-Dmalloc=hid_malloc','-Dcalloc=hid_calloc','-Drealloc=hid_realloc','-Dfree=hid_free']
    if host:flags+=['-DHID_HOST_TEST']
    else:flags+=['-mtext-section-literals','-mlongcalls','-ffreestanding','-fno-builtin']
    if sanitize:flags+=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer']
    objs=[]
    for s in src:
        obj=out/(str(s.relative_to(ROOT)).replace('/','_')+'.o');objs.append(obj)
        warnings=['-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-parameter'] if not s.is_relative_to(VENDOR) else ['-w']
        subprocess.run([cc,*flags,*warnings,*['-I'+str(p) for p in include],'-c',str(s),'-o',str(obj)],check=True)
    mapping=out/'exports.map';mapping.write_text('{ global: t5_driver_get; local: *; };\n')
    elf=out/('driver.so' if host else 'driver.elf')
    link=[] if host else ['-nostdlib','-nostartfiles','-Wl,--no-relax','-Wl,--hash-style=sysv','-Wl,--exclude-libs,ALL']
    subprocess.run([cc,*flags,'-shared',*link,'-Wl,--gc-sections','-Wl,--version-script='+str(mapping),*map(str,objs),'-lgcc','-o',str(elf)],check=True)
    if host:return elf
    normalize(elf)
    nm=cc.removesuffix('gcc')+'nm'
    symbols=subprocess.check_output([nm,'-D',str(elf)],text=True)
    imports={p.split()[-1] for p in symbols.splitlines() if ' U ' in ' '+p}
    exports={p.split()[-1] for p in symbols.splitlines() if len(p.split())>=3 and p.split()[-2] in ('T','D','B','R')}
    allowed={'memcpy','memset','memcmp','memmove','strlen','strcmp','strncat'}
    if imports-allowed or exports!={'t5_driver_get'}:raise ValueError((imports-allowed,exports))
    b=elf.read_bytes()
    if b[:7]!=b'\x7fELF\x01\x01\x01' or b[16:20]!=b'\x03\x00\x5e\x00':raise ValueError('Wrong ELF target')
    m=json.loads((SOURCE/'manifest.json').read_text())
    if m['id']!='ble-hid' or m['version']!='0.1.1':raise ValueError('Wrong manifest')
    record={'schema':1,'source_revision':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),'source_dirty':bool(subprocess.check_output(['git','status','--porcelain'],cwd=ROOT,text=True).strip()),'nimble_commit':pin['commit'],'compiler':subprocess.check_output([cc,'--version'],text=True).splitlines()[0],'size_bytes':len(b),'sha256':hashlib.sha256(b).hexdigest(),'imports':sorted(imports),'exports':sorted(exports),'physical_verification':'not-performed'}
    (out/'build-record.json').write_text(json.dumps(record,indent=2)+'\n');(out/'manifest.json').write_text(json.dumps(m,indent=2)+'\n')
    for f in ('LICENSE','NOTICE','SOURCE.json'):shutil.copyfile(VENDOR/f,out/('NimBLE-'+f))
    shutil.copyfile(VENDOR/'ext/tinycrypt/LICENSE',out/'TinyCrypt-LICENSE')
    print(f"BLE HID target ELF: {len(b)} bytes, {record['sha256']}");return elf
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--host',action='store_true');p.add_argument('--sanitize',action='store_true');a=p.parse_args();build(a.host,a.sanitize)

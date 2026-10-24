#!/usr/bin/env python3
"""Run real Runtime/CpuPort graph admission and fail-closed mutation checks."""
import argparse
import copy
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
LOCK=ROOT/'boards/waveshare-esp32-s3-touch-amoled-1.75c/home/sources.lock.json'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def files(root):
    return {p.relative_to(root).as_posix(): {'sha256':digest(p),'bytes':p.stat().st_size}
            for p in sorted(root.rglob('*')) if p.is_file()}


def pinned(root, revision, paths):
    def git(*args):return subprocess.check_output(['git','-C',str(root),*args])
    if git('rev-parse','HEAD').decode().strip()!=revision:raise ValueError('Wrong source pin: '+str(root))
    if git('status','--porcelain','--untracked-files=all','--',*paths):raise ValueError('Dirty source inputs: '+str(root))
    result={}
    for entry in git('ls-tree','-rz',revision,'--',*paths).split(b'\0'):
        if not entry:continue
        metadata,name=entry.split(b'\t',1);mode,kind,oid=metadata.split();path=root/name.decode()
        if kind!=b'blob' or mode not in (b'100644',b'100755') or path.is_symlink():raise ValueError('Unbound input: '+str(path))
        raw=path.read_bytes()
        if hashlib.sha1(b'blob '+str(len(raw)).encode()+b'\0'+raw).hexdigest()!=oid.decode():raise ValueError('Changed pinned bytes: '+str(path))
        result[name.decode()]=hashlib.sha256(raw).hexdigest()
    return result


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('runtime','controllers','store','output'):
        p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--native-elf',type=Path)
    p.add_argument('--graph-only',action='store_true',help='Explicitly omit ELF import/link qualification')
    p.add_argument('--sanitize',action='store_true')
    a=p.parse_args()
    if bool(a.native_elf)==bool(a.graph_only):p.error('Supply --native-elf or explicitly select --graph-only')
    N=a.runtime.resolve();T=a.controllers.resolve();out=a.output.resolve();store=a.store.resolve()
    out.mkdir(parents=True,exist_ok=True)
    (out/'admission.json').unlink(missing_ok=True)
    lock=json.loads(LOCK.read_text())
    dependency_paths={'runtime':['src','sdk','lib/ArduinoJson','lib/elf_loader','test'],
                      'controllers':['scripts/verify_update_elf.py']}
    roots={'runtime':N,'controllers':T}
    verified={n:pinned(root,lock[n]['commit'],dependency_paths[n]) for n,root in roots.items()}
    store_before=files(store)
    receipt=json.loads((store.parent/'build.json').read_text())
    if store_before!=receipt['store_files']:raise ValueError('Store differs from its build receipt')
    for name,expected in receipt['source_sha256'].items():
        if digest(ROOT/name)!=expected:raise ValueError('Composition source changed since target build: '+name)
    local_sources={str(p):digest(p) for p in [LOCK,Path(__file__).resolve(),ROOT/'tests/badge_home_admission.cpp']}
    native_digest=digest(a.native_elf) if a.native_elf else None
    incs=[N/x for x in ['src','sdk/app','sdk/driver','sdk/hardware','lib/ArduinoJson/src',
        'test','test/drivers/stubs','lib/elf_loader/include','test/native_bank_stubs']]+[out]
    flags=['-std=c++17','-Wall','-Wextra','-Werror','-Wno-missing-field-initializers',
           '-Wno-misleading-indentation','-rdynamic','-DRISC_APP_REQUIREMENT_ROWS=24']
    if a.native_elf:
        sys.path.insert(0,str(T/'scripts'))
        from verify_update_elf import cohort_admission_header
        header=cohort_admission_header(N,a.native_elf.read_bytes())
        # The pinned reuse helper predates Runtime's namespaced clock boundary.
        # Adapt only its host clock double, leaving extracted production
        # allowedImport/admitElf and exact linked export inventory untouched.
        clock='static uint32_t millis(){return ticks;}'
        if header.count(clock)!=1 or 'RiscPlatform::millis()' not in header:
            raise ValueError('Unexpected native admission clock boundary')
        header=header.replace(clock,'namespace RiscPlatform { '+clock+' }')
        (out/'cohort_elf_admission.h').write_text('#pragma once\n'+header)
        flags+=['-DBADGE_IMAGE_ADMISSION=1']
    san=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-no-pie'] if a.sanitize else []
    include=['-I'+str(i) for i in incs]
    sources=[N/x for x in ['src/bootstrap/Json.cpp','src/bootstrap/Board.cpp','src/bootstrap/Runtime.cpp',
        'src/runtime/drivers/ProviderGraphV2.cpp','src/runtime/drivers/ProviderModuleV2.cpp',
        'src/ports/esp32s3/CpuPort.cpp','src/runtime/streams/AppStreamSessions.cpp',
        'src/runtime/streams/ProviderQueueHost.cpp']]
    commands=[]
    def run(cmd,**kw):
        commands.append(list(map(str,cmd)))
        return subprocess.run(list(map(str,cmd)),**kw)
    run([os.environ.get('CC','cc'),'-std=c11',*san,*include,'-c',N/'lib/elf_loader/src/esp_elf_validate.c',
         '-o',out/'validate.o'],check=True)
    run([os.environ.get('CXX','c++'),*flags,*san,*include,*sources,ROOT/'tests/badge_home_admission.cpp',
         out/'validate.o','-ldl','-o',out/'admit'],check=True)
    def admit(directory):
        r=run([out/'admit',directory],text=True,capture_output=True,
              env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'})
        if not r.stdout.strip():raise RuntimeError(r.stderr)
        result=json.loads(r.stdout.splitlines()[-1])
        if result['hardware_calls']:raise ValueError('Admission performed hardware I/O')
        return r.returncode,result
    rc,positive=admit(store)
    if rc:raise ValueError('Real Runtime refused composed store: '+json.dumps(positive))
    refusals={}
    with tempfile.TemporaryDirectory(prefix='badge-negative-') as tmp:
        damaged=Path(tmp)/'store'
        def edit(path,fn):
            f=damaged/path;data=json.loads(f.read_text());fn(data);f.write_text(json.dumps(data))
        def append_grant(cap):
            edit('boot.json',lambda b:b['app_capabilities'][0]['grants'].append({'capability':cap,'api':1,'instance_id':0}))
        def cross_bus(b):
            bus=copy.deepcopy(b['buses'][0]);bus.update(instance_id=102,controller=1,physical_controller=1)
            bus['pins']={'sda':16,'scl':17};b['buses'].append(bus)
            next(d for d in b['devices'] if d['instance_id']==6)['config']['bus_instance_id']=102
        cases={
            'missing-supply':lambda:(damaged/'supply/manifest.json').unlink(),
            'missing-touch-hardware':lambda:edit('touch/manifest.json',lambda m:m.update(requires=[d for d in m['requires'] if d['capability']!='hardware.device'])),
            'global-touch-workaround':lambda:edit('boot.json',lambda b:next(d for d in b['drivers'] if d['instance_id']==6).pop('instance_id')),
            'cross-bus-touch':lambda:edit('board.json',cross_bus),
            'missing-i2c-binding':lambda:edit('board.json',lambda b:next(d for d in b['devices'] if d['instance_id']==6)['bindings'].pop('i2c.bus')),
            'duplicate-hardware-owner':lambda:edit('boot.json',lambda b:b['drivers'].append(copy.deepcopy(b['drivers'][0]))),
            'display-touch-reset-collision':lambda:edit('board.json',lambda b:next(d for d in b['devices'] if d['instance_id']==6)['config'].update(reset=1)),
            'duplicate-touch-compatibility':lambda:edit('touch/manifest.json',lambda m:m['hardware_compatibility'].append(copy.deepcopy(m['hardware_compatibility'][0]))),
            'unsupported-battery-grant':lambda:append_grant('board.battery'),
            'unsupported-rtc-required':lambda:edit('default.json',lambda m:(m['requires'].extend(d for d in m['optional'] if d['capability']=='rtc.clock'),m.update(optional=[d for d in m['optional'] if d['capability']!='rtc.clock']))),
            'missing-time-authority':lambda:edit('boot.json',lambda b:b['app_capabilities'][0].update(grants=[g for g in b['app_capabilities'][0]['grants'] if g['capability']!='runtime.realtime-control'])),
            'missing-promotion-authority':lambda:edit('boot.json',lambda b:b['app_capabilities'][0].update(grants=[g for g in b['app_capabilities'][0]['grants'] if g['capability']!='runtime.provider-promotion'])),
            'raw-platform-app-grant':lambda:(edit('default.json',lambda m:m['requires'].append({'capability':'platform.gpio','api':1})),append_grant('platform.gpio')),
        }
        if a.native_elf:
            cases['invalid-home-elf']=lambda:(damaged/'default.elf').write_bytes(b'not an ELF')
            cases['missing-touch-elf']=lambda:(damaged/'touch/driver.elf').unlink()
        for name,mutate in cases.items():
            if damaged.exists():shutil.rmtree(damaged)
            shutil.copytree(store,damaged);mutate()
            code,result=admit(damaged)
            if code!=4:raise ValueError(f'Invalid store accepted: {name}: {result}')
            refusals[name]=result
    for n,root in roots.items():
        if verified[n]!=pinned(root,lock[n]['commit'],dependency_paths[n]):raise ValueError('Pinned inputs changed during admission')
    if store_before!=files(store):raise ValueError('Store changed during admission')
    if any(digest(Path(p))!=expected for p,expected in local_sources.items()):raise ValueError('Admission harness changed during run')
    if a.native_elf and native_digest!=digest(a.native_elf):raise ValueError('Native ELF changed during admission')
    result={'schema':1,'verified_dependencies':verified,'source_sha256':local_sources,'store_files':store_before,'real_runtime_graph_admission':True,'target_images_checked':bool(a.native_elf),
            'native_elf_sha256':native_digest,
            'hardware_io_calls':0,'positive':positive,'negative_cases':refusals,'commands':commands,
            'sanitize':a.sanitize,'leak_sanitizer':False,'physical_verified':False}
    (out/'admission.json').write_text(json.dumps(result,indent=2)+'\n')
    print(f'Real Runtime admission and {len(refusals)} negative cases PASS; native image checks={bool(a.native_elf)}')


if __name__=='__main__':main()

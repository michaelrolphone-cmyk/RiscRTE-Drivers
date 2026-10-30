#!/usr/bin/env python3
"""Actual panel lease/lifecycle/PCA/scoped-I2C/current Runtime graph composition; no physical proof."""
import argparse,hashlib,json,os,resource,shlex,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--runtime',required=True,type=Path)
 p.add_argument('--dependency-drivers',type=Path,default=ROOT,help='Explicit shared source root; default this repository')
 p.add_argument('--pca-source',type=Path,default=ROOT,help='Explicit qualified PCA source root; default this repository')
 p.add_argument('--output',required=True,type=Path)
 p.add_argument('--sanitize',action='store_true')
 a=p.parse_args();resource.setrlimit(resource.RLIMIT_CORE,(0,0));runtime=a.runtime.resolve();drivers=a.dependency_drivers.resolve();pca=a.pca_source.resolve()
 inputs={};source_roots=[('candidate',ROOT),('runtime',runtime),('drivers',pca),('drivers',drivers)]
 with tempfile.TemporaryDirectory(prefix='panel-composition-') as td:
  build=Path(td);sdk=build/'sdk';sdk.mkdir();origins={}
  for directory in [drivers/'sdk/driver',runtime/'sdk/driver',runtime/'sdk/hardware']:
   for header in directory.glob('*.h'):origins[header.name]=header
  origins['RiscGpioExpanderV1.h']=pca/'sdk/driver/RiscGpioExpanderV1.h'
  for name in ['RiscPanelLeaseV1.h','RiscTps65185ProfileV1.h']:origins[name]=ROOT/'sdk/driver'/name
  for name,src in origins.items():(sdk/name).symlink_to(src)
  flags=['-Wall','-Wextra','-Werror','-Wno-missing-field-initializers']
  if a.sanitize:flags+=['-fsanitize=undefined','-fno-sanitize-recover=all']
  includes=['-I'+str(x) for x in [sdk,runtime/'src',runtime/'test/drivers/stubs']]
  cc=os.environ.get('CC','cc');cxx=os.environ.get('CXX','c++')
  cprefix=[cc,'-std=c11',*flags,*includes,'-fPIC','-fvisibility=hidden']
  cppprefix=[cxx,'-std=c++17',*flags,*includes]
  c_units=[(ROOT/'Drivers/tps65185_power/driver.c','tps'),(ROOT/'Drivers/t5s3_panel_profile/driver.c','profile'),(ROOT/'Drivers/panel_lease_lifecycle/driver.c','lifecycle'),(pca/'Drivers/pca9535_gpio/driver.c','pca'),(drivers/'components/i2c-scoped/driver.c','i2c')]
  units=[(cprefix,src) for src,name in c_units]
  cpp=[runtime/'src/runtime/drivers/ProviderModuleV2.cpp',runtime/'src/runtime/drivers/ProviderGraphV2.cpp',ROOT/'test/panel_lease_composition.cpp']
  units += [(cppprefix,x) for x in cpp]
  for prefix,src in units:
   raw=subprocess.check_output([*prefix,'-MM','-MT','dependencies',str(src)],text=True)
   for name in shlex.split(raw.split(':',1)[1].replace('\\\n',' ')):
    path=Path(name).resolve();key=None
    candidate_paths=[x for x,name in c_units[:3]]+[ROOT/'sdk/driver/RiscPanelLeaseV1.h',ROOT/'sdk/driver/RiscTps65185ProfileV1.h',ROOT/'test/panel_lease_composition.cpp']
    if path in candidate_paths:key='candidate/'+str(path.relative_to(ROOT))
    for label,root in source_roots[1:]:
     if key is None and path.is_relative_to(root):key=label+'/'+str(path.relative_to(root));break
    if key is None:raise ValueError('Undeclared non-system dependency: '+str(path))
    inputs[key]=sha(path)
  locked=json.loads((ROOT/'qualification/panel-lease-source.json').read_text())['dependency_inputs']
  observed={k:v for k,v in inputs.items() if not k.startswith('candidate/')}
  if observed!={k:v['sha256'] for k,v in locked.items()}:raise ValueError('Current source closure differs from pinned dependency inputs')
  for src,name in c_units:subprocess.run([*cprefix,'-shared',str(src),'-o',str(build/(name+'.so'))],check=True)
  subprocess.run([*cppprefix,*map(str,cpp),'-ldl','-o',str(build/'test')],check=True)
  cases=[]
  for mode in ['admission','normal','eager','partial-start','create-token','external-timeout','internal-timeout','stalled-clock','tps-write','release-wait','release-unlock','release-stalled','release-pca-write','sleep-wait','sleep-rollback','prepared-unload']:
   result=subprocess.run([str(build/'test'),str(build),mode],capture_output=True,text=True)
   quarantine=mode in ['partial-start','tps-write','prepared-unload','release-pca-write']
   expected=-6 if quarantine else 0
   marker='Panel retained-chain refusal PASS' if quarantine else 'PASS'
   if result.returncode!=expected or marker not in result.stdout:raise RuntimeError(f'{mode}: {result.returncode}\n{result.stdout}{result.stderr}')
   cases.append({'case':mode,'passed':True,'returncode':result.returncode,'stdout':result.stdout.strip()})
  receipt={'hardware_tested':False,'sanitizer':'undefined' if a.sanitize else 'none','translation_units':8,'inputs':dict(sorted(inputs.items())),'cases':cases,'compilers':{name:subprocess.check_output([cmd,'--version'],text=True).splitlines()[0] for name,cmd in [('c',cc),('c++',cxx)]}}
  a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(receipt,indent=2)+'\n')
  print(f'{len(cases)} actual provider/I2C/graph scenarios PASS; {len(inputs)} declared source inputs; physical behavior untested')
if __name__=='__main__':main()

#!/usr/bin/env python3
"""Actual PCA/scoped-I2C/current Runtime graph composition; no physical proof."""
import argparse,hashlib,json,os,resource,shlex,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--runtime',required=True,type=Path)
 p.add_argument('--dependency-drivers',type=Path,default=ROOT,help='Explicit shared source root; default this repository')
 p.add_argument('--output',required=True,type=Path)
 p.add_argument('--sanitize',action='store_true')
 a=p.parse_args();resource.setrlimit(resource.RLIMIT_CORE,(0,0));runtime=a.runtime.resolve();drivers=a.dependency_drivers.resolve()
 inputs={};source_roots={'candidate':ROOT,'drivers':drivers,'runtime':runtime}
 with tempfile.TemporaryDirectory(prefix='pca-composition-') as td:
  build=Path(td);sdk=build/'sdk';sdk.mkdir();origins={}
  for directory in [drivers/'sdk/driver',runtime/'sdk/driver',runtime/'sdk/hardware']:
   for header in directory.glob('*.h'):origins[header.name]=header
  origins['RiscGpioExpanderV1.h']=ROOT/'sdk/driver/RiscGpioExpanderV1.h'
  for name,src in origins.items():(sdk/name).symlink_to(src)
  flags=['-Wall','-Wextra','-Werror','-Wno-missing-field-initializers']
  if a.sanitize:flags+=['-fsanitize=undefined','-fno-sanitize-recover=all']
  includes=['-I'+str(x) for x in [sdk,runtime/'src',runtime/'test/drivers/stubs']]
  cc=os.environ.get('CC','cc');cxx=os.environ.get('CXX','c++')
  cprefix=[cc,'-std=c11',*flags,*includes,'-fPIC','-fvisibility=hidden']
  cppprefix=[cxx,'-std=c++17',*flags,*includes]
  units=[(cprefix,ROOT/'Drivers/pca9535_gpio/driver.c'),(cprefix,drivers/'components/i2c-scoped/driver.c')]
  cpp=[runtime/'src/runtime/drivers/ProviderModuleV2.cpp',runtime/'src/runtime/drivers/ProviderGraphV2.cpp',ROOT/'test/pca9535_composition.cpp']
  units += [(cppprefix,x) for x in cpp]
  for prefix,src in units:
   raw=subprocess.check_output([*prefix,'-MM','-MT','dependencies',str(src)],text=True)
   for name in shlex.split(raw.split(':',1)[1].replace('\\\n',' ')):
    path=Path(name).resolve();key=None
    candidate_paths=[ROOT/'Drivers/pca9535_gpio/driver.c',ROOT/'sdk/driver/RiscGpioExpanderV1.h',ROOT/'test/pca9535_composition.cpp']
    if path in candidate_paths:key='candidate/'+str(path.relative_to(ROOT))
    for label,root in sorted(source_roots.items(),key=lambda item:0 if item[0]=='runtime' else 1 if item[0]=='drivers' else 2):
     if key is None and path.is_relative_to(root):key=label+'/'+str(path.relative_to(root));break
    if key is None:raise ValueError('Undeclared non-system dependency: '+str(path))
    inputs[key]=sha(path)
  locked=json.loads((ROOT/'Drivers/pca9535_gpio/source-map.json').read_text())['dependency_inputs']
  observed={k:v for k,v in inputs.items() if not k.startswith('candidate/')}
  if observed!={k:v['sha256'] for k,v in locked.items()}:raise ValueError('Current source closure differs from pinned dependency inputs')
  for (_,src),name in zip(units[:2],['pca','i2c']):subprocess.run([*cprefix,'-shared',str(src),'-o',str(build/(name+'.so'))],check=True)
  subprocess.run([*cppprefix,*map(str,cpp),'-ldl','-o',str(build/'test')],check=True)
  cases=[]
  for mode in ['admission','wrong-bus','same-address','normal','address-20','address-27','partial','write','release-write','read-failure','release-read','unlock','destroy','close','create-token','start-read']:
   result=subprocess.run([str(build/'test'),str(build/'pca.so'),str(build/'i2c.so'),mode],capture_output=True,text=True)
   quarantine=mode in ['partial','write','release-write']
   expected=-6 if quarantine else 0
   marker='PCA uncertain-write custody PASS' if quarantine else 'PASS'
   if result.returncode!=expected or marker not in result.stdout:raise RuntimeError(f'{mode}: {result.returncode}\n{result.stdout}{result.stderr}')
   cases.append({'case':mode,'passed':True,'returncode':result.returncode,'stdout':result.stdout.strip()})
  receipt={'hardware_tested':False,'sanitizer':'undefined' if a.sanitize else 'none','translation_units':5,'inputs':dict(sorted(inputs.items())),'cases':cases,'compilers':{name:subprocess.check_output([cmd,'--version'],text=True).splitlines()[0] for name,cmd in [('c',cc),('c++',cxx)]}}
  a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(receipt,indent=2)+'\n')
  print(f'{len(cases)} actual provider/I2C/graph scenarios PASS; {len(inputs)} declared source inputs; physical behavior untested')
if __name__=='__main__':main()

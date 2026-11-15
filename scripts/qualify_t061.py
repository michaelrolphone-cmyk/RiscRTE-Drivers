#!/usr/bin/env python3
"""T061 source-closed host/target qualification; no device or publication actions."""
import argparse,hashlib,json,os,pathlib,shlex,subprocess,sys,tarfile,shutil
R=pathlib.Path(__file__).resolve().parents[1]
LANE='receiver'
PINS={'runtime': '6a3b3d46f5b2857eacc6e9863799dec1f466b036'}
def digest(data):return hashlib.sha256(data).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--dependencies',type=pathlib.Path,required=True);p.add_argument('--compiler',type=pathlib.Path,required=True);p.add_argument('--output',type=pathlib.Path,required=True);p.add_argument('--plan-only',action='store_true');a=p.parse_args()
 out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);deps={n:(a.dependencies/n).resolve() for n in PINS};cc=a.compiler.resolve();records=[]
 for n,pin in PINS.items():assert subprocess.check_output(['git','-C',str(deps[n]),'rev-parse','HEAD'],text=True).strip()==pin,n
 assert '8.4.0' in subprocess.check_output([str(cc),'--version'],text=True)
 def save(): (out/'runs.json').write_text(json.dumps(records,indent=2)+'\n')
 def run(label,cmd,env=None):
  cmd=list(map(str,cmd));row={'label':label,'command':cmd,'environment':env or {}};records.append(row);save()
  if a.plan_only:return
  with (out/(label+'.log')).open('w') as log:
   result=subprocess.run(cmd,cwd=R,stdout=log,stderr=subprocess.STDOUT,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1',**(env or {})))
  row['exit']=result.returncode;save();result.check_returncode()
 def py(label,script,*args):
  try:run(label,[sys.executable,R/'scripts'/script,*args])
  finally:
   artifact=R/'build'/label.replace('_','-')
   if not a.plan_only and artifact.is_dir():shutil.copytree(artifact,out/('artifacts-'+label),dirs_exist_ok=True)
 # Source archives are explicit Git source paths, never environment/auth stores or product output.
 sources={}
 for label,repo in {'owner':R,**deps}.items():
  names=subprocess.check_output(['git','-C',str(repo),'ls-files','--cached','--others','--exclude-standard','-z']).decode().split('\0');files={}
  with tarfile.open(out/(label+'-source.tar.gz'),'w:gz') as tar:
   for name in sorted(set(names)):
    if not name or name.split('/')[0] in {'.source','build','dist','qualification','.git'}:continue
    if label=='reader' and name.split('/')[0] not in {'sdk','scripts'}:continue
    path=repo/name
    if not path.is_file() or path.is_symlink():continue
    b=path.read_bytes();files[name]={'sha256':digest(b),'git_blob':hashlib.sha1(b'blob '+str(len(b)).encode()+b'\0'+b).hexdigest()};tar.add(path,arcname=name,recursive=False)
  sources[label]={'head':subprocess.check_output(['git','-C',str(repo),'rev-parse','HEAD'],text=True).strip(),'files':files}
 (out/'source-manifest.json').write_text(json.dumps(sources,indent=2)+'\n')
 runtime=deps['runtime']
 if LANE=='home':
  common=['--runtime',runtime,'--x4',deps['x4'],'--sdk',deps['reader']/'sdk/driver','--utilities',deps['utilities']]
  for label,extra in [('original',[]),('remote',['--remote-battery'])]:py('home-'+label,'test_home_provision.py',*common,'--output',out/('home-'+label),*extra)
  tree={'sha':PINS['runtime'],'truncated':False,'tree':[]}
  for line in subprocess.check_output(['git','-C',str(runtime),'ls-tree','-r','HEAD'],text=True).splitlines():
   meta,name=line.split('\t',1);mode,kind,sha=meta.split();tree['tree'].append({'path':name,'type':kind,'sha':sha})
  (out/'runtime-tree.json').write_text(json.dumps(tree))
  for mode,extra in [('normal',[]),('sanitized',['--sanitize'])]:
   py('persistence-'+mode,'test_remote_battery_persistence.py','--runtime',runtime,'--drivers',deps['drivers'],'--output',out/('persistence-'+mode),*extra)
   py('manifest-'+mode,'test_quick_remote_manifest.py','--runtime-repo',runtime,'--runtime-tree',out/'runtime-tree.json','--output',out/('manifest-'+mode),*extra)
  py('quick-adapter','test_quick_remote_adapter.py','--runtime',runtime,'--drivers',deps['drivers'],'--utilities',deps['utilities'],'--x4',deps['x4'],'--output',out/'quick-adapter')
  py('sparse','test_quick_remote_sparse.py','--sdk',out/'quick-adapter/include','--output',out/'sparse')
  for name in ['quick_remote_battery_build','remote_battery','remote_battery_action_contract','quick_remote_controls','quick_actions','quick_radios','quick_usb_transfer']:py(name,'test_'+name+'.py')
  build=[*common,'--reader-scripts',deps['reader']/'scripts','--compiler',cc]
  for label,extra in [('target-original',[]),('target',['--remote-battery-version','0.4.21']),('repeat',['--remote-battery-version','0.4.21'])]:py(label,'build_home_provision.py',*build,'--output',out/label,*extra)
  for name in ['quick_atomic','home_automatic','home_settings_availability']:
   for mode,extra in [('normal',[]),('sanitized',['--sanitize'])]:py(name+'-'+mode,'test_'+name+'.py','--target',out/'target/home','--output',out/(name+'-'+mode),*extra)
  targets=[out/'target/home/default.elf',out/'repeat/home/default.elf']
  py('target-closure','qualify_t061_target_closure.py','--target',out/'target','--output',out/'target-closure')
 else:
  for mode,extra in [('normal',[]),('sanitized',['--sanitize'])]:
   py('receiver-'+mode,'test_ble_sensor_link.py','--output',out/('receiver-'+mode),*extra)
   run('receiver-original-'+mode,['bash',R/'test/run_ble_sensor_test.sh'],{'SANITIZE':'1' if extra else '0','BLE_SENSOR_TEST_OUTPUT':str(out/('receiver-original-'+mode))})
  py('target','build_ble_sensor_link.py','--compiler',cc,'--output',out/'target')
  targets=[out/'target/package/driver.elf',out/'target/repeat/driver.elf']
 validator=out/'selected-validator'
 run('compile-validator',['cc','-std=c11','-Wall','-Wextra','-Werror','-I'+str(runtime/'test/native_apps/stubs'),'-I'+str(runtime/'lib/elf_loader/include'),runtime/'lib/elf_loader/src/esp_elf_validate.c',runtime/'test/native_apps/validate_test.c','-o',validator])
 for i,target in enumerate(targets):run('validate-'+str(i),[validator,target])
 if not a.plan_only:
  assert targets[0].read_bytes()==targets[1].read_bytes()
  for label,repo in {'owner':R,**deps}.items():
   for name,hashes in sources[label]['files'].items():assert digest((repo/name).read_bytes())==hashes['sha256'],(label,name)
  (out/'receipt.json').write_text(json.dumps({'lane':LANE,'pins':PINS,'compiler_archive_sha256':'8aa17a6adf01efa5b1628c8ac578063a44d26ae9581d39486b92223a41ef262f','compiler_sha256':digest(cc.read_bytes()),'repeat_identical':True,'target_sha256':digest(targets[0].read_bytes()),'scope':'Host doubles and target structural qualification; no device execution'},indent=2)+'\n')
if __name__=='__main__':main()

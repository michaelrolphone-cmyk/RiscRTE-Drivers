#!/usr/bin/env python3
"""Re-execute a hash-preserved real Runtime graph validator; no native ELF proof.

A bounded local reuse path when source, compiler selection and target image count
are unchanged. Never rebuilds a different Runtime or upgrades prior evidence.
"""
import argparse,gzip,hashlib,json,os,shutil,subprocess,tempfile
from pathlib import Path
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def require(ok,why):
 if not ok:raise ValueError(why)
def inventory(root):return {str(p.relative_to(root)):{'sha256':sha(p),'bytes':p.stat().st_size} for p in root.rglob('*') if p.is_file()}
def main():
 p=argparse.ArgumentParser(description=__doc__)
 for n in ('reference','binary-map','runtime','controllers','store','output'):p.add_argument('--'+n,type=Path,required=True)
 a=p.parse_args();ref=json.loads(a.reference.read_text());mapping=json.loads(a.binary_map.read_text());store=a.store.resolve();built=json.loads((store.parent/'build.json').read_text());before=inventory(store)
 require(built.get('include_points') is True,'Explicit Points product required');require(before==built['store_files'],'Changed store')
 require(ref.get('real_runtime_graph_admission') is True and ref.get('target_images_checked') is False,'Only explicit graph-only reuse supported')
 for name,root in [('runtime',a.runtime),('controllers',a.controllers)]:
  require(subprocess.check_output(['git','-C',str(root),'rev-parse','HEAD'],text=True).strip()==built['source_pins'][name]['commit'],'Wrong selected '+name)
  for path,digest in ref['verified_dependencies'][name].items():require(sha(root/path)==digest,'Changed graph source: '+path)
 for path,digest in ref['source_sha256'].items():require(sha(path)==digest,'Changed graph harness: '+path)
 count=sum(n.endswith('.elf') for n in before);commands=ref['commands'];compile_commands=[c for c in commands if '-DBADGE_PROTOTYPE_ADMISSION=1' in c]
 require(len(compile_commands)==2,'Missing broker on/off compiler records')
 for command in compile_commands:
  require('-DBADGE_EXPECTED_IMAGES='+str(count) in command,'Graph image-count selection changed')
  require(('-DBADGE_TIME_SERVER_SYNC=1' in command)==bool(built.get('time_server_sync',{}).get('enabled')),'Time-sample selection changed')
 require(any('-DRISC_RUNTIME_PREFERENCE_STORE=1' in c for c in compile_commands) and built.get('quick_actions') is True,'Copied preference selection mismatch')
 results={};environment={**os.environ,'ASAN_OPTIONS':'detect_leaks=0','UBSAN_OPTIONS':'halt_on_error=1'}
 with tempfile.TemporaryDirectory(prefix='points-graph-reuse-') as d:
  work=Path(d)
  def execute(binary,root,*extra):
   row=next(r for r in mapping if Path(r['original']).name==binary);data=gzip.decompress(Path(row['archive']).read_bytes());require(hashlib.sha256(data).hexdigest()==row['sha256'],'Preserved executable changed')
   exe=work/binary;exe.write_bytes(data);exe.chmod(0o700)
   try:
    process=subprocess.run([str(exe),str(root),*extra],capture_output=True,text=True,env=environment);require(bool(process.stdout.strip()),'No graph result: '+process.stderr);value=json.loads(process.stdout.splitlines()[-1]);require(value['hardware_calls']==0,'Unexpected hardware call');return process.returncode,value
   finally:exe.unlink()
  code,value=execute('admit',store);require(code==0 and value['prepared'],'Final Points graph refused');results['positive']=value
  code,value=execute('admit-broker-off',store);require(code==4,'Missing broker accepted');results['missing-preference-broker']=value
  code,value=execute('admit',store,'small-kv');require(code==4,'Insufficient bound KV accepted');results['insufficient-bound-kv-capacity']=value
  for cap in ['alarm.service','storage.preferences','runtime.realtime']:
   damaged=work/'store';shutil.copytree(store,damaged)
   try:
    path=damaged/'boot.json';boot=json.loads(path.read_text());row=next(x for x in boot['app_capabilities'] if x['manifest']=='points_in_time.json');row['grants']=[g for g in row['grants'] if g['capability']!=cap];path.write_text(json.dumps(boot));code,value=execute('admit',damaged);require(code==4,'Missing Points grant accepted: '+cap);results['points-missing-'+cap]=value
   finally:shutil.rmtree(damaged)
  for cap in ['alarm.service','storage.key-value','runtime.realtime']:
   damaged=work/'store';shutil.copytree(store,damaged)
   try:
    path=damaged/'points_in_time.json';manifest=json.loads(path.read_text());next(x for x in manifest['requires'] if x['capability']==cap)['api']=99;path.write_text(json.dumps(manifest));code,value=execute('admit',damaged);require(code==4,'Unsupported Points API accepted: '+cap);results['points-unsupported-'+cap]=value
   finally:shutil.rmtree(damaged)
 require(inventory(store)==before,'Store changed during graph execution')
 a.output.write_text(json.dumps({'schema':1,'reference':str(a.reference),'reference_sha256':sha(a.reference),'binary_map':str(a.binary_map),'binary_map_sha256':sha(a.binary_map),'target_build_sha256':sha(store.parent/'build.json'),'store_files':before,'source_pins':built['source_pins'],'results':results,'sanitize':ref['sanitize'],'actual_runtime_graph':True,'target_images_checked':False,'physical_verified':False,'runner_sha256':sha(__file__)},indent=2)+'\n');print('PASS final Points real Runtime graph plus eight refusal cases; no native image/device proof')
if __name__=='__main__':main()

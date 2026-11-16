#!/usr/bin/env python3
"""Actual native bridge exact shadow reuse; normal SDK and bounded fault model."""
import argparse,hashlib,json,os,pathlib,subprocess
p=argparse.ArgumentParser();p.add_argument('--commands',type=pathlib.Path,required=True);p.add_argument('--output',type=pathlib.Path,required=True);p.add_argument('--sanitize',action='store_true');p.add_argument('--target',action='store_true');a=p.parse_args()
r=pathlib.Path(__file__).resolve().parents[1];a.output.mkdir(parents=True,exist_ok=True);records=[]
for profile in ('default','badge','offset','offset180'):
 for rows in (2,4,6):
  for trace in (0,1):
   name=('' if profile=='default' else profile+'-')+f'trace{trace}-probe0';label=f'{profile}-rows{rows}-trace{trace}'
   source=a.commands/(('target-compile-' if a.target else 'host-compile-')+name+'.command.json');fallback180=profile=='offset180' and not source.exists()
   if fallback180:source=a.commands/source.name.replace('offset180-','offset-')
   cmd=json.loads(source.read_text());suffix='components/co5300/native/Co5300Display.cpp' if a.target else 'tests/co5300_native/test.cpp'
   original=next(pathlib.Path(x) for x in cmd if x.endswith(suffix));old=str(original)[:-len(suffix)].rstrip('/')
   cmd=[x.replace(old,str(r)) for x in cmd if not x.startswith('-fsanitize=') and x not in ('-fno-sanitize-recover=all','-fno-omit-frame-pointer')]
   artifact=a.output/('selected.o' if a.target else 'test');cmd[cmd.index('-o')+1]=str(artifact)
   for flag in ('-MF','-MT'):
    if flag in cmd:cmd[cmd.index(flag)+1]=str(artifact)+'.d'
   if fallback180:cmd+=['-URISC_CO5300_PIXEL_ROTATION','-DRISC_CO5300_PIXEL_ROTATION=180']
   cmd+=['-DRISC_CO5300_STRIPE_ROWS='+str(rows),'-DRISC_CO5300_REUSE_UNCHANGED=1','-DRISC_CO5300_TEST_REUSE=1']
   if a.sanitize:cmd+=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-no-pie']
   (a.output/(label+'.command.json')).write_text(json.dumps(cmd,indent=2));result=subprocess.run(cmd,text=True,capture_output=True);(a.output/(label+'.compile.log')).write_text(result.stdout+result.stderr);assert result.returncode==0,(label,result.stderr)
   if not a.target:
    result=subprocess.run([str(artifact)],text=True,capture_output=True,env=os.environ|{'ASAN_OPTIONS':'detect_leaks=0:halt_on_error=1'});(a.output/(label+'.log')).write_text(result.stdout+result.stderr);assert result.returncode==0,(label,result.stdout+result.stderr)
   records.append(dict(profile=profile,rows=rows,trace=trace,sha256=hashlib.sha256(artifact.read_bytes()).hexdigest(),passed=True));artifact.unlink();print(label,'PASS',flush=True)
(a.output/'receipt.json').write_text(json.dumps(dict(records=records,sanitized=a.sanitize,target=a.target,physical=False),indent=2))

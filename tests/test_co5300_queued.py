#!/usr/bin/env python3
"""Exact native queued/full-frame model; default partial contracts remain separate."""
import argparse,json,subprocess,pathlib,os,hashlib
p=argparse.ArgumentParser();p.add_argument('--commands',type=pathlib.Path,required=True);p.add_argument('--output',type=pathlib.Path,required=True);p.add_argument('--sanitize',action='store_true');a=p.parse_args()
r=pathlib.Path(__file__).resolve().parents[1];a.output.mkdir(parents=True,exist_ok=True);receipts=[]
for profile in ('default','badge','offset','offset180'):
 for rows in (2,4,8,16,32):
  for trace in (0,1):
   label=f'{profile}-rows{rows}-trace{trace}'
   cmd=json.loads((a.commands/f'{profile}-rows4-trace{trace}.command.json').read_text());old=next(x for x in cmd if x.endswith('/tests/co5300_native/test.cpp'))[:-len('/tests/co5300_native/test.cpp')]
   cmd=[x.replace(old,str(r)) for x in cmd if not x.startswith(('-DRISC_CO5300_STRIPE_ROWS=','-DRISC_CO5300_REUSE_UNCHANGED=','-fsanitize=')) and x not in ('-fno-sanitize-recover=all','-fno-omit-frame-pointer')]
   cmd[cmd.index(str(r/'tests/co5300_native/test.cpp'))]=str(r/'tests/co5300_native/queued.cpp');cmd[cmd.index('-o')+1]=str(a.output/'test')
   cmd+=['-DRISC_CO5300_STRIPE_ROWS='+str(rows),'-DRISC_CO5300_REUSE_UNCHANGED=1','-DRISC_CO5300_QUEUED_PIXELS=1','-Wno-return-type','-g0']
   if a.sanitize:cmd+=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-no-pie']
   (a.output/(label+'.command.json')).write_text(json.dumps(cmd,indent=2))
   with (a.output/(label+'.log')).open('w') as log:
    for c in (cmd,[str(a.output/'test')]):
     result=subprocess.run(c,stdout=log,stderr=subprocess.STDOUT,env=os.environ|{'TMPDIR':str(a.output),'ASAN_OPTIONS':'detect_leaks=0:halt_on_error=1:quarantine_size_mb=16'})
     if result.returncode:raise SystemExit(label+' failed '+str(result.returncode))
   receipts.append({'profile':profile,'rows':rows,'trace':trace,'passed':True,'artifact_sha256':hashlib.sha256((a.output/'test').read_bytes()).hexdigest()});(a.output/'test').unlink();print(label,'PASS',flush=True)
(a.output/'receipt.json').write_text(json.dumps({'lanes':receipts,'sanitize':a.sanitize,'physical':False},indent=2))

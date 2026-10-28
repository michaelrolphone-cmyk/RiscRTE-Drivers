#!/usr/bin/env python3
"""Shared inventory/parser and exact selected artifact package mutations."""
import argparse,copy,itertools,json,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from execution_case_inventory import case_ids,time_sync_cases
from time_sync_evidence_gate import qualify_product,qualify_execution
p=argparse.ArgumentParser();p.add_argument('--target',type=Path,required=True);p.add_argument('--native',type=Path,required=True);p.add_argument('--execution',type=Path,required=True);a=p.parse_args()
read=lambda p:json.loads(p.read_text());built=read(a.target/'build.json');proof=read(a.native/'build-proof.json');record=read(a.target/'wifi/wifi_settings-build-record.json');manifest=read(a.target/'store/wifi_settings.json');boot=read(a.target/'store/boot.json');header=(Path(__file__).resolve().parents[1]/'sdk/driver/RiscTimeSampleV1.h').read_bytes();receipt=read(a.execution)
count=0
for fg,files,alarm,quick,wifi,sync in itertools.product([False,True],repeat=6):
 b={'store_files':{'file_browser.elf':{}} if files else {},'native_alarms':alarm,'quick_actions':quick,'native_wifi_geometry':wifi,'time_server_sync':{'enabled':sync}}
 if sync and not(wifi and alarm):
  try:case_ids(b,fg)
  except ValueError:continue
  raise AssertionError('Invalid dependency accepted')
 # Historical runner expression is preserved as the independent extraction oracle.
 expected=47 if sync and wifi else 46 if wifi else 42 if quick else 34 if alarm else 23 if files else 21 if fg else 11
 assert case_ids(b,fg)==tuple(range(expected));count+=1
assert count==40
for source in ['total_cases=__import__("os").system("false")','total_cases=47+1','total_cases=True','total_cases=0','total_cases=47\ntotal_cases=47','total_cases=47 if unknown else 46','total_cases=47 if native_wifi_geometry else bad()']:
 try:case_ids(built,source=source)
 except ValueError:pass
 else:raise AssertionError(source)
qualify_product(built,proof,record,manifest,boot,header);qualify_execution(built,receipt)
def reject(which,mutate):
 xs=copy.deepcopy([built,proof,record,manifest,boot,header]);mutate(xs[which])
 try:qualify_product(*xs)
 except ValueError:return
 raise AssertionError('Invalid product selection accepted')
reject(1,lambda x:x['time_sample'].update(enabled=False))
reject(0,lambda x:x['store_files'].pop('time-sample/driver.elf'))
reject(0,lambda x:x['time_server_sync'].update(authentication='trusted'))
reject(2,lambda x:x['time_server_sync'].update(header_sha256='0'*64))
reject(2,lambda x:x['time_server_sync'].update(server='wrong.invalid'))
reject(4,lambda x:x['drivers'].append({'manifest':'time-sample/manifest.json'}))
reject(4,lambda x:x['app_capabilities'][0]['grants'].append({'capability':'net.time-sample','api':1,'instance_id':0}))
for kind in ['missing','duplicate','marker']:
 x=copy.deepcopy(receipt);ids=time_sync_cases(built)
 if kind=='missing':x['results']=[c for c in x['results'] if c['target_index'] not in ids]
 elif kind=='duplicate':x['results']+=[next(c for c in x['results'] if c['target_index'] in ids)]
 else:next(c for c in x['results'] if c['target_index'] in ids)['stdout']='PASS generic'
 try:qualify_execution(built,x)
 except ValueError:pass
 else:raise AssertionError(kind)
print('PASS 40 exact profile inventories, invalid selections, seven unsafe AST forms, exact selected artifact and ten product/execution mutations')

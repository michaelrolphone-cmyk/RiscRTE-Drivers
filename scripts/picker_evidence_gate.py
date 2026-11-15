"""Exact-target picker coverage; host substitutions cannot qualify a product."""
from execution_source_inputs import validated_inputs
import hashlib,re
from pathlib import Path
FIXTURES={'scripts/test_badge_picker_execution.py','tests/badge_picker_execution.cpp','tests/badge_picker_execution.inc','tests/badge_alarm_execution.inc','tests/badge_quick_execution.inc','tests/badge_wifi_execution.inc'}
EXPECTED=['full-scroll-return','slow-display-scroll-return','allocation-failure-retry','typed-multicontact-gap-recovery','expected-terminal-retention']
def require(ok,message):
 if not ok:raise ValueError(message)
def qualify_picker(built,r,sanitize,files,target_build_sha,root,verify_sources=True):
 require(r.get('qualification')=='badge-picker-execution' and r.get('schema')==1 and r.get('passed') is True,'Missing picker qualification')
 require(r.get('sanitize') is sanitize and r.get('native_alpha_host_experiment') is None,'Picker lane or host substitution mismatch')
 require(r.get('timed_stress') is False and r.get('coverage_contract')=='state-acknowledged all34 identities','Picker stress is not all-face qualification')
 require(r.get('target_build_sha256')==target_build_sha and r.get('source_pins')==built['source_pins'] and r.get('store_files')==files,'Mixed picker target cohort')
 require(r.get('case_count')==5 and r.get('cases')==EXPECTED and [x.get('target_index') for x in r.get('results',[])]==list(range(5)),'Incomplete picker case coverage')
 for i,x in enumerate(r['results']):
  out=x.get('stdout','');require(x.get('returncode')==0 and x.get('stderr')=='','Failed picker case')
  if i==4:
   require('PICKER_FAULT typed terminal stream result after actual next' in out and 'PASS expected-retained raw-CST picker typed terminal; Runtime failed/retained, no later hardware I/O, no ordinary UI-return claim' in out,'Missing expected-retained picker fence')
   require('PASS actual Runtime raw-CST picker' not in out,'Retained result mislabeled ordinary success')
  else:
   rows=[s for s in out.splitlines() if s.startswith('PASS actual Runtime raw-CST picker case='+str(i)+' ')]
   require(len(rows)==1,'Missing exact picker result')
   fields={k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',rows[0])}
   require(fields.get('categories')==31 and fields.get('faces')==(1<<34)-1,'Incomplete face/category traversal')
   require(all(fields.get(k,0)>0 for k in ['position_changes','picker_frames','preview_renders','cache_bytes']),'Missing picker movement/render proof')
   require('cancellation_return=passed modeled_hardware_only' in rows[0],'Missing picker return proof')
   if i==2:require(fields.get('allocations',0)>=2,'Missing allocation-refusal retry')
   if i==3:require(fields.get('neutral_gap',0)>0,'Missing uncertain-input recovery')
 require(r.get('source_sha256') and set(r.get('fixture_sha256',{}))==FIXTURES,'Missing picker source provenance')
 if verify_sources:
  for n,d in r['fixture_sha256'].items():require(hashlib.sha256((Path(root)/n).read_bytes()).hexdigest()==d,'Changed picker fixture: '+n)
  validated_inputs(r)

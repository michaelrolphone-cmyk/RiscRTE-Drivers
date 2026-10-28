"""Exact selected Settings UI writes must drive the established Home timers."""
from execution_source_inputs import validated_inputs
from pathlib import Path
import hashlib
EXPECTED=['automatic','manual','cancel-time','io-retry']
FIXTURES={'scripts/test_badge_settings_execution.py','tests/badge_settings_execution.cpp','tests/badge_settings_ui_execution.inc','tests/badge_hybrid_alarm_execution.inc','tests/badge_quick_execution.inc'}
def require(ok,message):
 if not ok:raise ValueError(message)
def qualify_settings(built,r,sanitize,files,target_build_sha,root,verify_sources=True):
 require(r.get('qualification')=='badge-settings-timers' and r.get('schema')==1 and r.get('passed') is True,'Missing Settings UI qualification')
 require(r.get('sanitize') is sanitize and r.get('settings_ui') is True and r.get('expected_regression') is False and r.get('short_prepare') is False,'Wrong Settings UI lane/scope')
 require(r.get('target_build_sha256')==target_build_sha and r.get('source_pins')==built['source_pins'] and r.get('store_files')==files,'Mixed Settings UI cohort')
 results=r.get('results',[])
 require(r.get('case_count')==4 and [(x.get('target_index'),x.get('scenario')) for x in results]==[(0,k) for k in EXPECTED],'Incomplete Settings scenarios')
 for x in results:
  out=x.get('stdout','');scenario=x['scenario']
  require(x.get('returncode')==0 and x.get('stderr')=='','Failed Settings UI execution')
  require('SETTINGS_SAVE key=sleep_idle ms=' in out and 'SETTINGS_SAVE key=sleep_deep ms=' in out and 'pending_frame=' in out,'Missing actual UI Save/completed pending frame')
  require('PASS actual Runtime Settings timers scenario='+scenario in out,'Missing Settings scenario success')
  if scenario=='cancel-time':
   require('cancel_Back_reopen_time_Save_preserved=passed' in out and 'NATIVE_SETTINGS_UI explicit_Save' in out and 'PASS actual Runtime navigation target=0' in out,'Missing cancel/time/return preservation')
  else:require('configured_Hybrid=passed' in out and 'PASS actual Runtime HYBRID timed Light then Deep' in out,'Missing saved timer Home execution')
  if scenario=='io-retry':require('SETTINGS_IO refused actual_Save key=sleep_idle durable_unchanged=1' in out and 'SETTINGS_IO completed_unconfirmed_frame explicit_retry_next=1' in out,'Missing failed-save uncertainty/retry proof')
 require(r.get('source_sha256') and set(r.get('fixture_sha256',{}))==FIXTURES,'Missing Settings source provenance')
 if verify_sources:
  for name,digest in r['fixture_sha256'].items():require(hashlib.sha256((Path(root)/name).read_bytes()).hexdigest()==digest,'Changed Settings fixture: '+name)
  validated_inputs(r)

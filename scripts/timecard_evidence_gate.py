"""Receipt gate for installed Timecard's actual Runtime workflows, not device proof."""
from execution_source_inputs import validated_inputs
from pathlib import Path
import hashlib,re
EXPECTED=['save','cold-load','light-draft','alarm-draft','quick-shared','quick-retained']
FIXTURES={'scripts/test_badge_timecard_execution.py','tests/badge_timecard_execution.cpp','tests/badge_timecard_execution.h','tests/badge_timecard_execution.inc','tests/badge_alarm_execution.inc','tests/badge_quick_execution.inc','tests/badge_wifi_execution.inc'}
def require(ok,message):
 if not ok:raise ValueError(message)
def qualify_timecard(built,r,sanitize,files,target_build_sha,root,verify_sources=True):
 require(r.get('qualification')=='badge-timecard' and r.get('schema')==1 and r.get('passed') is True,'Missing Timecard qualification')
 require(r.get('sanitize') is sanitize and r.get('include_timecard') is True and r.get('foreground') is True,'Wrong Timecard lane/profile')
 require(r.get('target_build_sha256')==target_build_sha and r.get('source_pins')==built['source_pins'] and r.get('store_files')==files,'Mixed Timecard cohort')
 require(r.get('host_preference_store')==1 and r.get('host_atomic_key_value')==0 and r.get('native_alarms') is True and r.get('quick_actions') is True,'Wrong actual Timecard dependencies')
 require(r.get('physical_tested') is False and r.get('target_instruction_execution') is False,'Wrong Timecard evidence boundary')
 catalog=r.get('launcher_catalog',[]);names=[x.get('file_name') for x in catalog]
 require(len(names)==len(set(names)) and names.count('timecard.elf')==names.count('settings.elf')==1,'Missing unique manifest-derived launcher identity')
 catalog_path=r.get('launcher_catalog_path');require(catalog_path and r.get('source_sha256',{}).get(catalog_path)==r.get('launcher_catalog_sha256'),'Unbound launcher catalog')
 if verify_sources:require(hashlib.sha256(Path(catalog_path).read_bytes()).hexdigest()==r['launcher_catalog_sha256'],'Changed launcher catalog')
 results=r.get('results',[])
 require(r.get('case_count')==6 and [(x.get('target_index'),x.get('scenario')) for x in results]==[(0,k) for k in EXPECTED],'Incomplete Timecard scenarios')
 for x in results:
  out=x.get('stdout','');scenario=x['scenario']
  require(x.get('returncode')==0 and x.get('stderr')=='','Failed Timecard execution')
  lines=[line for line in out.splitlines() if line.startswith('PASS actual Runtime Timecard ')]
  require(len(lines)==1,'Missing or ambiguous Timecard success')
  line=lines[0];fields=dict(re.findall(r'(\w+)=([^ ]+)',line))
  expected={'scenario':scenario,'launcher_item':str(names.index('timecard.elf')+1),'catalog':str(len(names)),'no_after_retained':'passed'}
  require(all(fields.get(k)==v for k,v in expected.items()) and line.endswith('modeled_not_device'),'Wrong Timecard success identity')
  keys=['actual_scroll','data_reads','data_writes','light','restored','pending_frames','copied_preference_writes','retained']
  require(all(re.fullmatch(r'[0-9]+',fields.get(k,'')) for k in keys),'Missing or invalid Timecard result counts')
  n={k:int(fields[k]) for k in keys}
  require(n['actual_scroll']>0,'Timecard launcher scroll not executed')
  require(n['data_writes']==int(scenario=='save'),'Wrong Timecard durable write count')
  require(n['retained']==int(scenario=='quick-retained'),'Wrong Timecard retention outcome')
  if scenario=='cold-load':require(n['data_reads']>0,'Missing cold AppData read')
  if scenario=='light-draft':require(n['light']==1 and n['restored']==1,'Missing exact Light draft restoration')
  if scenario in ['quick-shared','quick-retained']:
   require(n['copied_preference_writes']==1 and n['pending_frames']>0,'Missing actual preference write/pending frame')
 foreground=set(built.get('foreground_idle',{}).get('apps',[]))
 require('timecard' in foreground and r.get('timecard_recipe') and set(r.get('foreground_recipes',{}))==foreground,'Missing selected foreground app recipe')
 require(r.get('source_sha256') and set(r.get('fixture_sha256',{}))==FIXTURES,'Missing Timecard source provenance')
 if verify_sources:
  for name,digest in r['fixture_sha256'].items():require(hashlib.sha256((Path(root)/name).read_bytes()).hexdigest()==digest,'Changed Timecard fixture: '+name)
  validated_inputs(r)

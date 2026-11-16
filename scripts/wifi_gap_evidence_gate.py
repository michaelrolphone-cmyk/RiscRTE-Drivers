"""Installed native Wi-Fi draft must survive actual recoverable report gaps."""
from execution_source_inputs import validated_inputs
from pathlib import Path
import hashlib
EXPECTED=['marker','count','coordinates','bus']
FIXTURES={'scripts/test_badge_wifi_gap_execution.py','tests/badge_wifi_gap_execution.cpp','tests/badge_wifi_gap_execution.inc','tests/badge_alarm_execution.inc','tests/badge_quick_execution.inc'}
def require(ok,message):
 if not ok:raise ValueError(message)
def qualify_wifi_gap(built,r,sanitize,files,target_build_sha,root,verify_sources=True):
 require(r.get('qualification')=='wifi-report-gap' and r.get('schema')==1 and r.get('passed') is True,'Missing installed Wi-Fi gap success')
 require(r.get('sanitize') is sanitize and r.get('native_wifi_geometry') is True and built.get('native_wifi_geometry') is True,'Wrong native Wi-Fi gap lane/profile')
 require(r.get('target_build_sha256')==target_build_sha and r.get('source_pins')==built['source_pins'] and r.get('store_files')==files,'Mixed Wi-Fi gap cohort')
 results=r.get('results',[])
 require(r.get('case_count')==4 and [(x.get('target_index'),x.get('fault')) for x in results]==[(43,k) for k in EXPECTED],'Incomplete Wi-Fi fault coverage')
 for x in results:
  out=x.get('stdout','')
  require(x.get('returncode')==0 and x.get('stderr')=='','Failed Wi-Fi gap execution')
  for marker in ['WIFI_REPORT_GAP kind='+x['fault'],'WIFI_GAP fresh_physical_key_exactly_once=passed retained_draft=passed','PASS actual Runtime installed native WiFi report gap','PASS actual Runtime native WiFi case=43','namespace6_writes=0','returning_Light=3','completed_restore=passed']:
   require(marker in out,'Missing Wi-Fi gap proof: '+marker)
 require(r.get('source_sha256') and set(r.get('fixture_sha256',{}))==FIXTURES,'Missing Wi-Fi gap source provenance')
 if verify_sources:
  for n,d in r['fixture_sha256'].items():require(hashlib.sha256((Path(root)/n).read_bytes()).hexdigest()==d,'Changed Wi-Fi gap fixture: '+n)
  validated_inputs(r)

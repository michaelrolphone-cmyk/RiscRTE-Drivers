"""Accept only exact-cohort successful recovery from actual touch report gaps."""
from execution_source_inputs import validated_inputs
from pathlib import Path
import hashlib
EXPECTED=['marker','count','coordinates','bus']
def require(ok,message):
 if not ok:raise ValueError(message)
def qualify_springboard(built,r,sanitize,files,target_build_sha,verify_sources=True):
 require(r.get('qualification')=='springboard-report-gap' and r.get('schema')==1 and r.get('passed') is True,'Missing Springboard recovery qualification')
 require(r.get('sanitize') is sanitize and r.get('expected_retained') is False,'Wrong Springboard recovery lane or regression oracle')
 require(r.get('target_build_sha256')==target_build_sha and r.get('source_pins')==built['source_pins'] and r.get('store_files')==files,'Mixed Springboard recovery cohort')
 results=r.get('results',[])
 require(r.get('case_count')==4 and [(x.get('target_index'),x.get('fault')) for x in results]==[(0,k) for k in EXPECTED],'Incomplete Springboard report fault coverage')
 for item in results:
  out=item.get('stdout','')
  require(item.get('returncode')==0 and item.get('stderr')=='','Failed Springboard recovery execution')
  require('CST_RECOVERY kind='+item['fault'] in out and 'actual_Settings_Save_and_fresh_Home=passed' in out and 'PASS actual Runtime navigation target=0' in out,'Missing substantive navigation recovery proof')
 require(r.get('source_sha256'),'Missing Springboard recovery source provenance')
 if verify_sources:
  validated_inputs(r)

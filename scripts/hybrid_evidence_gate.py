"""Require actual staged Home execution, including the short-yield PMU defect."""
from execution_source_inputs import validated_inputs
from pathlib import Path
import hashlib
EXPECTED=[(6,'timer'),(20,'timer'),(6,'gpio'),(20,'gpio'),(6,'default'),(20,'default'),(23,'alarm-earlier'),(23,'alarm-equal')]
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def require(ok,message):
 if not ok:raise ValueError(message)
def qualify_hybrid(built,r,sanitize,files,target_build_sha,root,verify_sources=True):
 require(r.get('qualification')=='badge-hybrid-execution' and r.get('schema')==1 and r.get('passed') is True,'Missing staged execution success')
 require(r.get('sanitize') is sanitize and r.get('expected_regression') is False and r.get('settings_ui') is False,'Wrong staged execution lane/scope')
 require(r.get('short_prepare') is True,'Missing actual short-yield PMU regression')
 require(r.get('target_build_sha256')==target_build_sha and r.get('source_pins')==built['source_pins'] and r.get('store_files')==files,'Mixed staged execution cohort')
 require(r.get('case_count')==8 and [(x.get('target_index'),x.get('scenario')) for x in r.get('results',[])]==EXPECTED,'Incomplete staged sleep scenarios')
 require(all(x.get('returncode')==0 and 'PASS actual Runtime HYBRID' in x.get('stdout','') for x in r['results']),'Failed staged sleep scenario')
 require(r.get('source_sha256') and r.get('fixture_sha256'),'Missing staged source provenance')
 if verify_sources:
  for name,digest in r['fixture_sha256'].items():
   p=Path(name);require(not p.is_absolute() and '..' not in p.parts and sha(Path(root)/p)==digest,'Changed staged fixture: '+name)
  validated_inputs(r)

#!/usr/bin/env python3
"""Owning package validation: reconstruct only the recorded generated mapper."""
import argparse,copy,hashlib,json,tempfile
from pathlib import Path
from badge_host_mapper import stage_runtime_mapper
from execution_source_inputs import validated_inputs

def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--runtime',type=Path,required=True);ap.add_argument('--integration',type=Path);a=ap.parse_args()
 with tempfile.TemporaryDirectory(prefix='badge-mapper-gate-test-') as td:
  root=Path(td);runtime=root/'runtime';source=runtime/'test/drivers/stubs/esp_dlfcn.h';source.parent.mkdir(parents=True)
  source.write_bytes((a.runtime/'test/drivers/stubs/esp_dlfcn.h').read_bytes())
  output=stage_runtime_mapper(runtime,root/'lane-a'/'host-mapper');header=output/'esp_dlfcn.h'
  helper=Path(__file__).with_name('badge_host_mapper.py').resolve();ordinary=root/'ordinary';ordinary.write_text('strict input')
  r={'source_sha256':{str(header):digest(header),str(helper):digest(helper),str(ordinary):digest(ordinary)},'host_mapper_staging':json.loads((output/'staging.json').read_text())}
  expected=validated_inputs(r);original_header=header.read_bytes();header.unlink()
  assert validated_inputs(r)==expected
  other=copy.deepcopy(r);other['source_sha256'][str(root/'lane-b'/'host-mapper'/'esp_dlfcn.h')]=other['source_sha256'].pop(str(header))
  assert validated_inputs(other)==expected
  # Every consumer uses the same strict reconstruction path; no focused lane
  # silently skips deleted inputs or compares unstable generated paths.
  scripts=Path(__file__).parent
  for name in ('hybrid','settings','picker','wifi_gap','springboard','timecard','local_quick'):
   assert 'validated_inputs(r)' in (scripts/(name+'_evidence_gate.py')).read_text()
  assert 'validated_inputs(receipt,' in (scripts/'execution_source_equivalence.py').read_text()
  assert 'validated_inputs(read(a.execution_sanitized))' in (scripts/'package_badge_prototype.py').read_text()
  alias=root/'ordinary-alias';alias.symlink_to(ordinary)
  aliases=copy.deepcopy(r);aliases['source_sha256'][str(alias)]=digest(ordinary)
  assert validated_inputs(aliases)==expected
  # A clean identical owning checkout may move; bytes, not its transient path,
  # identify the generator. All selected source files remain hash-checked.
  relocated=root/'other-owner/scripts/badge_host_mapper.py';relocated.parent.mkdir(parents=True);relocated.write_bytes(helper.read_bytes())
  moved=copy.deepcopy(r);moved['source_sha256'][str(relocated)]=moved['source_sha256'].pop(str(helper))
  assert validated_inputs(moved)[str(relocated.resolve())]==digest(helper)
  passed=[]
  def refuse(name, candidate):
   try:validated_inputs(candidate)
   except (ValueError,FileNotFoundError,AssertionError):passed.append(name)
   else:raise AssertionError('Accepted '+name)
  bad=copy.deepcopy(aliases);bad['source_sha256'][str(alias)]='0'*64;refuse('conflicting alias digest',bad)
  ambiguous=copy.deepcopy(r);ambiguous['source_sha256'][str(relocated)]=digest(relocated);refuse('ambiguous generator owners',ambiguous)
  relocated.write_bytes(helper.read_bytes()+b'tamper');refuse('relocated generator changed',moved)
  bad=copy.deepcopy(r);bad['host_mapper_staging']['changes'][0][1]+='tamper';refuse('recipe',bad)
  bad=copy.deepcopy(r);bad['host_mapper_staging']['staged_sha256']='0'*64;refuse('staged digest',bad)
  bad=copy.deepcopy(r);bad['source_sha256'][str(header)]='0'*64;refuse('input digest',bad)
  header.write_bytes(original_header+b'tamper');refuse('existing generated header',r);header.unlink()
  original=source.read_bytes();source.write_bytes(original+b'tamper');refuse('selected source',r);source.write_bytes(original)
  bad=copy.deepcopy(r);bad['host_mapper_staging']['selected_source_sha256']='0'*64;refuse('selected digest',bad)
  bad=copy.deepcopy(r);bad['source_sha256'][str(root/'missing')]='0'*64;refuse('unrecognized missing input',bad)
  bad=copy.deepcopy(r);bad['source_sha256'][str(helper)]='0'*64;refuse('generator digest',bad)
  ordinary.write_text('tamper');refuse('ordinary source',r);ordinary.write_text('strict input')
  bad=copy.deepcopy(r);del bad['host_mapper_staging'];refuse('missing recipe',bad)
  assert validated_inputs(r)==expected
  print(json.dumps({'passed':True,'negative_cases':passed,'existing_and_deleted_mapper':True,'cross_lane_canonical_identity':True,'other_sources_strict':True}))
 if a.integration:
  from local_quick_evidence_gate import qualify
  q=a.integration;built=json.loads((q/'target/build.json').read_text())
  reference=validated_inputs(json.loads((q/'execution-sanitized.json').read_text()))
  for lane,mode in [('normal',False),('sanitized',True)]:
   r=json.loads((q/('local-quick-'+lane+'.json')).read_text())
   qualify(built,r,mode,Path(__file__).resolve().parents[1],reference)
   print('PASS actual local Quick '+lane+' complete gate and full-source canonical comparison')
if __name__=='__main__':main()

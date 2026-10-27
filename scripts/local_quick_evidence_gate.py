"""Exact selected temporary local Quick profile and generated-fixture evidence."""
from execution_source_inputs import validated_inputs
import hashlib,json,subprocess,sys,tempfile
from pathlib import Path
from package_badge_flash import require,sha
from temporary_quick_profile import qualify as qualify_profile

def selection(built):
 selected=built.get('springboard_quick_actions',False)
 require(type(selected) is bool,'Springboard Quick selection must be boolean')
 require(not selected or built.get('quick_actions') is True and built.get('sliced_native_frames') is True,'Springboard Quick requires copied Home preferences and sliced frames')
 return selected

def profile(built,recipe,manifest,grants):
 selected=selection(built)
 qualify_profile(recipe,manifest,grants,selected)
 if selected:
  require(recipe.get('compile_inputs',{}).get('definitions')==recipe.get('build_defines'),'Springboard Quick exact compile definitions mismatch')
  require(recipe.get('grant_bindings',{}).get('storage.preferences')==[1],'Springboard Quick recipe namespace mismatch')
  require(recipe.get('sliced_native_frames') is True and recipe.get('quick_radios') is False,'Springboard Quick rendering/radio selection mismatch')
  if 'applications' in built:
   require(recipe.get('sha256')==built['applications']['springboard']['psram_safety']['original_elf_sha256'],'Springboard recipe/target artifact mismatch')
   require(recipe.get('repository_commit')==built['source_pins']['system']['commit'] and recipe.get('working_tree_dirty') is False,'Springboard recipe source identity mismatch')
  require('-DPORTABLE_QUICK_RADIOS' not in recipe['build_defines'],'Unexpected Springboard radio controls')

def qualify(built,r,mode,root,reference_sources=None):
 require(selection(built),'Unselected local Quick evidence')
 require(r.get('qualification')=='temporary-springboard-local-quick' and r.get('temporary_local_ownership') is True and r.get('resident_architecture_complete') is False,'Missing temporary local Quick qualification')
 require(r.get('passed') is True and r.get('sanitize') is mode and type(r.get('case_count')) is int and r.get('case_count')==4,'Missing complete local Quick lane')
 require(r.get('source_pins')==built['source_pins'] and r.get('store_files')==built['store_files'],'Mixed local Quick cohort')
 results=r.get('results',[])
 require([x.get('target_index') for x in results]==[34,37,38,40] and all(type(x.get('target_index')) is int for x in results),'Missing local Quick save/fault/Light/alarm cases')
 for x in results:
  require(not x.get('stderr') and f'PASS actual Runtime quick actions ownership=temporary-local case={x["target_index"]} ' in x.get('stdout','') and 'APP_ENTER default 1' in x['stdout'] and 'APP_ENTER springboard 2' in x['stdout'],'Incomplete local Quick runtime evidence')
 require('MODELED_PROVIDER_FAULT collect99 after actual terminal collection; client must retain' in results[1]['stdout'],'Missing actual local copied-result fault')
 sources=r.get('source_sha256',{})
 require(sources,'Missing local Quick source provenance')
 resolved=validated_inputs(r)
 if reference_sources is not None:
  altered=('scripts/test_badge_prototype_execution.py','tests/badge_prototype_execution.cpp','tests/badge_quick_execution.inc')
  for p,d in reference_sources.items():
   if isinstance(p,str) and any(p.endswith('/'+name) for name in altered):continue
   require(resolved.get(p)==d,'Local Quick omitted/changed shared execution input: '+str(p))
 # Use the same hash-locked observer profile as qualification setup. The
 # historical transform generator predates additive sampler bindings; it is
 # not the recipe used to execute this frozen observer cohort.
 from qualification_fixture import profile_load
 profile=root/'tests/qualification_profiles/local-quick'
 _,locked,_=profile_load(profile)
 expected=('scripts/test_badge_prototype_execution.py','tests/badge_prototype_execution.cpp','tests/badge_quick_execution.inc')
 require(set(locked)==set(expected),'Incomplete local Quick observer profile')
 for name in expected:
  matches=[p for p in sources if p.endswith('/'+name)]
  require(len(matches)==1 and sources[matches[0]]==locked[name],'Local Quick locked fixture mismatch: '+name)

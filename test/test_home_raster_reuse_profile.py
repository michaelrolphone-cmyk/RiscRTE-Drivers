#!/usr/bin/env python3
import copy, hashlib, itertools, pathlib, sys, tempfile
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'scripts'))
from home_raster_reuse_profile import qualify
count=0
with tempfile.TemporaryDirectory() as t:
 root=pathlib.Path(t); hashes={}
 for p in ('apps/clock/nova/nova.c','apps/clock/nova/native_stroke_cache.h','apps/clock/nova/native_reuse_admission.h','apps/clock/faces/render.inc'):
  f=root/p;f.parent.mkdir(parents=True,exist_ok=True);f.write_text(p);hashes[p]=hashlib.sha256(f.read_bytes()).hexdigest()
 for glyph,stroke in itertools.product((False,True),repeat=2):
  flags=[flag for flag,on in [('-DWATCH_NATIVE_COVERAGE_CACHE',glyph),('-DWATCH_NATIVE_STROKE_CACHE',stroke)] if on]
  r={'artifacts':{'default':{'defines':flags}},'native_raster_caches':{'glyph_enabled':glyph,'stroke_enabled':stroke,'input_sha256':hashes}}
  assert qualify(r,glyph,stroke,root)==r['native_raster_caches'];count+=1
  mutations=[]
  for key in ('glyph_enabled','stroke_enabled'):
   q=copy.deepcopy(r);q['native_raster_caches'][key]=not q['native_raster_caches'][key];mutations.append(q)
  for flag in ('-DWATCH_NATIVE_COVERAGE_CACHE=0','-DWATCH_NATIVE_STROKE_CACHE=0','-UWATCH_NATIVE_COVERAGE_CACHE','-UWATCH_NATIVE_STROKE_CACHE'):
   q=copy.deepcopy(r);q['artifacts']['default']['defines'].append(flag);mutations.append(q)
  for flag in flags:
   q=copy.deepcopy(r);q['artifacts']['default']['defines'].append(flag);mutations.append(q)
  for path in hashes:
   q=copy.deepcopy(r);q['native_raster_caches']['input_sha256'][path]='0'*64;mutations.append(q)
  for q in mutations:
   try: qualify(q,glyph,stroke,root)
   except ValueError: count+=1
   else: raise AssertionError(q)
 assert qualify({'artifacts':{'default':{'defines':[]}}},False,False,root)=={'glyph_enabled':False,'stroke_enabled':False};count+=1
print(count,'exact selection/source/duplicate/undefinition/fallback cases PASS')

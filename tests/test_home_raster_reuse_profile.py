#!/usr/bin/env python3
"""Selected optional composition is bound to actual flags and owning source bytes."""
import copy, hashlib, importlib.util, itertools, pathlib, tempfile, unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('profile', ROOT/'scripts/home_raster_reuse_profile.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class Selection(unittest.TestCase):
 def test_matrix(self):
  paths=('apps/clock/nova/nova.c','apps/clock/nova/native_stroke_cache.h','apps/clock/nova/native_reuse_admission.h','apps/clock/faces/render.inc','apps/clock/faces/picker_compositor.h','apps/clock/nova/native_damage_decl.h','apps/clock/faces/damage_replay.inc')
  with tempfile.TemporaryDirectory() as tmp:
   root=pathlib.Path(tmp)
   for path in paths:
    p=root/path;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(path)
   hashes={p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in paths}
   for glyph,stroke,picker,damage in itertools.product((False,True),repeat=4):
    flags=[ '-D'+name for name,on in [('WATCH_NATIVE_COVERAGE_CACHE',glyph),('WATCH_NATIVE_STROKE_CACHE',stroke),('WATCH_PICKER_COMPOSITOR_PLAN',picker),('WATCH_NATIVE_DAMAGE_REPLAY',damage)] if on]
    record={'artifacts':{'default':{'defines':flags}},'native_raster_caches':{'glyph_enabled':glyph,'stroke_enabled':stroke,'picker_compositor_enabled':picker,'native_damage_replay_enabled':damage,'input_sha256':hashes}}
    self.assertEqual(m.qualify(record,glyph,stroke,root,picker,damage),record['native_raster_caches'])
    bad=copy.deepcopy(record);bad['native_raster_caches']['native_damage_replay_enabled']=not damage
    with self.assertRaises(ValueError):m.qualify(bad,glyph,stroke,root,picker,damage)
    bad=copy.deepcopy(record);bad['native_raster_caches']['picker_compositor_enabled']=not picker
    with self.assertRaises(ValueError):m.qualify(bad,glyph,stroke,root,picker,damage)
    for flag in ('-DWATCH_PICKER_COMPOSITOR_PLAN=0','-UWATCH_PICKER_COMPOSITOR_PLAN','-DWATCH_NATIVE_DAMAGE_REPLAY=0','-UWATCH_NATIVE_DAMAGE_REPLAY'):
     bad=copy.deepcopy(record);bad['artifacts']['default']['defines'].append(flag)
     with self.assertRaises(ValueError):m.qualify(bad,glyph,stroke,root,picker,damage)
    bad=copy.deepcopy(record);bad['native_raster_caches']['input_sha256'][paths[-1]]='wrong'
    with self.assertRaises(ValueError):m.qualify(bad,glyph,stroke,root,picker,damage)
   legacy={'artifacts':{'default':{'defines':[]}}}
   self.assertFalse(m.qualify(legacy,False,False,root)['glyph_enabled'])
   with self.assertRaises(ValueError):m.qualify(legacy,False,False,root,True)
if __name__=='__main__':unittest.main()

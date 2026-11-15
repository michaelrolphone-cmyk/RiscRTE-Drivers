import ast,copy,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from springboard_gesture_profile import selected,runtime_defines,qualify,FLAG,CLASSIFICATION

def recipe():
 return {'build_defines':[FLAG,'-DPORTABLE_PERFORMANCE_TRACE'],'watch_native_ui':True,'time_profile':'watch-native-time','native_performance_trace':{'enabled':True},'springboard_gesture_diagnostics':{'schema':1,'classification':CLASSIFICATION,'selector':FLAG[2:],'tags':[224,225,226,227,228],'max_records_per_gesture':14,'max_semantic_points':4,'provider_coordinates':'decoded-not-raw-CST-bytes','scope':'Springboard-owned only; suppress Quick/alarm/other apps','transport':'existing-owner-RAM-trace; explicit perf capture only','physical_verified':False}}
class Selection(unittest.TestCase):
 def test_default_off(self):
  for p in [{},{'native_performance_trace':True},{'springboard_gesture_diagnostics':False}]:
   self.assertFalse(selected(p));self.assertEqual(runtime_defines(p),[])
  qualify({},False)
 def test_selected(self):
  p={'springboard_gesture_diagnostics':True,'native_performance_trace':True}
  self.assertTrue(selected(p));self.assertEqual(runtime_defines(p),['-DBADGE_SPRINGBOARD_GESTURE_DIAGNOSTICS=1']);qualify(recipe(),True)
 def test_strict_boolean(self):
  for value in [None,0,1,'true',[],{}]:
   with self.subTest(value=value),self.assertRaises(ValueError):selected({'springboard_gesture_diagnostics':value})
   with self.subTest(value=value),self.assertRaises(ValueError):qualify({},value)
 def test_requires_transport(self):
  for value in [None,False,1,'true']:
   with self.subTest(value=value),self.assertRaises(ValueError):selected({'springboard_gesture_diagnostics':True,'native_performance_trace':value})
 def test_scope(self):
  for app in ['settings','default','wifi_settings','timecard']:
   with self.subTest(app=app),self.assertRaises(ValueError):qualify(recipe(),True,app)
 def test_schema_mutations(self):
  for field,value in recipe()['springboard_gesture_diagnostics'].items():
   for bad in [None,True if type(value) is int else 'wrong']:
    r=recipe();r['springboard_gesture_diagnostics'][field]=bad
    with self.subTest(field=field,bad=bad),self.assertRaises(ValueError):qualify(r,True)
 def test_missing_receipt(self):
  r=recipe();del r['springboard_gesture_diagnostics']
  with self.assertRaises(ValueError):qualify(r,True)
 def test_unselected_leak(self):
  with self.assertRaises(ValueError):qualify(recipe(),False)
  r=recipe();r['build_defines']=[]
  with self.assertRaises(ValueError):qualify(r,False)
 def test_missing_define(self):
  r=recipe();r['build_defines'].remove(FLAG)
  with self.assertRaises(ValueError):qualify(r,True)
 def test_profile(self):
  for field,bad in [('watch_native_ui',False),('time_profile','historical')]:
   r=recipe();r[field]=bad
   with self.subTest(field=field),self.assertRaises(ValueError):qualify(r,True)
 def test_trace_recipe(self):
  r=recipe();r['native_performance_trace']['enabled']=False
  with self.assertRaises(ValueError):qualify(r,True)
  r=recipe();r['build_defines'].remove('-DPORTABLE_PERFORMANCE_TRACE')
  with self.assertRaises(ValueError):qualify(r,True)
 def test_cli_and_only_springboard_forward(self):
  source=(ROOT/'scripts/build_badge_prototype.py').read_text();ast.parse(source)
  self.assertIn("if a.springboard_gesture_diagnostics and not a.native_performance_trace:p.error(",source)
  forwards=[line for line in source.splitlines() if '*([\'--springboard-gesture-diagnostics\']' in line]
  self.assertEqual(len(forwards),1);self.assertIn('build_portable_springboard.py',forwards[0])
 def test_actual_wrapper_from_record(self):
  source=(ROOT/'scripts/test_badge_prototype_execution.py').read_text();ast.parse(source)
  self.assertIn('*gesture_runtime_defines(record)',source)
  self.assertIn("qualify_gesture(gesture_record,gesture_diagnostics and gesture_app=='springboard',gesture_app)",source)
  self.assertIn('BADGE_SPRINGBOARD_GESTURE_DIAGNOSTICS',(ROOT/'tests/badge_prototype_execution.cpp').read_text())
if __name__=='__main__':unittest.main()

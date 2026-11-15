"""Native WiFi recipe and complete selected-product evidence must fail closed."""
import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
import package_badge_prototype as package
import wifi_geometry_profile as geometry
import test_badge_quick_evidence_gate as quick

class WifiGeometryEvidence(unittest.TestCase):
 def recipe(self):
  definitions=sorted(geometry.WIFI_NATIVE_FLAGS)
  return {'watch_native_ui':True,'safe_content_native_raster':True,
          'display_rotation':0,'touch_rotation':0,'build_defines':definitions,
          'compile_inputs':{'definitions':definitions.copy()}}
 def reject(self,edit,selected=True):
  recipe=self.recipe();edit(recipe)
  with self.assertRaises(ValueError):geometry.qualify_wifi_geometry_recipe(recipe,selected)
 def test_selected_exact(self):geometry.qualify_wifi_geometry_recipe(self.recipe(),True)
 def test_selected_boolean_contract(self):
  for field in ('watch_native_ui','safe_content_native_raster'):
   for value in (False,0,1,None,'true',[]):
    with self.subTest(field=field,value=value):self.reject(lambda r:r.update({field:value}))
   self.reject(lambda r:r.pop(field))
  for selected in (None,0,1,'true'):
   with self.assertRaises(ValueError):geometry.qualify_wifi_geometry_recipe(self.recipe(),selected)
 def test_every_compiler_flag_required_and_aligned(self):
  for flag in geometry.WIFI_NATIVE_FLAGS:
   self.reject(lambda r:r['build_defines'].remove(flag))
   self.reject(lambda r:r['compile_inputs']['definitions'].remove(flag))
  self.reject(lambda r:r['compile_inputs']['definitions'].append('-DUNRECORDED'))
  standalone=self.recipe();standalone.pop('compile_inputs');geometry.qualify_wifi_geometry_recipe(standalone,True)
  for value in (None,'-DPORTABLE_WIFI_NATIVE_PRESENTATION',[1],{}):
   self.reject(lambda r:r.update(build_defines=value))
   self.reject(lambda r:r['compile_inputs'].update(definitions=value))
 def test_rotations_are_exact_integer_zero(self):
  for field in ('display_rotation','touch_rotation'):
   self.reject(lambda r:r.pop(field))
   for value in (1,90,180,270,-1,False,True,0.0,'0',None):
    with self.subTest(field=field,value=value):self.reject(lambda r:r.update({field:value}))
 def test_omitted_historical_recipes(self):
  for recipe in ({},{'compile_inputs':{'definitions':['-DPORTABLE_SAFE_VIEWPORT']}},
                 {'build_defines':['-DPORTABLE_SAFE_VIEWPORT'],'display_rotation':0}):
   geometry.qualify_wifi_geometry_recipe(recipe,False)
  for field in ('watch_native_ui','safe_content_native_raster'):
   for value in (True,1,0,'false',None):
    with self.assertRaises(ValueError):geometry.qualify_wifi_geometry_recipe({field:value},False)
  for recipe in ({'build_defines':['-DPORTABLE_WIFI_NATIVE_PRESENTATION']},
                 {'compile_inputs':{'definitions':['-DPORTABLE_WIFI_NATIVE_PRESENTATION']}}):
   with self.assertRaises(ValueError):geometry.qualify_wifi_geometry_recipe(recipe,False)

class WifiExecutionEvidence(unittest.TestCase):
 def fixture(self):
  b,_,_,_,_,r=quick.QuickEvidence().fixture()
  b['native_wifi_geometry']=True;r.update(native_wifi_geometry=True,case_count=46,results=[{'target_index':i} for i in range(46)])
  r['foreground_recipes']['wifi_settings']=WifiGeometryEvidence().recipe()
  return b,r
 def gate(self,b,r):package.qualify_execution_receipt(b,r,False,b['store_files'])
 def test_exact_46_and_historical_42(self):
  b,r=self.fixture();self.assertEqual(package.execution_case_count(b),46);self.gate(b,r)
  b,_,_,_,_,r=quick.QuickEvidence().fixture();self.assertEqual(package.execution_case_count(b),42);self.gate(b,r)
  b['native_wifi_geometry']=False;r['native_wifi_geometry']=False;self.gate(b,r)
 def test_requires_quick_cohort(self):
  for value in (False,None,0,1,'true'):
   b,r=self.fixture();b['quick_actions']=value
   with self.assertRaises(ValueError):package.execution_case_count(b)
  b,r=self.fixture();b.pop('quick_actions')
  with self.assertRaises(ValueError):package.execution_case_count(b)
 def test_selection_must_match_and_be_boolean(self):
  for field_owner in ('build','receipt'):
   for value in (False,None,0,1,'true'):
    b,r=self.fixture();(b if field_owner=='build' else r)['native_wifi_geometry']=value
    with self.assertRaises(ValueError):self.gate(b,r)
  b,r=self.fixture();r.pop('native_wifi_geometry')
  with self.assertRaises(ValueError):self.gate(b,r)
 def test_missing_duplicate_and_mistyped_cases(self):
  for index in range(46):
   b,r=self.fixture();r['results'].pop(index)
   with self.assertRaises(ValueError):self.gate(b,r)
  for value in (42,45,47,46.0,True,None):
   b,r=self.fixture();r['case_count']=value
   with self.assertRaises(ValueError):self.gate(b,r)
  for value in (0,True,45.0,'45',46):
   b,r=self.fixture();r['results'][-1]['target_index']=value
   with self.assertRaises(ValueError):self.gate(b,r)
 def test_native_recipe_selection_cannot_be_hidden(self):
  edits=[lambda r:r.pop('compile_inputs'),lambda r:r.update(watch_native_ui=False),lambda r:r.update(safe_content_native_raster=False),
         lambda r:r['build_defines'].remove('-DPORTABLE_RF_NATIVE_CANVAS'),lambda r:r.update(touch_rotation=90),
         lambda r:r['compile_inputs']['definitions'].remove('-DPORTABLE_WIFI_NATIVE_PRESENTATION')]
  for edit in edits:
   b,r=self.fixture();edit(r['foreground_recipes']['wifi_settings'])
   with self.assertRaises(ValueError):self.gate(b,r)
 def test_selected_native_wifi_does_not_require_native_alarm_geometry(self):
  b,r=self.fixture();b['native_alarm_geometry']=False;r['native_alarm_geometry']=False;self.gate(b,r)

if __name__=='__main__':unittest.main()

import copy,sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from package_badge_prototype import execution_case_count,qualify_execution_receipt,ALARM_MEMBERS,ALARM_FOREGROUND_APPS,ALARM_PROFILE,ALARM_NATIVE_FLAGS
class Evidence(unittest.TestCase):
 def candidate(self):return {'store_files':{'file_browser.elf':{}},'supported_app_increment':{'files':True},'foreground_idle':{'enabled':True,'apps':['file_browser']}}
 def test_files_requires_23_cases(self):self.assertEqual(execution_case_count(self.candidate()),23)
 def test_flag_cannot_hide_packaged_files(self):
  b=self.candidate();b['supported_app_increment']['files']=False
  with self.assertRaises(Exception):execution_case_count(b)
 def test_flag_cannot_claim_absent_files(self):
  b=self.candidate();b['store_files']={}
  with self.assertRaises(Exception):execution_case_count(b)
 def test_files_requires_foreground(self):
  b=self.candidate();b['foreground_idle']['enabled']=False
  with self.assertRaises(Exception):execution_case_count(b)
 def test_files_requires_actual_idle_recipe(self):
  b=self.candidate();b['foreground_idle']['apps']=[]
  with self.assertRaises(Exception):execution_case_count(b)
 def test_existing_profiles_preserved(self):
  self.assertEqual(execution_case_count({'store_files':{}}),11)
  self.assertEqual(execution_case_count({'store_files':{},'foreground_idle':{'enabled':True}}),21)
class AlarmEvidence(unittest.TestCase):
 def candidate(self):
  return {'native_alarms':True,'clock_edit_authority':True,'alarm_profile':ALARM_PROFILE,'native_daily':True,
   'source_pins':{'exact':'pin'},'store_files':{name:{} for name in [*ALARM_MEMBERS,'file_browser.elf']},
   'supported_app_increment':{'files':True},'providers':{'alarm-service':{'profile':ALARM_PROFILE}},
   'foreground_idle':{'enabled':True,'apps':sorted(ALARM_FOREGROUND_APPS)}}
 def receipt(self,b):
  return {'sanitize':False,'source_pins':b['source_pins'],'passed':True,'store_files':b['store_files'],
   'foreground':True,'native_daily':True,'native_alarms':True,'clock_edit_authority':True,'alarm_profile':ALARM_PROFILE,'case_count':34,
   'alarm_service_recipe':{'profile':ALARM_PROFILE,'points_selected':False},
   'foreground_recipes':{name:{} for name in ALARM_FOREGROUND_APPS},
   'results':[{'target_index':i} for i in range(34)]}
 def rejects_build(self,edit):
  b=self.candidate();edit(b)
  with self.assertRaises(ValueError):execution_case_count(b)
 def rejects_receipt(self,edit):
  b=self.candidate();r=self.receipt(b);edit(r)
  with self.assertRaises(ValueError):qualify_execution_receipt(b,r,False,b['store_files'])
 def test_exact_alarm_cohort(self):
  b=self.candidate();self.assertEqual(execution_case_count(b),34)
  qualify_execution_receipt(b,self.receipt(b),False,b['store_files'])
 def test_members_cannot_be_hidden_or_missing(self):
  self.rejects_build(lambda b:b.update(native_alarms=False))
  for name in ALARM_MEMBERS:
   self.rejects_build(lambda b,name=name:b['store_files'].pop(name))
 def test_required_profiles(self):
  for field in ('native_daily','native_alarms','clock_edit_authority'):
   self.rejects_build(lambda b,field=field:b.update({field:False}))
  for profile in (None,'native-utc-points',True):
   self.rejects_build(lambda b,profile=profile:b.update(alarm_profile=profile))
  self.rejects_build(lambda b:b['providers']['alarm-service'].update(profile='points'))
  self.rejects_build(lambda b:b['foreground_idle'].update(enabled=False))
  self.rejects_build(lambda b:b['store_files'].pop('file_browser.elf'))
  self.rejects_build(lambda b:b.update(native_alarms='true'))
 def test_all_nine_apps_exactly_once(self):
  for name in ALARM_FOREGROUND_APPS:
   self.rejects_build(lambda b,name=name:b['foreground_idle']['apps'].remove(name))
  self.rejects_build(lambda b:b['foreground_idle']['apps'].append('alarms'))
  self.rejects_build(lambda b:b['foreground_idle']['apps'].__setitem__(0,'other'))
 def test_execution_selection_and_profile_match(self):
  for value in (False,1,'true'):
   self.rejects_receipt(lambda r,value=value:r.update(native_alarms=value))
  self.rejects_receipt(lambda r:r.pop('native_alarms'))
  self.rejects_receipt(lambda r:r.update(alarm_profile='native-utc-points'))
  self.rejects_receipt(lambda r:r.pop('alarm_profile'))
  self.rejects_receipt(lambda r:r['alarm_service_recipe'].update(points_selected=True))
  self.rejects_receipt(lambda r:r['alarm_service_recipe'].update(profile='wrong'))
 def test_34_complete_unique_indices_and_recipes(self):
  self.rejects_receipt(lambda r:r.update(case_count=23))
  self.rejects_receipt(lambda r:r['results'].pop())
  self.rejects_receipt(lambda r:r['results'][-1].update(target_index=0))
  self.rejects_receipt(lambda r:r['results'].append({'target_index':34}))
  for name in ALARM_FOREGROUND_APPS:
   self.rejects_receipt(lambda r,name=name:r['foreground_recipes'].pop(name))
 def test_native_geometry_requires_alarms_and_boolean(self):
  for value in (1,'true',None):
   self.rejects_build(lambda b,value=value:b.update(native_alarm_geometry=value))
  with self.assertRaises(ValueError):execution_case_count({'store_files':{},'native_alarm_geometry':True})
 def test_native_geometry_receipt_and_each_recipe_bound(self):
  b=self.candidate();b['native_alarm_geometry']=True;r=self.receipt(b)
  with self.assertRaises(ValueError):qualify_execution_receipt(b,r,False,b['store_files'])
  r['native_alarm_geometry']=True
  for name in ('alarms','countdown'):
   r['foreground_recipes'][name]['native_alarm_geometry']=True
   r['foreground_recipes'][name]['compile_inputs']={'definitions':sorted(ALARM_NATIVE_FLAGS)}
  qualify_execution_receipt(b,r,False,b['store_files'])
  for name in ('alarms','countdown'):
   for value in (False,1,'true',None):
    bad=copy.deepcopy(r);bad['foreground_recipes'][name]['native_alarm_geometry']=value
    with self.assertRaises(ValueError):qualify_execution_receipt(b,bad,False,b['store_files'])
  for name in ('alarms','countdown'):
   for flag in ALARM_NATIVE_FLAGS:
    bad=copy.deepcopy(r);bad['foreground_recipes'][name]['compile_inputs']['definitions'].remove(flag)
    with self.assertRaises(ValueError):qualify_execution_receipt(b,bad,False,b['store_files'])
  for value in (False,1,'true',None):
   bad=copy.deepcopy(r);bad['native_alarm_geometry']=value
   with self.assertRaises(ValueError):qualify_execution_receipt(b,bad,False,b['store_files'])
 def test_unselected_native_macro_rejected(self):
  self.rejects_receipt(lambda r:r['foreground_recipes']['alarms'].update(compile_inputs={'definitions':['-DPORTABLE_ALARM_NATIVE_PRESENTATION']}))
 def test_legacy_receipt_omission_unchanged(self):
  for foreground,count in ((False,11),(True,21)):
   b={'source_pins':{},'store_files':{},'foreground_idle':{'enabled':foreground}}
   r={'source_pins':{},'store_files':{},'sanitize':False,'passed':True,'foreground':foreground,
      'results':[{'target_index':i} for i in range(count)]}
   qualify_execution_receipt(b,r,False,{})
   r['native_alarms']=True
   with self.assertRaises(ValueError):qualify_execution_receipt(b,r,False,{})
if __name__=='__main__':unittest.main()

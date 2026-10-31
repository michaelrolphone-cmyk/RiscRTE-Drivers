#!/usr/bin/env python3
import copy,importlib.util,itertools,json,sys,unittest
from argparse import Namespace
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'scripts'))
import points_product_profile as p
class Selection(unittest.TestCase):
 def test_dependency_closure(self):
  keys=['include_timecard','native_alarms','foreground_idle','quick_actions','productivity']
  for bits in itertools.product([False,True],repeat=len(keys)):
   a=Namespace(include_points=True,**dict(zip(keys,bits)))
   if all(bits):p.validate_selection(a)
   else:
    with self.subTest(bits=bits),self.assertRaises(ValueError):p.validate_selection(a)
  p.validate_selection(Namespace(include_points=False))
 def app(self):
  return {'id':'points_in_time','file_name':'points_in_time.elf','requires':[]},{'profile':'points-current-badge','grant_bindings':{'storage.key-value':[1,5],'storage.preferences':[1]},'service_occurrence_access':False,'clock_edit_authority':False,'service_projection_context':True,'foreground_idle':{'enabled':True},'build_defines':['-D'+x for x in ['POINTS_SERVICE_CONTEXT','ALARM_SERVICE_TAGGED_V2','PORTABLE_NATIVE_SCENE','PORTABLE_QUICK_ATOMIC_PREFERENCES','PORTABLE_FOREGROUND_ALARM_SLEEP']]}
 def test_app_authority(self):
  m,r=self.app();p.qualify_app(m,r)
  for key,value in [('service_occurrence_access',True),('clock_edit_authority',True),('service_projection_context',False),('profile','legacy')]:
   bad=copy.deepcopy(r);bad[key]=value
   with self.subTest(key=key),self.assertRaises(ValueError):p.qualify_app(m,bad)
  for namespaces in [[1],[1,4],[1,5,4],[0,5]]:
   bad=copy.deepcopy(r);bad['grant_bindings']['storage.key-value']=namespaces
   with self.subTest(namespaces=namespaces),self.assertRaises(ValueError):p.qualify_app(m,bad)
  for flag in r['build_defines']:
   bad=copy.deepcopy(r);bad['build_defines'].remove(flag)
   with self.subTest(flag=flag),self.assertRaises(ValueError):p.qualify_app(m,bad)
 def test_service_policy(self):
  policy={k:{'namespace':n,'access':a} for k,(n,a) in p.POLICY.items()};r={'bound_keys':policy,'profile':p.PROFILE,'points_selected':True,'points_context_selected':True,'defines':['ALARM_NATIVE_MIXED_POINTS','POINTS_IN_TIME_SERVICE','POINTS_SERVICE_CONTEXT','ALARM_SERVICE_CLOCK_EDIT_V2']};p.qualify_service(r,policy)
  for key in policy:
   for mutation in ('omit','access','namespace','bool','float'):
    bad=copy.deepcopy(policy)
    if mutation=='omit':del bad[key]
    elif mutation=='access':bad[key]['access']='write'
    elif mutation=='bool':bad[key]['namespace']=True
    elif mutation=='float':bad[key]['namespace']=float(bad[key]['namespace'])
    else:bad[key]['namespace']=0
    with self.subTest(key=key,mutation=mutation),self.assertRaises(ValueError):p.qualify_service({**r,'bound_keys':bad},bad)
  with self.assertRaises(ValueError):p.qualify_service({**r,'defines':r['defines']+['ALARM_NATIVE_SCHEDULES_ONLY']},policy)
 def test_grants(self):
  grants=[{'capability':c,'api':1,'instance_id':i} for c,ids in [('storage.key-value',[1,5]),('storage.preferences',[1])] for i in ids];p.qualify_grants(grants)
  for cap,i in [('storage.key-value',4),('runtime.realtime-control',0),('storage.app-data',61)]:
   with self.subTest(cap=cap),self.assertRaises(ValueError):p.qualify_grants(grants+[{'capability':cap,'api':1,'instance_id':i}])
 def test_explicit_successor_lock(self):
  root=ROOT/'boards/waveshare-esp32-s3-touch-amoled-1.75c/prototype';base=json.loads((root/'sources.lock.json').read_text());successor=json.loads((root/'points-sources.lock.json').read_text())
  self.assertEqual({k for k in base if base[k]!=successor[k]},{'system','utilities','productivity'})
  self.assertEqual(successor['home_utilities'],base['utilities'])
  self.assertEqual(set(successor)-set(base),{'home_utilities'})
  self.assertIn("'points-sources.lock.json' if a.include_points else 'sources.lock.json'",(ROOT/'scripts/build_badge_prototype.py').read_text())
if __name__=='__main__':unittest.main()

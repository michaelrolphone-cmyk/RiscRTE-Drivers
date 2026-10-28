import copy,pathlib,sys,unittest
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'scripts'))
from build_badge_prototype import qualify_sleep_timer_controls
class Controls(unittest.TestCase):
 def record(self):return {'sleep_defaults':{'idle_timer_editable':True,'deep_timer_editable':True,'sleep_backend':False},'preferences':{'instance':1,'idle_timer_key':'sleep_idle','deep_timer_key':'sleep_deep'},'build_defines':['-DPORTABLE_SETTINGS_BOTH_TIMERS']}
 def test_selected(self):qualify_sleep_timer_controls(self.record())
 def test_wrong_contract(self):
  for section,key,values in [('sleep_defaults','idle_timer_editable',[False,None]),('sleep_defaults','deep_timer_editable',[False,None]),('sleep_defaults','sleep_backend',[True,None]),('preferences','instance',[0,2,None]),('preferences','idle_timer_key',['sleep_deep',None]),('preferences','deep_timer_key',['sleep_idle',None])]:
   for value in values:
    r=self.record();r[section][key]=value
    with self.assertRaises(ValueError):qualify_sleep_timer_controls(r)
 def test_wrong_source(self):
  for flags in [[],['-DPORTABLE_SETTINGS_IDLE_TIMER'],['-DPORTABLE_SETTINGS_BOTH_TIMERS','-DPORTABLE_SETTINGS_IDLE_TIMER']]:
   r=self.record();r['build_defines']=flags
   with self.assertRaises(ValueError):qualify_sleep_timer_controls(r)
 def test_absent_receipt(self):
  with self.assertRaises(ValueError):qualify_sleep_timer_controls({})
if __name__=='__main__':unittest.main()

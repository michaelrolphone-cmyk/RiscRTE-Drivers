import copy,importlib.util,json,unittest
from pathlib import Path
R=Path(__file__).resolve().parents[1];s=importlib.util.spec_from_file_location('profile',R/'scripts/temporary_quick_profile.py');m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
class Contract(unittest.TestCase):
 def good(self):
  native={'selected':True,'ownership':'temporary-app-local','resident_architecture_complete':False,'preference_capability':'storage.preferences','preference_api':1,'preference_instance':1,'completed_pending_frame_before_submit':True,'sliced_frame_identity':True,'wifi_action':'setup-launch','wifi_destination':'wifi_settings.elf','radio_control_grants':False}
  flags=['PORTABLE_QUICK_ACTIONS','PORTABLE_QUICK_PENDING_FRAME','PORTABLE_QUICK_NATIVE_GEOMETRY','PORTABLE_QUICK_CAPABILITY_SAFE','PORTABLE_QUICK_ATOMIC_PREFERENCES','PORTABLE_PREFERENCE_STORE','PORTABLE_FRAME_RECEIPTS','PORTABLE_RASTER_SNAPSHOT']
  return {'quick_actions':True,'native_copied_quick':native,'build_defines':['-D'+x for x in flags]},{'requires':[{'capability':'storage.preferences','api':1}]},[{'capability':'storage.preferences','api':1,'instance_id':1}]
 def test_selected(self):m.qualify(*self.good(),True)
 def test_unselected(self):m.qualify({'quick_actions':False},{'requires':[]},[],False)
 def test_bad_receipt(self):
  for key,value in self.good()[0]['native_copied_quick'].items():
   for replacement in [None,0,'wrong']:
    with self.subTest(key=key,value=replacement):
     r,manifest,g=self.good();r['native_copied_quick'][key]=replacement
     with self.assertRaises(ValueError):m.qualify(r,manifest,g,True)
 def test_missing_define(self):
  for flag in self.good()[0]['build_defines']:
   r,manifest,g=self.good();r['build_defines'].remove(flag)
   with self.assertRaises(ValueError):m.qualify(r,manifest,g,True)
 def test_authority(self):
  for cap in ['rtc.clock','bluetooth.hci','net.wifi']:
   r,manifest,g=self.good();g.append({'capability':cap,'api':1,'instance_id':0})
   with self.assertRaises(ValueError):m.qualify(r,manifest,g,True)
  for namespace in [0,2,61]:
   r,manifest,g=self.good();g[0]['instance_id']=namespace
   with self.assertRaises(ValueError):m.qualify(r,manifest,g,True)
 def test_no_implicit_enablement(self):
  r,manifest,g=self.good()
  with self.assertRaises(ValueError):m.qualify(r,manifest,g,False)
  r['quick_actions']=False
  with self.assertRaises(ValueError):m.qualify(r,manifest,g,False)
 def test_required_service(self):
  r,manifest,g=self.good();manifest['requires']=[]
  with self.assertRaises(ValueError):m.qualify(r,manifest,g,True)
if __name__=='__main__':unittest.main()

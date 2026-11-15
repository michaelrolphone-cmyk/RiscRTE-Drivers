#!/usr/bin/env python3
import copy,hashlib,json,sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from time_sync_profile import qualify
class Profile(unittest.TestCase):
 def setUp(self):
  self.header=b'canonical';self.pin='a'*40;self.server='pool.ntp.org'
  self.record={'build_defines':['-DPORTABLE_WIFI_TIME_SYNC','-DPORTABLE_TIME_SYNC_SERVER='+json.dumps(self.server),'-DPORTABLE_NATIVE_CUSTODY_FENCE','-DPORTABLE_FOREGROUND_ALARM_SLEEP','-DALARM_SERVICE_CLOCK_EDIT_V2'],
   'time_server_sync':{'capability':'net.time-sample@1','server':self.server,'timeout_ms':5000,'sample_max_age_ms':1000,'transport':'unauthenticated SNTP','timezone_changed':False,'clock_commit':'shared AlarmClockEditV2 + native seed/readback','header_source':'sdk/driver/RiscTimeSampleV1.h','header_commit':self.pin,'header_sha256':hashlib.sha256(self.header).hexdigest()}}
  self.manifest={'requires':[{'capability':c,'api':v} for c,v in [('net.time-sample',1),('runtime.realtime-control',1),('alarm.service',2)]]}
 def run_profile(self):qualify(self.record,self.manifest,True,self.server,self.pin,self.header)
 def test_valid_and_disabled(self):
  self.run_profile();qualify({'build_defines':[]},{},False,'',None,b'')
 def test_each_policy_field_is_bound(self):
  for key in self.record['time_server_sync']:
   old=self.record['time_server_sync'][key];self.record['time_server_sync'][key]=None
   with self.assertRaises(ValueError):self.run_profile()
   self.record['time_server_sync'][key]=old
 def test_each_define_required(self):
  old=self.record['build_defines']
  for i in range(len(old)):
   self.record['build_defines']=old[:i]+old[i+1:]
   with self.assertRaises(ValueError):self.run_profile()
  self.record['build_defines']=old
 def test_grants_not_invented(self):
  old=copy.deepcopy(self.manifest)
  for i in range(3):
   self.manifest=copy.deepcopy(old);self.manifest['requires'].pop(i)
   with self.assertRaises(ValueError):self.run_profile()
  self.manifest=copy.deepcopy(old);self.manifest['requires']*=2
  with self.assertRaises(ValueError):self.run_profile()
if __name__=='__main__':unittest.main()

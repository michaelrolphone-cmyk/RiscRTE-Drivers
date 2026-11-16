#!/usr/bin/env python3
"""Product receipts must preserve shared staged default, not override saved intent."""
import copy, pathlib, sys, unittest
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'scripts'))
from build_badge_prototype import qualify_sleep_defaults
class SleepDefaults(unittest.TestCase):
 def records(self,seconds):
  r={'sleep_defaults':{'mode':'hybrid','idle_seconds':seconds,'persisted_preferences_take_precedence':True}}
  return r,copy.deepcopy(r)
 def test_all_accepted_idle_defaults(self):
  for seconds in range(5,3601):qualify_sleep_defaults(*self.records(seconds),seconds)
 def test_mode_preference_and_timer_drift_rejected(self):
  for which in range(2):
   for key,values in [('mode',['deep','light',None]),('idle_seconds',[0,60,None]),('persisted_preferences_take_precedence',[False,None])]:
    for value in values:
     r=list(self.records(15));r[which]['sleep_defaults'][key]=value
     with self.assertRaises(ValueError):qualify_sleep_defaults(*r,15)
 def test_missing_receipt_rejected(self):
  for which in range(2):
   r=list(self.records(15));r[which]={}
   with self.assertRaises(ValueError):qualify_sleep_defaults(*r,15)
if __name__=='__main__':unittest.main()

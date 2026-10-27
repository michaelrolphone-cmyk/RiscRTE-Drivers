import sys,unittest,copy
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parents[1]/'scripts'))
from build_badge_prototype import qualify_native_performance as qualify, qualify_performance_sdk, PERFORMANCE_SDK_COMMIT, PERFORMANCE_SDK_OBJECTS
class Selection(unittest.TestCase):
 def test_selected_and_refusals(self):
  good={'build_defines':['-DPORTABLE_PERFORMANCE_TRACE'],'native_performance_trace':{'enabled':True,'runtime_commit':'exact','capture_command_ascii_hex':'706572660a','unsolicited_trace_output':False,'profile_changed':False}}
  qualify(good,True,'exact');qualify({},False,'exact')
  for key,value in [('enabled','true'),('runtime_commit','old'),('capture_command_ascii_hex','70657266'),('unsolicited_trace_output',True),('profile_changed',True)]:
   bad=copy.deepcopy(good);bad['native_performance_trace'][key]=value
   with self.assertRaises(ValueError):qualify(bad,True,'exact')
  bad=copy.deepcopy(good);bad['build_defines']=[]
  with self.assertRaises(ValueError):qualify(bad,True,'exact')
  with self.assertRaises(ValueError):qualify(good,False,'exact')
 def test_canonical_sdk_successor_identity_and_refusals(self):
  identity={'canonical_commit':PERFORMANCE_SDK_COMMIT,'checkout_commit':'successor','checkout_clean':True,'canonical_ancestor':True,'exact_git_objects':PERFORMANCE_SDK_OBJECTS}
  for field in ['runtime_commit','runtime']:
   good={field:PERFORMANCE_SDK_COMMIT,'runtime_sdk_identity':identity};qualify_performance_sdk(good,'successor',field)
   for key,value in [('canonical_commit','wrong'),('checkout_commit','wrong'),('checkout_clean',1),('canonical_ancestor',1),('exact_git_objects',{'sdk':'wrong'})]:
    bad=copy.deepcopy(good);bad['runtime_sdk_identity'][key]=value
    with self.assertRaises(ValueError):qualify_performance_sdk(bad,'successor',field)
   with self.assertRaises(ValueError):qualify_performance_sdk({field:PERFORMANCE_SDK_COMMIT},'successor',field)
if __name__=='__main__':unittest.main()

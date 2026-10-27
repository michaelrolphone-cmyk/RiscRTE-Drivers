import copy,hashlib,pathlib,sys,tempfile,unittest
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'scripts'))
from settings_evidence_gate import qualify_settings,FIXTURES,EXPECTED
class Gate(unittest.TestCase):
 def receipt(self):
  rows=[]
  for scenario in EXPECTED:
   out='SETTINGS_SAVE key=sleep_idle ms=10000 pending_frame=1\nSETTINGS_SAVE key=sleep_deep ms=120000 pending_frame=2\nPASS actual Runtime Settings timers scenario='+scenario+'\n'
   out+=('cancel_Back_reopen_time_Save_preserved=passed NATIVE_SETTINGS_UI explicit_Save PASS actual Runtime navigation target=0' if scenario=='cancel-time' else 'configured_Hybrid=passed PASS actual Runtime HYBRID timed Light then Deep')
   if scenario=='io-retry':out+=' SETTINGS_IO refused actual_Save key=sleep_idle durable_unchanged=1 SETTINGS_IO completed_unconfirmed_frame explicit_retry_next=1'
   rows.append({'target_index':0,'scenario':scenario,'returncode':0,'stderr':'','stdout':out})
  return {'qualification':'badge-settings-timers','schema':1,'passed':True,'sanitize':False,'settings_ui':True,'expected_regression':False,'short_prepare':False,'case_count':4,'target_build_sha256':'target','source_pins':{'system':'pin'},'store_files':{'settings.elf':'hash'},'results':rows,'source_sha256':{'source':'digest'},'fixture_sha256':dict.fromkeys(FIXTURES,'digest')}
 def check(self,r,root='.',verify=False):qualify_settings({'source_pins':{'system':'pin'}},r,False,{'settings.elf':'hash'},'target',root,verify_sources=verify)
 def test_valid(self):self.check(self.receipt())
 def test_header_mismatch(self):
  for key,value in [('qualification','other'),('schema',2),('passed',False),('sanitize',True),('settings_ui',False),('expected_regression',True),('short_prepare',True),('case_count',3),('target_build_sha256','mixed'),('source_pins',{}),('store_files',{}),('source_sha256',{}),('fixture_sha256',{})]:
   r=self.receipt();r[key]=value
   with self.assertRaises(ValueError):self.check(r)
 def test_missing_reordered(self):
  for indexes in [[0,1,2],[1,0,2,3],[0,0,2,3]]:
   r=self.receipt();r['results']=[r['results'][i] for i in indexes]
   with self.assertRaises(ValueError):self.check(r)
 def test_failed_cases(self):
  for i in range(4):
   for key,value in [('returncode',1),('stderr','fault'),('stdout','PASS'),('target_index',1)]:
    r=self.receipt();r['results'][i][key]=value
    with self.assertRaises(ValueError):self.check(r)
 def test_required_markers(self):
  for i in range(4):
   for marker in ['SETTINGS_SAVE key=sleep_idle','SETTINGS_SAVE key=sleep_deep','pending_frame=','PASS actual Runtime Settings timers']:
    r=self.receipt();r['results'][i]['stdout']=r['results'][i]['stdout'].replace(marker,'missing')
    with self.assertRaises(ValueError):self.check(r)
 def test_source_hashes(self):
  with tempfile.TemporaryDirectory() as d:
   root=pathlib.Path(d);r=self.receipt();h=hashlib.sha256(b'input').hexdigest()
   for n in FIXTURES:p=root/n;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(b'input')
   p=root/'source';p.write_bytes(b'input');r['source_sha256']={str(p):h};r['fixture_sha256']=dict.fromkeys(FIXTURES,h);self.check(r,root,True)
   p.write_bytes(b'changed')
   with self.assertRaises(ValueError):self.check(r,root,True)
if __name__=='__main__':unittest.main()

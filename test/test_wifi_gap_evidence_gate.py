import copy,pathlib,sys,unittest,tempfile,hashlib
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'scripts'))
from wifi_gap_evidence_gate import qualify_wifi_gap,EXPECTED,FIXTURES
class Gate(unittest.TestCase):
 def setUp(self):
  self.b={'native_wifi_geometry':True,'source_pins':{'system':'pin'}};self.files={'wifi_settings.elf':'hash'}
  self.r={'qualification':'wifi-report-gap','schema':1,'passed':True,'sanitize':False,'native_wifi_geometry':True,'target_build_sha256':'target','source_pins':self.b['source_pins'].copy(),'store_files':self.files.copy(),'case_count':4,'source_sha256':{'source':'digest'},'fixture_sha256':dict.fromkeys(FIXTURES,'digest'),'results':[{'target_index':43,'fault':k,'returncode':0,'stderr':'','stdout':'WIFI_REPORT_GAP kind='+k+' WIFI_GAP fresh_physical_key_exactly_once=passed retained_draft=passed PASS actual Runtime installed native WiFi report gap PASS actual Runtime native WiFi case=43 namespace6_writes=0 returning_Light=3 completed_restore=passed'} for k in EXPECTED]}
 def verify(self,root='.',sources=False):qualify_wifi_gap(self.b,self.r,False,self.files,'target',root,verify_sources=sources)
 def test_valid(self):self.verify()
 def test_header(self):
  for k,v in [('qualification','wrong'),('schema',2),('passed',False),('sanitize',True),('native_wifi_geometry',False),('target_build_sha256','bad'),('source_pins',{}),('store_files',{}),('case_count',3),('source_sha256',{}),('fixture_sha256',{})]:
   original=self.r[k];self.r[k]=v
   with self.assertRaises(ValueError):self.verify()
   self.r[k]=original
 def test_fault_order(self):
  self.r['results'][1]=self.r['results'][0]
  with self.assertRaises(ValueError):self.verify()
 def test_failures(self):
  for i in range(4):
   for k,v in [('returncode',1),('stderr','error'),('target_index',0),('stdout','PASS')]:
    original=self.r['results'][i][k];self.r['results'][i][k]=v
    with self.assertRaises(ValueError):self.verify()
    self.r['results'][i][k]=original
 def test_each_invariant(self):
  for marker in ['WIFI_REPORT_GAP kind=','fresh_physical_key_exactly_once=passed','retained_draft=passed','PASS actual Runtime installed native WiFi report gap','PASS actual Runtime native WiFi case=43','namespace6_writes=0','returning_Light=3','completed_restore=passed']:
   original=self.r['results'][0]['stdout'];self.r['results'][0]['stdout']=original.replace(marker,'missing')
   with self.assertRaises(ValueError):self.verify()
   self.r['results'][0]['stdout']=original
 def test_source_mutation(self):
  with tempfile.TemporaryDirectory() as d:
   p=pathlib.Path(d);h=hashlib.sha256(b'input').hexdigest()
   for n in FIXTURES:f=p/n;f.parent.mkdir(parents=True,exist_ok=True);f.write_bytes(b'input')
   f=p/'source';f.write_bytes(b'input');self.r['source_sha256']={str(f):h};self.r['fixture_sha256']=dict.fromkeys(FIXTURES,h);self.verify(p,True)
   f.write_bytes(b'change')
   with self.assertRaises(ValueError):self.verify(p,True)
if __name__=='__main__':unittest.main()

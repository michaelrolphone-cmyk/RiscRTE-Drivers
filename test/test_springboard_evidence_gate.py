import copy,pathlib,sys,unittest
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'scripts'))
from springboard_evidence_gate import qualify_springboard,EXPECTED
class Gate(unittest.TestCase):
 def setUp(self):
  self.b={'source_pins':{'runtime':'pin'},'store_files':{'default.elf':'hash'}}
  self.r={'qualification':'springboard-report-gap','schema':1,'passed':True,'sanitize':False,'expected_retained':False,'target_build_sha256':'0'*64,'source_pins':copy.deepcopy(self.b['source_pins']),'store_files':copy.deepcopy(self.b['store_files']),'case_count':4,'source_sha256':{'source':'digest'},'results':[{'target_index':0,'fault':k,'returncode':0,'stderr':'','stdout':'CST_RECOVERY kind='+k+' actual_Settings_Save_and_fresh_Home=passed PASS actual Runtime navigation target=0'} for k in EXPECTED]}
 def verify(self):qualify_springboard(self.b,self.r,False,self.b['store_files'],'0'*64,False)
 def test_positive(self):self.verify()
 def test_scope_and_lane(self):
  for key,value in [('qualification','other'),('schema',2),('passed',False),('sanitize',True),('expected_retained',True),('case_count',3)]:
   original=self.r[key];self.r[key]=value
   with self.assertRaises(ValueError):self.verify()
   self.r[key]=original
 def test_mixed_cohort(self):
  for key,value in [('source_pins',{}),('store_files',{}),('target_build_sha256','bad')]:
   original=self.r[key];self.r[key]=value
   with self.assertRaises(ValueError):self.verify()
   self.r[key]=original
 def test_missing_fault(self):
  self.r['results'].pop()
  with self.assertRaises(ValueError):self.verify()
 def test_duplicate_fault(self):
  self.r['results'][1]=self.r['results'][0]
  with self.assertRaises(ValueError):self.verify()
 def test_failed_run(self):
  self.r['results'][0]['returncode']=1
  with self.assertRaises(ValueError):self.verify()
 def test_stderr(self):
  self.r['results'][0]['stderr']='sanitizer failure'
  with self.assertRaises(ValueError):self.verify()
 def test_missing_recovery(self):
  self.r['results'][0]['stdout']='PASS actual Runtime navigation target=0'
  with self.assertRaises(ValueError):self.verify()
 def test_missing_provenance(self):
  self.r['source_sha256']={}
  with self.assertRaises(ValueError):self.verify()
if __name__=='__main__':unittest.main()

import copy,json,pathlib,unittest,sys
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/"scripts"))
from hybrid_evidence_gate import qualify_hybrid
Q=pathlib.Path(__file__).resolve().parent
class Gate(unittest.TestCase):
 def setUp(self):
  self.b={'source_pins':{'runtime':'pin'},'store_files':{'default.elf':'hash'}}
  self.r={'qualification':'badge-hybrid-execution','schema':1,'passed':True,'sanitize':False,'expected_regression':False,'settings_ui':False,'short_prepare':True,'target_build_sha256':'0'*64,'source_pins':copy.deepcopy(self.b['source_pins']),'store_files':copy.deepcopy(self.b['store_files']),'case_count':8,'fixture_sha256':{'fixture':'digest'},'source_sha256':{'source':'digest'},'results':[{'target_index':i,'scenario':n,'returncode':0,'stdout':'PASS actual Runtime HYBRID'} for i,n in __import__('hybrid_evidence_gate').EXPECTED]}
 def verify(self):qualify_hybrid(self.b,self.r,False,self.b['store_files'],self.r['target_build_sha256'],Q/'repos/drivers',False)
 def test_positive_shape(self):self.verify()
 def test_missing_or_wrong_qualification(self):
  for key,value in [('qualification','other'),('schema',2),('passed',False),('sanitize',True),('settings_ui',True),('expected_regression',True),('short_prepare',False),('case_count',7)]:
   original=self.r[key];self.r[key]=value
   with self.assertRaises(ValueError):self.verify()
   self.r[key]=original
 def test_wrong_cohort(self):
  for key,value in [('source_pins',{}),('store_files',{}),('target_build_sha256','bad')]:
   original=self.r[key];self.r[key]=value
   with self.assertRaises(ValueError):qualify_hybrid(self.b,self.r,False,self.b['store_files'],'0'*64,Q,False)
   self.r[key]=original
 def test_incomplete_scenarios(self):
  self.r['results']=self.r['results'][:-1]
  with self.assertRaises(ValueError):self.verify()
 def test_duplicate_scenario(self):
  self.r['results'][1]=copy.deepcopy(self.r['results'][0])
  with self.assertRaises(ValueError):self.verify()
 def test_failed_scenario(self):
  self.r['results'][0]['returncode']=1
  with self.assertRaises(ValueError):self.verify()
 def test_missing_runtime_pass(self):
  self.r['results'][0]['stdout']=''
  with self.assertRaises(ValueError):self.verify()
 def test_missing_provenance(self):
  self.r['fixture_sha256']={}
  with self.assertRaises(ValueError):self.verify()
if __name__=='__main__':unittest.main()

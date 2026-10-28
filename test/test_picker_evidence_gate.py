import copy,pathlib,sys,unittest
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'scripts'))
from picker_evidence_gate import qualify_picker,FIXTURES,EXPECTED
class Gate(unittest.TestCase):
 def receipt(self):
  results=[{'target_index':i,'returncode':0,'stderr':'','stdout':f'PASS actual Runtime raw-CST picker case={i} categories=31 faces=17179869183 position_changes=1 picker_frames=1 preview_renders=1 cache_bytes=1 allocations=2 neutral_gap=1 cancellation_return=passed modeled_hardware_only'} for i in range(4)]
  results.append({'target_index':4,'returncode':0,'stderr':'','stdout':'PICKER_FAULT typed terminal stream result after actual next\nPASS expected-retained raw-CST picker typed terminal; Runtime failed/retained, no later hardware I/O, no ordinary UI-return claim'})
  return {'qualification':'badge-picker-execution','schema':1,'passed':True,'sanitize':False,'native_alpha_host_experiment':None,'target_build_sha256':'target','source_pins':{'watch':'pin'},'store_files':{'default.elf':'hash'},'case_count':5,'cases':EXPECTED[:],'results':results,'source_sha256':{'source':'hash'},'fixture_sha256':dict.fromkeys(FIXTURES,'hash')}
 def check(self,r):qualify_picker({'source_pins':{'watch':'pin'}},r,False,{'default.elf':'hash'},'target','.',False)
 def test_valid(self):self.check(self.receipt())
 def test_identity(self):
  for key,value in [('qualification','other'),('schema',2),('passed',False),('sanitize',True),('native_alpha_host_experiment',{'commit':'candidate'}),('source_pins',{}),('target_build_sha256','other'),('store_files',{})]:
   r=self.receipt();r[key]=value
   with self.assertRaises(ValueError):self.check(r)
 def test_incomplete(self):
  for field in ['cases','results']:
   r=self.receipt();r[field]=r[field][:-1]
   with self.assertRaises(ValueError):self.check(r)
 def test_case_failure(self):
  for i in range(5):
   for key,value in [('returncode',1),('stderr','fault'),('stdout','PASS')]:
    r=self.receipt();r['results'][i][key]=value
    with self.assertRaises(ValueError):self.check(r)
 def test_face_and_fault_coverage(self):
  for i,old,new in [(0,'faces=17179869183','faces=1'),(1,'categories=31','categories=1'),(2,'allocations=2','allocations=1'),(3,'neutral_gap=1','neutral_gap=0'),(0,'position_changes=1','position_changes=0')]:
   r=self.receipt();r['results'][i]['stdout']=r['results'][i]['stdout'].replace(old,new)
   with self.assertRaises(ValueError):self.check(r)
 def test_retained_not_ordinary(self):
  r=self.receipt();r['results'][4]['stdout']+='\nPASS actual Runtime raw-CST picker'
  with self.assertRaises(ValueError):self.check(r)
 def test_source_provenance(self):
  for key in ['source_sha256','fixture_sha256']:
   r=self.receipt();r[key]={}
   with self.assertRaises(ValueError):self.check(r)
if __name__=='__main__':unittest.main()

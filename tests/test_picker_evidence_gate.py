import pathlib,sys,unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
import picker_evidence_gate as g

class PickerGateTests(unittest.TestCase):
 def good(self):
  r=dict(qualification='badge-picker-execution',schema=1,passed=True,sanitize=False,native_alpha_host_experiment=None,timed_stress=False,coverage_contract='state-acknowledged all34 identities',target_build_sha256='target',source_pins={'watch':'pin'},store_files={'default.elf':'hash'},case_count=5,cases=g.EXPECTED.copy(),source_sha256={'source':'hash'},fixture_sha256={k:'hash' for k in g.FIXTURES})
  r['results']=[dict(target_index=i,returncode=0,stderr='',stdout=f'PASS actual Runtime raw-CST picker case={i} categories=31 faces={(1<<34)-1} position_changes=1 picker_frames=1 preview_renders=1 cache_bytes=1 allocations=2 neutral_gap=1 cancellation_return=passed modeled_hardware_only') for i in range(4)]
  r['results'].append(dict(target_index=4,returncode=0,stderr='',stdout='PICKER_FAULT typed terminal stream result after actual next\nPASS expected-retained raw-CST picker typed terminal; Runtime failed/retained, no later hardware I/O, no ordinary UI-return claim'))
  return r
 def run_gate(self,r):g.qualify_picker({'source_pins':{'watch':'pin'}},r,False,{'default.elf':'hash'},'target',ROOT,False)
 def test_valid(self):self.run_gate(self.good())
 def test_stress_cannot_qualify_even_with_all_faces(self):
  for key,value in [('timed_stress',True),('timed_stress',None),('coverage_contract','phase-dependent stress only'),('coverage_contract',None)]:
   with self.subTest(key=key,value=value):
    r=self.good();r[key]=value
    with self.assertRaises(ValueError):self.run_gate(r)
 def test_incomplete_cases(self):
  for n in range(5):
   r=self.good();r['results']=r['results'][:n];r['case_count']=n
   with self.assertRaises(ValueError):self.run_gate(r)
 def test_missing_face_or_terminal_evidence(self):
  for i in range(5):
   r=self.good();r['results'][i]['stdout']='PASS'
   with self.assertRaises(ValueError):self.run_gate(r)
if __name__=='__main__':unittest.main()

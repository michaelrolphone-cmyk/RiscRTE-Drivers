import copy,json,sys,tempfile,unittest,hashlib
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
import package_badge_prototype as p
import local_quick_evidence_gate as local
import execution_source_equivalence as eq
from test_badge_quick_evidence_gate import QuickEvidence
from test_temporary_quick_profile import Contract
class LocalPackage(unittest.TestCase):
 def fixture(self):
  v=list(QuickEvidence().fixture()[:5]);r,m,g=Contract().good()
  r.update(compile_inputs={'definitions':r['build_defines'].copy()},grant_bindings={'storage.preferences':[1]},sliced_native_frames=True,quick_radios=False)
  v[0].update(springboard_quick_actions=True,sliced_native_frames=True)
  v[3]['app_capabilities'].append({'manifest':'springboard.json','grants':g})
  m['type']='application';v[4]['springboard.json']=m
  return v+[r]
 def test_selected_exact(self):p.qualify_quick_native(*self.fixture())
 def test_omitted(self):p.qualify_quick_native(*QuickEvidence().fixture()[:5])
 def test_selection_and_authority(self):
  edits=[lambda v:v[0].update(springboard_quick_actions=1),lambda v:v[0].update(sliced_native_frames=False),lambda v:v[0].update(springboard_quick_actions=False),lambda v:v[3]['app_capabilities'].append(v[3]['app_capabilities'][-1]),lambda v:v[3]['app_capabilities'][-1]['grants'][0].update(api=True),lambda v:v[4]['springboard.json'].update(optional=[{'capability':'storage.preferences','api':1}]),lambda v:v[5]['compile_inputs'].update(definitions=[]),lambda v:v[5].update(quick_radios=True),lambda v:v[5]['native_copied_quick'].update(completed_pending_frame_before_submit=False)]
  for edit in edits:
   with self.subTest(edit=edit):
    v=self.fixture();edit(v)
    with self.assertRaises(ValueError):p.qualify_quick_native(*v)
 def test_missing_local_evidence_rejected(self):
  for r in ({},{'passed':True},{'qualification':'temporary-springboard-local-quick','temporary_local_ownership':True,'resident_architecture_complete':False,'passed':True,'sanitize':False,'case_count':4}):
   with self.assertRaises(ValueError):local.qualify(self.fixture()[0],r,False,Path('.'))
class Equivalence(unittest.TestCase):
 def test_exact_transform_only(self):
  with tempfile.TemporaryDirectory() as d:
   path=Path(d)/'scripts/test_badge_prototype_execution.py';path.parent.mkdir()
   old="common=['-O1','-g','-Wall']\nassert safety\n";new=old.replace("['-O1','-g'","['-pipe','-O1','-g1'");path.write_text(new)
   h=lambda x:hashlib.sha256(x.encode()).hexdigest()
   r={'source_sha256':{str(path):h(old)}};receipt=Path(d)/'receipt.json';receipt.write_text(json.dumps(r))
   e={'schema':1,'qualification':'host-runner-debug-pipe-only','execution_receipt_sha256':p.sha(receipt),'source_path':str(path),'historical_source':old,'historical_sha256':h(old),'current_sha256':h(new)}
   self.assertTrue(eq.verify(receipt,r,e))
   with self.assertRaises(ValueError):eq.verify(receipt,r)
   for key,value in [('execution_receipt_sha256','wrong'),('historical_source',old+'pass'),('current_sha256','wrong')]:
    bad=dict(e);bad[key]=value
    with self.assertRaises(ValueError):eq.verify(receipt,r,bad)
   path.write_text(new.replace('assert safety','pass'));e['current_sha256']=p.sha(path)
   with self.assertRaises(ValueError):eq.verify(receipt,r,e)
if __name__=='__main__':unittest.main()

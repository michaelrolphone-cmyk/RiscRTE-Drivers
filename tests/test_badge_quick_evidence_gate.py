import copy,sys,unittest,tempfile
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
import package_badge_prototype as p
import test_badge_files_evidence_gate as legacy
class QuickEvidence(unittest.TestCase):
 def fixture(self):
  b=legacy.AlarmEvidence().candidate();b.update(quick_actions=True,home={'sha256':'exact-home'})
  flags=sorted(p.QUICK_FLAGS|p.QUICK_PREFERENCE_FLAGS)
  recipe={'defines':flags+['-flto'],'compile_inputs':{'definitions':flags},'sha256':'exact-home'}
  home={'native_quick_actions':True,'artifacts':{'default':recipe}}
  proof={'quick_actions':True,'preference_store':{'enabled':True,'atomic_key_value':False}}
  pref={'capability':'storage.preferences','api':1,'instance_id':1};kv={'capability':'storage.key-value','api':1,'instance_id':1}
  boot={'app_capabilities':[{'manifest':'default.json','grants':[pref,kv]},{'manifest':'settings.json','grants':[kv.copy()]}]}
  manifests={'default.json':{'type':'application','requires':[{'capability':'storage.preferences','api':1}],'optional':[]},'settings.json':{'type':'application','requires':[]}}
  r=legacy.AlarmEvidence().receipt(b);r.update(quick_actions=True,native_quick_actions=True,host_preference_store=1,host_atomic_key_value=0,home_recipe=copy.deepcopy(recipe),case_count=42,results=[{'target_index':i} for i in range(42)])
  return b,proof,home,boot,manifests,r
 def gate(self,values):p.qualify_quick_native(*values[:5])
 def reject(self,edit):
  values=self.fixture();edit(*values)
  with self.assertRaises(ValueError):self.gate(values)
 def test_exact(self):
  v=self.fixture();self.gate(v);self.assertEqual(p.execution_case_count(v[0]),42);p.qualify_execution_receipt(v[0],v[5],False,v[0]['store_files'])
 def test_boolean_and_mixed_native(self):
  for value in (False,1,'true',None):
   self.reject(lambda b,n,h,g,m,r,value=value:n.update(quick_actions=value))
  for value in (1,'true',None):
   self.reject(lambda b,n,h,g,m,r,value=value:b.update(quick_actions=value))
  self.reject(lambda b,n,h,g,m,r:n.pop('quick_actions'))
  self.reject(lambda b,n,h,g,m,r:b.update(quick_actions=False))
  self.reject(lambda b,n,h,g,m,r:b.pop('quick_actions'))
 def test_broker_mode_exact(self):
  for field in ('enabled','atomic_key_value'):
   for value in (None,1,'true',False if field=='enabled' else True):
    self.reject(lambda b,n,h,g,m,r,field=field,value=value:n['preference_store'].update({field:value}))
  self.reject(lambda b,n,h,g,m,r:n.pop('preference_store'))
 def test_prerequisites(self):
  for field in ('native_alarms','clock_edit_authority'):
   self.reject(lambda b,n,h,g,m,r,field=field:b.pop(field))
 def test_home_exact_flags_receipt(self):
  for flag in p.QUICK_FLAGS|p.QUICK_PREFERENCE_FLAGS:
   self.reject(lambda b,n,h,g,m,r,flag=flag:h['artifacts']['default']['defines'].remove(flag))
  self.reject(lambda b,n,h,g,m,r:h['artifacts']['default']['compile_inputs']['definitions'].append('-DUNRECORDED'))
  self.reject(lambda b,n,h,g,m,r:h['artifacts']['default'].update(sha256='wrong'))
  for value in (False,1,None,'true'):
   self.reject(lambda b,n,h,g,m,r,value=value:h.update(native_quick_actions=value))
 def test_home_authority(self):
  for key,value in [('api',2),('api',True),('instance_id',4),('instance_id',True)]:
   self.reject(lambda b,n,h,g,m,r,key=key,value=value:g['app_capabilities'][0]['grants'][0].update({key:value}))
  self.reject(lambda b,n,h,g,m,r:g['app_capabilities'][0]['grants'].append(g['app_capabilities'][0]['grants'][0].copy()))
  self.reject(lambda b,n,h,g,m,r:g['app_capabilities'][1]['grants'].append(g['app_capabilities'][0]['grants'][0].copy()))
  for i in (0,1):self.reject(lambda b,n,h,g,m,r,i=i:g['app_capabilities'][i]['grants'].__setitem__(-1,{'capability':'storage.key-value','api':1,'instance_id':4}))
 def test_mandatory_and_foreground(self):
  self.reject(lambda b,n,h,g,m,r:m['default.json'].update(requires=[],optional=[{'capability':'storage.preferences','api':1}]))
  self.reject(lambda b,n,h,g,m,r:m['default.json']['requires'][0].update(api=2))
  self.reject(lambda b,n,h,g,m,r:m['settings.json']['requires'].append({'capability':'storage.preferences','api':1}))
  self.reject(lambda b,n,h,g,m,r:m.pop('default.json'))
 def test_execution_mutations(self):
  edits=[lambda r:r.update(case_count=42.0),lambda r:r['results'][0].update(target_index=False),lambda r:r.update(case_count=34),lambda r:r['results'].pop(),lambda r:r['results'][-1].update(target_index=0),lambda r:r.update(quick_actions=1),lambda r:r.pop('quick_actions'),lambda r:r.update(host_preference_store=0),lambda r:r.update(host_preference_store=True),lambda r:r.update(host_atomic_key_value=1),lambda r:r.pop('home_recipe'),lambda r:r.update(native_quick_actions=False),lambda r:r['home_recipe'].update(sha256='other')]
  for edit in edits:
   b,n,h,g,m,r=self.fixture();edit(r)
   with self.assertRaises(ValueError):p.qualify_execution_receipt(b,r,False,b['store_files'])
 def test_historical_alarm_count(self):
  b=legacy.AlarmEvidence().candidate();self.assertEqual(p.execution_case_count(b),34)
  p.qualify_execution_receipt(b,legacy.AlarmEvidence().receipt(b),False,b['store_files'])
class QuickDocumentation(unittest.TestCase):
 def test_omission_is_byte_unchanged(self):
  with tempfile.TemporaryDirectory() as directory:
   out=Path(directory);(out/'INSTALL.md').write_bytes(b'original\n')
   p.write_quick_actions_docs(out,{'store_files':{}})
   self.assertEqual((out/'INSTALL.md').read_bytes(),b'original\n');self.assertFalse((out/'QUICK-ACTIONS.md').exists())
 def test_selected_notes_and_boundaries(self):
  with tempfile.TemporaryDirectory() as directory:
   out=Path(directory)
   for name in ('INSTALL.md','TEST-ON-DEVICE.md','CAPABILITIES.md','REPRODUCE.md'):(out/name).write_text('inherited')
   p.write_quick_actions_docs(out,QuickEvidence().fixture()[0]);text=(out/'QUICK-ACTIONS.md').read_text()
   for phrase in ('native Runtime rebuild','namespace1','atomic key-value','not a hardware flashlight','Settings has no brightness editor','release-then-fail','full42-case','physical results pending','SYS_OUT GPIO3','Releasing normally','Retained unknown results forbid further drawing','retained-unknown path can paint feedback', 'PRESENT_COMPLETE before commit-turn preference capture/validation or mutators', 'including fast-tap toggles', 'never rebases stale intent', 'causes no preference write', 'without a post-retention repaint', 'possibly\n   brief SAVE NOT CONFIRMED'):
    self.assertIn(phrase,text)
   for name in ('INSTALL.md','TEST-ON-DEVICE.md','CAPABILITIES.md','REPRODUCE.md'):
    self.assertIn('QUICK-ACTIONS.md',(out/name).read_text());self.assertTrue((out/name).read_text().endswith('inherited'))
class QuickAdmission(unittest.TestCase):
 def test_exact_mode_and_refusals(self):
  built=QuickEvidence().fixture()[0];keys=['missing-preference-broker','missing-preference-grant','unsupported-preference-api']
  good={'quick_actions':True,'negative_cases':keys};p.qualify_quick_admission(built,good)
  for value in (None,False,1,'true'):
   with self.assertRaises(ValueError):p.qualify_quick_admission(built,{**good,'quick_actions':value})
  for key in keys:
   with self.assertRaises(ValueError):p.qualify_quick_admission(built,{**good,'negative_cases':[k for k in keys if k!=key]})
  p.qualify_quick_admission({}, {})
  with self.assertRaises(ValueError):p.qualify_quick_admission({},good)
if __name__=='__main__':unittest.main()

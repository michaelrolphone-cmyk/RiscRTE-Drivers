import copy,json,sys,tempfile,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
import package_badge_prototype as p
from test_badge_quick_evidence_gate import QuickEvidence
class TimecardPackage(unittest.TestCase):
 def fixture(self):
  b,n,h,g,m,r=QuickEvidence().fixture()
  manifest={'type':'application','id':'timecard','file_name':'timecard.elf','requires':[{'capability':c,'api':1} for c in ['storage.key-value','storage.app-data','storage.preferences']],'optional':[{'capability':'board.battery','api':1}]}
  flags=sorted(p.TIMECARD_QUICK_FLAGS);pins={name:{'commit':str(i)*40} for i,name in enumerate(['productivity','system','runtime','utilities'],1)}
  rec={'profile':{'profile':'timecard-current-badge','quick_actions_selected':True,'snapshot_selected':False,'quick_preference_store':{'completed_pending_frame_before_submit':True}},'manifest':copy.deepcopy(manifest),'build_defines':flags,'compile_inputs':{'definitions':flags.copy()},'foreground_idle':{'enabled':True},'clock_edit_authority':False,'source_repositories':{name:{'commit':pin['commit'],'dirty':False} for name,pin in pins.items()},'grant_bindings':{c:[1] for c in ['storage.key-value','storage.app-data','storage.preferences']}}
  b.update(include_timecard=True,timecard_profile=rec,source_pins=pins)
  b['store_files'].update({'timecard.elf':{},'timecard.json':{}});b['foreground_idle']['apps'].append('timecard')
  m['timecard.json']=manifest;g['app_capabilities'].append({'manifest':'timecard.json','grants':[{'capability':c,'api':1,'instance_id':1} for c in rec['grant_bindings']]+[{'capability':'board.battery','api':1,'instance_id':4}]})
  return b,n,h,g,m,r
 def gate(self,v):p.qualify_quick_native(*v[:5])
 def reject(self,edit):
  v=self.fixture();edit(*v)
  with self.assertRaises(ValueError):self.gate(v)
 def test_exact_and_old_matrix(self):
  v=self.fixture();self.gate(v);self.assertEqual(p.execution_case_count(v[0]),42)
 def test_selection_and_members(self):
  for value in [False,None,1,'true']:
   self.reject(lambda b,n,h,g,m,r,value=value:b.update(include_timecard=value))
  self.reject(lambda b,n,h,g,m,r:b['store_files'].pop('timecard.elf'))
  self.reject(lambda b,n,h,g,m,r:b.update(quick_actions=False))
 def test_all_flags_and_exact_replay(self):
  for flag in p.TIMECARD_QUICK_FLAGS:self.reject(lambda b,n,h,g,m,r,flag=flag:b['timecard_profile']['build_defines'].remove(flag))
  self.reject(lambda b,n,h,g,m,r:b['timecard_profile']['compile_inputs']['definitions'].append('-DFAKE'))
 def test_completed_pending_policy_required(self):
  for bad in [False,None,1,'true']:
   self.reject(lambda b,n,h,g,m,r,bad=bad:b['timecard_profile']['profile']['quick_preference_store'].update(completed_pending_frame_before_submit=bad))
 def test_namespaces_and_types(self):
  for cap in ['storage.key-value','storage.app-data','storage.preferences']:
   for bad in [0,2,61,True]:
    self.reject(lambda b,n,h,g,m,r,cap=cap,bad=bad:b['timecard_profile']['grant_bindings'].update({cap:[bad]}))
    self.reject(lambda b,n,h,g,m,r,cap=cap,bad=bad:next(x for x in g['app_capabilities'][-1]['grants'] if x['capability']==cap).update(instance_id=bad))
 def test_not_blanket_foreground_permission(self):
  self.reject(lambda b,n,h,g,m,r:g['app_capabilities'][1]['grants'].append({'capability':'storage.preferences','api':1,'instance_id':1}))
  self.reject(lambda b,n,h,g,m,r:g['app_capabilities'].append(copy.deepcopy(g['app_capabilities'][-1])))
 def test_source_and_manifest_identity(self):
  for name in ['productivity','system','runtime','utilities']:
   self.reject(lambda b,n,h,g,m,r,name=name:b['timecard_profile']['source_repositories'][name].update(dirty=True))
   self.reject(lambda b,n,h,g,m,r,name=name:b['timecard_profile']['source_repositories'][name].update(commit='f'*40))
  self.reject(lambda b,n,h,g,m,r:m['timecard.json'].update(optional=[]))
 def test_no_radio_clock_or_legacy_atomic_authority(self):
  for cap in ['net.wifi','runtime.realtime-control','storage.key-value.atomic']:
   self.reject(lambda b,n,h,g,m,r,cap=cap:g['app_capabilities'][-1]['grants'].append({'capability':cap,'api':1,'instance_id':1}))
 def test_preserve_optional_battery_binding(self):
  self.reject(lambda b,n,h,g,m,r:g['app_capabilities'][-1]['grants'].pop())
  for bad in [0,7,True]:self.reject(lambda b,n,h,g,m,r,bad=bad:g['app_capabilities'][-1]['grants'][-1].update(instance_id=bad))
 def test_timecard_admission_refusals_required(self):
  b=self.fixture()[0];keys={'missing-preference-broker','missing-preference-grant','unsupported-preference-api','timecard-missing-appdata','timecard-zero-private-namespace','timecard-private-namespace-alias','timecard-unsupported-appdata-api','timecard-missing-preference-grant'}
  p.qualify_quick_admission(b,{'quick_actions':True,'negative_cases':list(keys)})
  for key in keys:
   with self.assertRaises(ValueError):p.qualify_quick_admission(b,{'quick_actions':True,'negative_cases':list(keys-{key})})
 def test_selected_docs_correct_authority(self):
  with tempfile.TemporaryDirectory() as d:
   p.write_quick_actions_docs(Path(d),self.fixture()[0]);text=(Path(d)/'QUICK-ACTIONS.md').read_text()
   self.assertIn('Timecard alone receives',text);self.assertNotIn('No foreground app receives',text);self.assertIn('wifi_settings.elf',text)
if __name__=='__main__':unittest.main()

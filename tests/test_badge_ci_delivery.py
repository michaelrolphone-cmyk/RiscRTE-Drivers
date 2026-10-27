import json,sys,tempfile,unittest,hashlib
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
import run_badge_prototype_ci as ci
import recover_badge_ci_sources as recovery
import verify_badge_prototype_image as image
FULL=['--foreground-idle','--include-files','--native-daily','--native-alarms','--native-alarm-geometry','--quick-actions']
class Runner(unittest.TestCase):
 def invoke(self,flags):
  with tempfile.TemporaryDirectory() as d:
   w=Path(d);out=w/'out';calls=[]
   def fake(command,**kwargs):
    c=list(map(str,command));calls.append(c)
    if any(x.endswith('/build_badge_prototype.py') for x in c):
     target=Path(c[c.index('--output')+1]);target.mkdir();(target/'build.json').write_text(json.dumps({'source_pins':{},'store_files':{}}))
    return type('Result',(),{'returncode':0})()
   with patch.object(sys,'argv',['runner','--workspace',str(w),'--idf',str(w/'idf'),'--cc','xtensa','--output',str(out),*flags]),patch.object(ci.subprocess,'run',side_effect=fake),patch.object(ci.subprocess,'check_output',return_value='a'*40),patch.object(ci,'copy_recovery_receipts') as copied:
    ci.main();copy_count=copied.call_count
   return calls,copy_count
 def test_full_pipeline(self):
  calls,copies=self.invoke(FULL);self.assertEqual(copies,1)
  by=lambda name:[c for c in calls if any(x.endswith('/'+name) for x in c)]
  target=by('build_badge_prototype.py')[0]
  for flag in FULL:self.assertIn(flag,target)
  for name in ['test_badge_prototype_execution.py','test_badge_hybrid_execution.py','test_badge_springboard_execution.py','test_badge_settings_execution.py']:
   rows=by(name);self.assertEqual(len(rows),2);self.assertNotIn('--sanitize',rows[0]);self.assertIn('--sanitize',rows[1]);self.assertTrue(all('--foreground' in r for r in rows))
  self.assertTrue(all('--short-prepare' in r for r in by('test_badge_hybrid_execution.py')))
  self.assertTrue(all('--expect-retained' not in r for r in by('test_badge_springboard_execution.py')))
  self.assertTrue(all('--settings-ui' in r for r in by('test_badge_settings_execution.py')))
  self.assertEqual(by('test_badge_wifi_gap_execution.py'),[])
  self.assertEqual(len(by('test_settings_evidence_gate.py')),1)
  self.assertEqual(len(by('test_gpio_key_sleep.py')),1);self.assertEqual(len(by('run_cst9217_touch_test.sh')),1)
  package=next(i for i,c in enumerate(calls) if any(x.endswith('/package_badge_prototype.py') for x in c))
  image_check=next(i for i,c in enumerate(calls) if any(x.endswith('/verify_badge_prototype_image.py') for x in c));self.assertGreater(image_check,package)
  replay=[(i,c) for i,c in enumerate(calls) if '--appdata-image' in c];self.assertEqual(len(replay),2)
  for i,c in replay:self.assertGreater(i,package);self.assertTrue(c[c.index('--appdata-image')+1].endswith('badge-round-prototype/appdata.bin'))
 def test_native_wifi_adds_checks_without_replacing_sleep_navigation(self):
  calls,copies=self.invoke(FULL+['--native-wifi-geometry']);self.assertEqual(copies,1)
  by=lambda name:[c for c in calls if any(x.endswith('/'+name) for x in c)]
  target=by('build_badge_prototype.py')[0]
  self.assertIn('--native-wifi-geometry',target)
  gap=by('test_badge_wifi_gap_execution.py');self.assertEqual(len(gap),2);self.assertNotIn('--sanitize',gap[0]);self.assertIn('--sanitize',gap[1])
  self.assertEqual(len(by('test_wifi_gap_evidence_gate.py')),1)
  for name in ['test_badge_wifi_evidence_gate.py','test_wifi_native.py']:
   self.assertEqual(len(by(name)),1)
  for name in ['test_badge_prototype_execution.py','test_badge_hybrid_execution.py','test_badge_springboard_execution.py','test_badge_settings_execution.py']:
   rows=by(name);self.assertEqual(len(rows),2)
   self.assertNotIn('--sanitize',rows[0]);self.assertIn('--sanitize',rows[1])
  self.assertEqual(len(by('verify_badge_prototype_image.py')),1)
  self.assertEqual(len([c for c in calls if '--appdata-image' in c]),2)
 def test_timecard_selected_pipeline(self):
  calls,copies=self.invoke(FULL+['--native-wifi-geometry','--include-timecard']);self.assertEqual(copies,1)
  by=lambda name:[c for c in calls if any(x.endswith('/'+name) for x in c)]
  target=by('build_badge_prototype.py')[0];self.assertIn('--include-timecard',target);self.assertTrue(target[target.index('--productivity')+1].endswith('/productivity-prototype'))
  for name in ['test_badge_timecard_execution.py','test_native_scene.py','test_quick_pending_frame.py','test_quick_pending_input.py','test_timecard_current_contract.py','test_timecard_current_ui.py']:
   rows=by(name);self.assertEqual(len(rows),2);self.assertNotIn('--sanitize',rows[0]);self.assertIn('--sanitize',rows[1])
  for name in ['test_timecard_current_build.py','test_timecard_current_legacy.py','test_timecard_evidence_gate.py','test_badge_timecard_package.py','test_timecard_product_selection.py']:self.assertEqual(len(by(name)),1)
  for c in by('test_badge_timecard_execution.py'):self.assertIn('--productivity',c);self.assertIn('--foreground',c)
  for name in ['test_badge_prototype_execution.py','test_badge_hybrid_execution.py','test_badge_springboard_execution.py','test_badge_settings_execution.py','test_badge_wifi_gap_execution.py']:self.assertEqual(len(by(name)),2)
 def test_timecard_invalid_profile_before_work(self):
  with tempfile.TemporaryDirectory() as d,patch.object(sys,'argv',['x','--workspace',d,'--idf',d,'--cc','cc','--output',d+'/out','--include-timecard']),patch.object(ci.subprocess,'run') as run:
   with self.assertRaises(SystemExit):ci.main()
   run.assert_not_called()
 def test_native_wifi_invalid_profile_fails_before_work(self):
  with tempfile.TemporaryDirectory() as d,patch.object(sys,'argv',['x','--workspace',d,'--idf',d,'--cc','cc','--output',d+'/out','--native-wifi-geometry']),patch.object(ci.subprocess,'run') as run:
   with self.assertRaises(SystemExit):ci.main()
   run.assert_not_called();self.assertFalse((Path(d)/'out').exists())
 def test_historical_minimal_preserved(self):
  calls,copies=self.invoke([]);self.assertEqual(copies,0)
  self.assertFalse(any('timecard' in Path(x).name for c in calls for x in c))
  self.assertFalse(any(any(x.endswith('/test_badge_hybrid_execution.py') or x.endswith('/test_badge_springboard_execution.py') or x.endswith('/test_badge_settings_execution.py') for x in c) for c in calls))
 def test_invalid_profile_fails_before_work(self):
  with tempfile.TemporaryDirectory() as d,patch.object(sys,'argv',['x','--workspace',d,'--idf',d,'--cc','cc','--output',d+'/out','--native-alarms']),patch.object(ci.subprocess,'run') as run:
   with self.assertRaises(SystemExit):ci.main()
   run.assert_not_called();self.assertFalse((Path(d)/'out').exists())
 def test_required_receipts_cannot_be_omitted(self):
  with tempfile.TemporaryDirectory() as d:
   q=Path(d);target=q/'target';target.mkdir();(target/'build.json').write_text('{"source_pins":{},"store_files":{}}');e=q/'evidence';e.mkdir()
   with self.assertRaises(FileNotFoundError):ci.copy_recovery_receipts(target,q,e)
 def test_receipt_validation_precedes_copy(self):
  with tempfile.TemporaryDirectory() as d:
   q=Path(d);target=q/'target';target.mkdir();(target/'build.json').write_text('{"source_pins":{},"store_files":{}}');e=q/'evidence';e.mkdir();(q/'hybrid-short-normal.json').write_text('{"passed":true}')
   with self.assertRaises(ValueError):ci.copy_recovery_receipts(target,q,e)
   self.assertEqual(list(e.iterdir()),[])
 def test_workflow_profiles_and_permissions(self):
  text=(ci.ROOT/'.github/workflows/badge-prototype.yml').read_text()
  for item in ['historical-minimal','selected-full','max-parallel: 1','permissions:\n  contents: read',"Drivers/gpio_key_sleep/**","Drivers/cst9217_touch/**","--native-wifi-geometry","--include-timecard","matrix.recovery_flags","tests/badge_settings*","test/test_settings_evidence_gate.py"]:self.assertIn(item,text)
  self.assertNotIn('contents: write',text);self.assertNotIn('pull_request_target',text)
class Recovery(unittest.TestCase):
 def test_literal_exact_constants_only(self):
  with tempfile.TemporaryDirectory() as d:
   p=Path(d)/'source.py';p.write_text("PIN='"+'a'*40+"'\n");self.assertEqual(recovery.constants(p,['PIN']),{'PIN':'a'*40})
   p.write_text("PIN='main'\n")
   with self.assertRaises(ValueError):recovery.constants(p,['PIN'])
 def test_missing_constant_rejected(self):
  with tempfile.TemporaryDirectory() as d:
   p=Path(d)/'source.py';p.write_text('x=1')
   with self.assertRaises(ValueError):recovery.constants(p,['PIN'])
 def test_complete_fetch_plan(self):
  with tempfile.TemporaryDirectory() as d:
   root=Path(d);board=root/'drivers/boards/waveshare-esp32-s3-touch-amoled-1.75c';pin='a'*40
   for kind in ['home','prototype']:
    p=board/kind/'sources.lock.json';p.parent.mkdir(parents=True);p.write_text(json.dumps({key:{'repository':'owner/repo','commit':pin} for _,key,k in recovery.ROLES if k==kind}))
   for _,rel,names in recovery.ANCHORS:
    p=root/rel;p.parent.mkdir(parents=True,exist_ok=True);p.write_text('\n'.join(n+'='+repr(pin) for n in names))
   p=root/'drivers/docs/usb-hid-checked-source-lock.json';p.parent.mkdir();p.write_text(json.dumps({'cohorts':{'pr40':{'commit':pin}}}))
   calls=[];receipt=recovery.recover(root,run=lambda c,**kw:calls.append(c),output=lambda c,**kw:pin)
   self.assertEqual(len(receipt['roles']),len(recovery.ROLES));self.assertFalse(receipt['latest_ref_fallback']);self.assertTrue(any(x['role']=='drivers' for x in receipt['extra_refs']))
   for c in calls:
    if 'fetch' in c:self.assertEqual(c[-1],pin);self.assertIn('--depth=1',c)
 def test_timecard_recovery_roles_and_owning_anchors(self):
  with tempfile.TemporaryDirectory() as d:
   root=Path(d);board=root/'drivers/boards/waveshare-esp32-s3-touch-amoled-1.75c';pin='a'*40
   for kind in ['home','prototype']:
    p=board/kind/'sources.lock.json';p.parent.mkdir(parents=True);keys={key for _,key,k in recovery.ROLES if k==kind}|({'productivity'} if kind=='prototype' else set());p.write_text(json.dumps({key:{'repository':'owner/repo','commit':pin} for key in keys}))
   for _,rel,names in recovery.ANCHORS:
    p=root/rel;p.parent.mkdir(parents=True,exist_ok=True);p.write_text('\n'.join(n+'='+repr(pin) for n in names))
   p=root/'drivers/docs/usb-hid-checked-source-lock.json';p.parent.mkdir();p.write_text(json.dumps({'cohorts':{'pr40':{'commit':pin}}}))
   p=root/'productivity-prototype/sdk/timecard-current-profile.json';p.parent.mkdir(parents=True);p.write_text(json.dumps({'authoritative_model_base':pin,'scene_contract_basis':pin,'alarm_sdk':pin}))
   calls=[];receipt=recovery.recover(root,run=lambda c,**kw:calls.append(c),output=lambda c,**kw:pin,include_timecard=True)
   self.assertEqual(len(receipt['roles']),14);self.assertTrue(receipt['include_timecard'])
   self.assertTrue({'watch-alpha-baseline','productivity-prototype','timecard-productivity-baseline','timecard-system-baseline','timecard-alarm-sdk'}<=set(x['role'] for x in receipt['roles']))
   self.assertFalse(receipt['latest_ref_fallback'])
   for c in calls:
    if 'fetch' in c:self.assertEqual(c[-1],pin)
class Image(unittest.TestCase):
 def test_truncated_image_refused(self):
  with tempfile.TemporaryDirectory() as d:
   p=Path(d);(p/'package.json').write_text(json.dumps({'flash_bytes':33554432,'combined':{'sha256':hashlib.sha256(b'bad').hexdigest()}}));(p/'badge-prototype-combined.bin').write_bytes(b'bad')
   with self.assertRaises(AssertionError):image.verify(p)
if __name__=='__main__':unittest.main()

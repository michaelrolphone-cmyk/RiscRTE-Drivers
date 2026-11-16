"""Exact recovery roles remain separate and preserve existing workspaces."""
import json,sys,tempfile,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
import recover_badge_ci_sources as recovery
class Recovery(unittest.TestCase):
 def test_scene_roles_are_independent_and_exact(self):
  proof={'schema':1,'tools':'a'*40,'scene':'b'*40,'before_scene':'c'*40};profile={'scene_contract_basis':'b'*40}
  self.assertEqual(recovery.scene_qualification_anchors(profile,proof),[('scene-qualification-tools','system','a'*40),('timecard-pre-scene-baseline','system','c'*40)])
  for key,value in [('tools','main'),('scene','d'*40),('before_scene','b'*40),('schema',True)]:
   with self.subTest(key=key),self.assertRaises(ValueError):recovery.scene_qualification_anchors(profile,{**proof,key:value})
  with self.assertRaises(ValueError):recovery.scene_qualification_anchors(profile,{**proof,'unknown':1})
 def test_scene_role_directories_refused_before_any_fetch(self):
  for role in ['scene-qualification-tools','timecard-pre-scene-baseline']:
   with self.subTest(role=role),tempfile.TemporaryDirectory() as d:
    root=Path(d);board=root/'drivers/boards/waveshare-esp32-s3-touch-amoled-1.75c'
    for kind in ['home','prototype']:
     p=board/kind/'sources.lock.json';p.parent.mkdir(parents=True);p.write_text('{}')
    p=root/'drivers/docs/badge-source-recovery/sources.json';p.parent.mkdir(parents=True);p.write_text('{}')
    prior=root/role;prior.mkdir();marker=prior/'keep';marker.write_text('owned');calls=[]
    with self.assertRaisesRegex(ValueError,'Fresh workspace roles required'):recovery.recover(root,run=lambda c,**kw:calls.append(c),include_timecard=True)
    self.assertEqual(calls,[]);self.assertEqual(marker.read_text(),'owned')

 def test_literal_exact_constants_only(self):
  with tempfile.TemporaryDirectory() as d:
   p=Path(d)/'source.py';p.write_text("PIN='"+'a'*40+"'\n");self.assertEqual(recovery.constants(p,['PIN']),{'PIN':'a'*40})
   p.write_text("PIN='main'\n")
   with self.assertRaises(ValueError):recovery.constants(p,['PIN'])
 def test_missing_constant_rejected(self):
  with tempfile.TemporaryDirectory() as d:
   p=Path(d)/'source.py';p.write_text('x=1')
   with self.assertRaises(ValueError):recovery.constants(p,['PIN'])
 def test_existing_roles_refused_before_git_and_preserved(self):
  with tempfile.TemporaryDirectory() as d:
   root=Path(d);board=root/'drivers/boards/waveshare-esp32-s3-touch-amoled-1.75c'
   for kind in ['home','prototype']:
    p=board/kind/'sources.lock.json';p.parent.mkdir(parents=True);p.write_text('{}')
   p=root/'drivers/docs/badge-source-recovery/sources.json';p.parent.mkdir(parents=True);p.write_text('{}')
   prior=root/'runtime';prior.mkdir();marker=prior/'uncommitted.txt';marker.write_text('preserve')
   calls=[]
   with self.assertRaisesRegex(ValueError,'Fresh workspace roles required'):
    recovery.recover(root,run=lambda c,**kw:calls.append(c))
   self.assertEqual(calls,[]);self.assertEqual(marker.read_text(),'preserve')
 def test_timecard_baseline_directory_refused_before_git(self):
  with tempfile.TemporaryDirectory() as d:
   root=Path(d);board=root/'drivers/boards/waveshare-esp32-s3-touch-amoled-1.75c'
   for kind in ['home','prototype']:
    p=board/kind/'sources.lock.json';p.parent.mkdir(parents=True);p.write_text('{}')
   p=root/'drivers/docs/badge-source-recovery/sources.json';p.parent.mkdir(parents=True);p.write_text('{}')
   prior=root/'timecard-alarm-sdk';prior.mkdir();marker=prior/'sdk.h';marker.write_text('canonical')
   calls=[]
   with self.assertRaisesRegex(ValueError,'Fresh workspace roles required'):
    recovery.recover(root,run=lambda c,**kw:calls.append(c),include_timecard=True)
   self.assertEqual(calls,[]);self.assertEqual(marker.read_text(),'canonical')

if __name__=='__main__':unittest.main()

"""Only the approved, independently validated additive manifest escapes PR40 diff."""
import importlib.util,json,os,pathlib,shutil,subprocess,tempfile,unittest
ROOT=pathlib.Path(os.environ.get('PRESERVATION_TEST_ROOT',pathlib.Path(__file__).resolve().parents[2]))
VERIFIER=pathlib.Path(os.environ.get('PRESERVATION_VERIFIER',ROOT/'scripts/verify_usb_hid_preservation.py'))
spec=importlib.util.spec_from_file_location('custody_preservation',VERIFIER);mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
class CustodyManifestPreservation(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup);self.root=pathlib.Path(self.tmp.name)
  # Prevent background Git maintenance from racing TemporaryDirectory cleanup.
  # Command configuration covers clone; repository configuration covers later operations.
  subprocess.run(['git','-c','gc.auto=0','-c','maintenance.auto=false','clone',
                  '--config','gc.auto=0','--config','maintenance.auto=false',
                  '-q','--shared','--no-checkout',str(ROOT),str(self.root)],check=True)
  self.baseline=json.loads((ROOT/'docs/usb-hid-checked-source-lock.json').read_text())['cohorts']['pr40']['commit']
  subprocess.run(['git','-C',str(self.root),'checkout',self.baseline,'--','manifest'],check=True)
  for rel in ['manifest/released-drivers.json',mod.ADDITIVE_CUSTODY,'scripts/check_parity.py']:
   dst=self.root/rel;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(ROOT/rel,dst)
  self.custody=json.loads((self.root/mod.ADDITIVE_CUSTODY).read_text())
  for row in self.custody['sources']:shutil.copytree(ROOT/row['path'],self.root/row['path'])
  subprocess.run(['git','-C',str(self.root),'add','manifest'],check=True)
 def test_fixture_disables_background_maintenance(self):
  for key,value in [('gc.auto',b'0'),('maintenance.auto',b'false')]:
   self.assertEqual(subprocess.check_output(['git','-C',str(self.root),'config','--local','--get',key]).strip(),value)
 def verify(self):mod.verify_manifest(self.root,self.baseline)
 def test_exact_staged_addition_validated(self):self.verify()
 def test_additive_byte_mutation(self):
  p=self.root/mod.ADDITIVE_CUSTODY;p.write_bytes(p.read_bytes()+b'\n')
  with self.assertRaises(ValueError):self.verify()
 def test_additive_schema_mutation(self):
  p=self.root/mod.ADDITIVE_CUSTODY;p.write_text('{}')
  with self.assertRaises(ValueError):self.verify()
 def test_additive_executable_mode(self):
  p=self.root/mod.ADDITIVE_CUSTODY;p.chmod(p.stat().st_mode | 0o111)
  with self.assertRaises(ValueError):self.verify()
 def test_missing_additive(self):
  (self.root/mod.ADDITIVE_CUSTODY).unlink()
  with self.assertRaises(ValueError):self.verify()
 def test_symlink_additive(self):
  p=self.root/mod.ADDITIVE_CUSTODY;p.unlink();p.symlink_to(ROOT/mod.ADDITIVE_CUSTODY)
  with self.assertRaises(ValueError):self.verify()
 def test_historical_manifest_drift_still_fails(self):
  files=[f for f in (self.root/'manifest').iterdir() if f.name not in ['released-drivers.json','local-source-custody.json']];self.assertTrue(files)
  p=files[0];p.write_bytes(p.read_bytes()+b'\n')
  with self.assertRaises(subprocess.CalledProcessError):self.verify()
 def test_unapproved_staged_manifest_still_fails(self):
  (self.root/'manifest/other-custody.json').write_text('{}');subprocess.run(['git','-C',str(self.root),'add','manifest'],check=True)
  with self.assertRaises(subprocess.CalledProcessError):self.verify()
 def test_underlying_custody_tree_mutation(self):
  p=self.root/self.custody['sources'][0]['path'];(p/'unapproved.h').write_text('drift')
  with self.assertRaises(ValueError):self.verify()
 def test_additive_already_in_baseline_rejected(self):
  subprocess.run(['git','-C',str(self.root),'-c','user.name=fixture','-c','user.email=fixture@example.invalid','commit','-qm','fixture'],check=True)
  with self.assertRaises(ValueError):mod.verify_manifest(self.root,'HEAD')
 def test_released_inventory_historical_lock_unchanged(self):
  import hashlib
  lock=json.loads((ROOT/'docs/usb-hid-checked-source-lock.json').read_text());p='manifest/released-drivers.json'
  actual=mod.preserved_bytes(p,(ROOT/p).read_bytes());self.assertEqual(hashlib.sha256(actual).hexdigest(),lock['cohorts']['pr40']['files'][p]['sha256'])
if __name__=='__main__':unittest.main()

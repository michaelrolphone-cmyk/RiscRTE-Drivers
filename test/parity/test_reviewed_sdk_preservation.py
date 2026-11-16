"""Exact SDK exceptions retain historical bytes and reject new drift."""
import hashlib,importlib.util,json,pathlib,shutil,subprocess,tempfile,unittest
from unittest.mock import patch
ROOT=pathlib.Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('sdk_preservation',ROOT/'scripts/verify_usb_hid_preservation.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
EVIDENCE={"sdk/driver/RiscBluetoothSensorLinkV1.h":{"sha256":"de1e11824b6bb98859fc31ab39e530dcec41f5fc00b785dc669bef547956d5e2","blob":"1b3e5ca8526427464a70810cb2ac7ca382584c47","producer":"864b076e07bfd5f6912b0bd0cb83df2da6c151fe"},"sdk/driver/RiscDisplayOutputV1.h":{"sha256":"11e747aecadcdfd0b37c79d057208ca9249714cb7482ca32b4a7a3296648c619","blob":"402a1629fc4b89a7150cba6416527563ecaf9643","producer":"a45497f5df164ace12e3b56b615a3eaa76581f2c"},"sdk/driver/RiscPowerKeyNeutralizeV1.h":{"sha256":"5ec7d904b25d8ea8d515ceac88605d1f183cfe48740a30133a16c983d7d7f111","blob":"78dd23e0d7ad8abbf37c08082fee2ba7485ebde2","producer":"a45497f5df164ace12e3b56b615a3eaa76581f2c"},"sdk/driver/RiscTimeSampleV1.h":{"sha256":"3298d2826a55263a5a5478f6f230030588ad7538aca5076d124d70f3e795eb2b","blob":"7e9044715c4a8c8f26739d9e85a5cb2576e4651a","producer":"a45497f5df164ace12e3b56b615a3eaa76581f2c"}}
BASE='90e7a8faae1a6678a24c92c32f95032b2fbe1e24'
class ReviewedSdk(unittest.TestCase):
 def test_exact_published_blob_identities(self):
  self.assertEqual(set(EVIDENCE),set(m.ADDITIVE_SDK)|set(m.EXTENDED_SDK))
  for path,record in EVIDENCE.items():
   data=(ROOT/path).read_bytes()
   self.assertEqual(hashlib.sha256(data).hexdigest(),record['sha256'])
   self.assertEqual(hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest(),record['blob'])
   self.assertEqual(subprocess.check_output(['git','-C',ROOT,'rev-parse',record['producer']+':'+path],text=True).strip(),record['blob'])
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup);self.root=pathlib.Path(self.temp.name)
  subprocess.run(['git','init','-q',self.root],check=True)
  for path in m.EXTENDED_SDK:
   p=self.root/path;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(subprocess.check_output(['git','-C',ROOT,'show',BASE+':'+path]))
  subprocess.run(['git','-C',self.root,'add','.'],check=True)
  subprocess.run(['git','-C',self.root,'-c','user.name=SDK test','-c','user.email=sdk@example.invalid','commit','-qm','historical'],check=True)
  for path in EVIDENCE:
   p=self.root/path;p.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(ROOT/path,p)
 def test_exact_reviewed_set(self):m.verify_sdk(self.root,'HEAD')
 def test_every_header_byte_mutation(self):
  for path in EVIDENCE:
   with self.subTest(path=path):
    p=self.root/path;data=p.read_bytes();p.write_bytes(data+b'\n')
    with self.assertRaises(ValueError):m.verify_sdk(self.root,'HEAD')
    p.write_bytes(data)
 def test_missing_symlink_and_executable_members(self):
  for path in EVIDENCE:
   p=self.root/path;data=p.read_bytes();p.unlink()
   with self.subTest(path=path,mode='missing'),self.assertRaises(OSError):m.verify_sdk(self.root,'HEAD')
   p.symlink_to(ROOT/path)
   with self.subTest(path=path,mode='symlink'),self.assertRaises(ValueError):m.verify_sdk(self.root,'HEAD')
   p.unlink();p.write_bytes(data);p.chmod(0o755)
   with self.subTest(path=path,mode='executable'),self.assertRaises(ValueError):m.verify_sdk(self.root,'HEAD')
   p.chmod(0o644)
 def test_historical_extension_pin_cannot_change(self):
  with patch.object(m,'EXTENDED_SDK',{p:('0'*64,after) for p,(_,after) in m.EXTENDED_SDK.items()}):
   with self.assertRaises(ValueError):m.verify_sdk(self.root,'HEAD')
 def test_addition_cannot_hide_historical_file(self):
  subprocess.run(['git','-C',self.root,'add','.'],check=True)
  subprocess.run(['git','-C',self.root,'-c','user.name=SDK test','-c','user.email=sdk@example.invalid','commit','-qm','not additive'],check=True)
  with self.assertRaises(ValueError):m.verify_sdk(self.root,'HEAD')
 def test_unknown_sdk_file_is_not_excluded(self):
  p=self.root/'sdk/driver/unapproved.h';p.write_text('new');subprocess.run(['git','-C',self.root,'add',p],check=True)
  m.verify_sdk(self.root,'HEAD')
  result=subprocess.run(['git','-C',self.root,'diff','--exit-code','HEAD','--','sdk',*[':(exclude,literal)'+p for p in (*m.ADDITIVE_SDK,*m.EXTENDED_SDK)]],stdout=subprocess.DEVNULL)
  self.assertNotEqual(result.returncode,0)
if __name__=='__main__':unittest.main()

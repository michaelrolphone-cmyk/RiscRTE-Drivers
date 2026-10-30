"""Exact reviewed SDK extensions cannot hide new drift."""
import hashlib
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('preservation_sdk', ROOT/'scripts/verify_usb_hid_preservation.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

class ReviewedSdkPreservation(unittest.TestCase):
    def test_pca_addition_matches_published_producer_content(self):
        # Actual producer f2c6353, not a dynamically accepted header digest.
        path = 'sdk/driver/RiscGpioExpanderV1.h'
        data = (ROOT/path).read_bytes()
        self.assertEqual(module.ADDITIVE_SDK[path], '110f0b141842e7cdc6798519cc5cd61c371cfe0d3e0af59a3f19c12598242f55')
        self.assertEqual(hashlib.sha256(data).hexdigest(), module.ADDITIVE_SDK[path])
        self.assertEqual(hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest(), '1660b562ed1046570542e603e5d891f33af6d786')
        self.assertNotEqual(subprocess.run(['git', '-C', ROOT, 'cat-file', '-e',
            '90e7a8faae1a6678a24c92c32f95032b2fbe1e24:'+path],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode, 0)

    def test_exact_additions_and_extension_reject_every_mutated_header(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            subprocess.run(['git', 'init', '-q', root], check=True)
            for path in module.EXTENDED_SDK:
                target = root/path
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(subprocess.check_output(['git', '-C', ROOT, 'show', '90e7a8faae1a6678a24c92c32f95032b2fbe1e24:'+path]))
            subprocess.run(['git', '-C', root, 'add', '.'], check=True)
            subprocess.run(['git', '-C', root, '-c', 'user.name=SDK test', '-c', 'user.email=sdk@example.invalid', 'commit', '-qm', 'historical'], check=True)
            for path in (*module.ADDITIVE_SDK, *module.EXTENDED_SDK):
                target = root/path
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes((ROOT/path).read_bytes())
            module.verify_sdk(root, 'HEAD')
            for path in (*module.ADDITIVE_SDK, *module.EXTENDED_SDK):
                with self.subTest(path=path):
                    target = root/path
                    exact = target.read_bytes()
                    target.write_bytes(exact+b'\n')
                    with self.assertRaises(AssertionError): module.verify_sdk(root, 'HEAD')
                    target.write_bytes(exact)
            with patch.object(module, 'EXTENDED_SDK', {path: ('0'*64, after) for path, (_, after) in module.EXTENDED_SDK.items()}):
                with self.assertRaises(AssertionError): module.verify_sdk(root, 'HEAD')

if __name__ == '__main__':
    unittest.main()

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

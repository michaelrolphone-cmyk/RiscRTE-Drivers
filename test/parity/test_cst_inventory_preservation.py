"""The CST original entry cannot mask changes to the historical inventory."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('preservation', ROOT/'scripts/verify_usb_hid_preservation.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

class CstInventoryPreservation(unittest.TestCase):
    def test_exact_addition_and_negative_mutations(self):
        path = 'manifest/released-drivers.json'
        data = (ROOT/path).read_bytes()
        lock = json.loads((ROOT/'docs/usb-hid-checked-source-lock.json').read_text())
        expected = lock['cohorts']['pr40']['files'][path]['sha256']
        def accepted(candidate):
            try:
                return hashlib.sha256(module.preserved_bytes(path, candidate)).hexdigest() == expected
            except (ValueError, KeyError, TypeError):
                return False
        self.assertTrue(accepted(data))
        original = json.loads(data)
        mutants = []
        for field in ('version', 'source_tree_sha', 'source_path', 'id'):
            changed = copy.deepcopy(original)
            changed['original_drivers'][-1][field] = 'wrong'
            mutants.append(changed)
        changed = copy.deepcopy(original); changed['original_drivers'].pop(); mutants.append(changed)
        changed = copy.deepcopy(original); changed['original_drivers'].append(module.CST_ENTRY); mutants.append(changed)
        changed = copy.deepcopy(original); changed['drivers'][0]['version'] = '99.0.0'; mutants.append(changed)
        changed = copy.deepcopy(original); changed['parity_source']['source_master_sha'] = '0'*40; mutants.append(changed)
        changed = copy.deepcopy(original); changed['unexpected'] = True; mutants.append(changed)
        for candidate in mutants:
            with self.subTest(candidate=candidate):
                self.assertFalse(accepted((json.dumps(candidate, indent=2)+'\n').encode()))
        self.assertFalse(accepted(data+b'\n'))
        self.assertEqual(module.preserved_bytes('Drivers/usb_hid/driver.c', b'arbitrary'), b'arbitrary')

if __name__ == '__main__':
    unittest.main()

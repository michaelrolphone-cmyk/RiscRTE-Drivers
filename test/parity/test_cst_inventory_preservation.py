"""The CST original entry cannot mask changes to the historical inventory."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('preservation', ROOT/'scripts/verify_usb_hid_preservation.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

class CstInventoryPreservation(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.path = 'manifest/released-drivers.json'
        cls.data = (ROOT/cls.path).read_bytes()
        lock = json.loads((ROOT/'docs/usb-hid-checked-source-lock.json').read_text())
        cls.expected = lock['cohorts']['pr40']['files'][cls.path]['sha256']
        cls.baseline = subprocess.check_output([
            'git', '-C', str(ROOT), 'show',
            lock['cohorts']['pr40']['commit']+':'+cls.path])

    def assert_accepted(self, candidate, expected):
        try:
            preserved = module.preserved_bytes(self.path, candidate)
        except (ValueError, KeyError, TypeError):
            self.assertFalse(expected)
        else:
            # Both consumers must reject drift: the source-lock digest guard and
            # the candidate route's exact historical-byte comparison.
            self.assertEqual(hashlib.sha256(preserved).hexdigest() == self.expected, expected)
            self.assertEqual(preserved == self.baseline, expected)

    def test_exact_addition_preserves_historical_bytes(self):
        self.assertEqual(hashlib.sha256(self.baseline).hexdigest(), self.expected)
        self.assert_accepted(self.data, True)

    def test_inventory_mutations_fail_closed(self):
        original = json.loads(self.data)
        mutants = {}
        for field in ('version', 'source_tree_sha', 'source_path', 'id'):
            changed = copy.deepcopy(original)
            changed['original_drivers'][-1][field] = 'wrong'
            mutants['changed CST '+field] = changed
            changed = copy.deepcopy(original)
            del changed['original_drivers'][-1][field]
            mutants['missing CST '+field] = changed
        changed = copy.deepcopy(original); changed['original_drivers'][-1]['unexpected'] = True; mutants['extra CST field'] = changed
        changed = copy.deepcopy(original); changed['original_drivers'][-1] = dict(reversed(list(changed['original_drivers'][-1].items()))); mutants['reordered CST fields'] = changed
        changed = copy.deepcopy(original); changed['original_drivers'].pop(); mutants['missing CST'] = changed
        changed = copy.deepcopy(original); changed['original_drivers'].append(copy.deepcopy(module.CST_ENTRY)); mutants['duplicate CST'] = changed
        changed = copy.deepcopy(original); changed['original_drivers'].insert(0, changed['original_drivers'].pop()); mutants['reordered CST'] = changed
        changed = copy.deepcopy(original); changed['original_drivers'].insert(0, {'id': 'unexpected'}); mutants['unapproved addition'] = changed
        for section in ('drivers', 'source_only_drivers', 'original_drivers'):
            changed = copy.deepcopy(original); changed[section][0]['version'] = '99.0.0'; mutants['edited '+section] = changed
            changed = copy.deepcopy(original); changed[section].pop(0); mutants['removed '+section] = changed
            changed = copy.deepcopy(original); changed[section][0:2] = reversed(changed[section][0:2]); mutants['reordered '+section] = changed
        changed = copy.deepcopy(original); changed['parity_source']['source_master_sha'] = '0'*40; mutants['edited provenance'] = changed
        changed = copy.deepcopy(original); changed['unexpected'] = True; mutants['extra top-level field'] = changed
        for name, candidate in mutants.items():
            with self.subTest(mutation=name):
                self.assert_accepted((json.dumps(candidate, indent=2)+'\n').encode(), False)

    def test_serialization_mutations_fail_closed(self):
        mutants = {
            'extra newline': self.data+b'\n',
            'missing newline': self.data.rstrip(b'\n'),
            'CRLF': self.data.replace(b'\n', b'\r\n'),
            'compact JSON': json.dumps(json.loads(self.data)).encode(),
            'duplicate key': self.data.replace(b'"schema": 1,', b'"schema": 1,\n  "schema": 1,'),
            'truncated JSON': self.data[:-2],
        }
        for name, candidate in mutants.items():
            with self.subTest(mutation=name):
                self.assert_accepted(candidate, False)

    def test_non_inventory_bytes_are_unchanged(self):
        self.assertEqual(module.preserved_bytes('Drivers/usb_hid/driver.c', b'arbitrary'), b'arbitrary')

if __name__ == '__main__':
    unittest.main()

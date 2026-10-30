"""A later standalone adapter is not a released or physically accepted package."""
import copy
from collections import Counter
import json
from pathlib import Path
import sys
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
import check_parity as parity


class TimeSampleCustodyTests(unittest.TestCase):
    def test_candidate_lifecycle_is_orthogonal_to_local_adapter_custody(self):
        custody = parity.load_custody(ROOT)
        entry = next(e for e in custody['sources'] if e['path'] == 'Drivers/time_sample')
        self.assertEqual(entry['kind'], 'adapter')
        manifest = json.loads((ROOT / entry['path'] / 'manifest.json').read_text())
        self.assertEqual(manifest['status'], 'candidate-read-only-network-time')
        self.assertEqual(manifest['physical_verification'], 'pending')
        self.assertEqual(manifest['requires'], [{'capability': 'platform.time-sample', 'api': 1}])
        self.assertEqual(manifest['provides'], [{'capability': 'net.time-sample', 'api': 1}])
        rows, errors = parity.validate_custody(ROOT, {'schema': 1, 'sources': [entry]}, set(), set())
        self.assertEqual(errors, [])
        self.assertFalse(rows[0]['release_qualified'])
        self.assertFalse(rows[0]['upstream_parity'])
        bad = copy.deepcopy(entry)
        bad.update(kind='candidate', candidate_for=entry['id'])
        with self.assertRaisesRegex(ValueError, 'candidate lineage'):
            parity.validate_custody(ROOT, {'schema': 1, 'sources': [bad]}, set(), set())

    def test_only_later_path_diagnostic_is_removed(self):
        fixtures = Path(__file__).parent / 'fixtures'
        source = json.loads((fixtures / 'source.json').read_text())
        released = parity.unique(json.loads((fixtures / 'release.json').read_text())['drivers'], 'released')
        paths = ['manifest/released-drivers.json', 'manifest/source-trees.json']
        frozen = {p: (ROOT / p).read_bytes() for p in paths}
        args = [ROOT, *(json.loads(frozen[p]) for p in paths), source['source'], released,
                source['pins']['master'], source['pins']['release-index']]
        custody = parity.load_custody(ROOT)
        old = copy.deepcopy(custody)
        old['sources'] = [e for e in old['sources'] if e['path'] != 'Drivers/time_sample']
        before = parity.validate(*args, old)
        after = parity.validate(*args, custody)
        label = 'untracked local source directory: time_sample'
        self.assertEqual(Counter(before['errors'])[label], 1)
        self.assertEqual(Counter(after['errors']), Counter(before['errors']) - Counter([label]))
        self.assertEqual(before['drivers'], after['drivers'])
        self.assertEqual(frozen, {p: (ROOT / p).read_bytes() for p in paths})

    def test_composition_rejects_duplicate_or_conflicting_paths(self):
        combined = parity.load_custody(ROOT)
        for kind in ('adapter', 'support'):
            duplicate = copy.deepcopy(combined)
            entry = copy.deepcopy(duplicate['sources'][-1])
            entry['kind'] = kind
            duplicate['sources'].append(entry)
            with tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                (root / 'manifest').mkdir()
                (root / 'qualification').mkdir()
                (root / 'manifest/local-source-custody.json').write_text(json.dumps(combined))
                (root / 'qualification/local-source-custody.json').write_text(json.dumps({'schema': 1, 'sources': [entry]}))
                with self.subTest(kind=kind), self.assertRaisesRegex(ValueError, 'duplicate local custody path'):
                    parity.validate_custody(ROOT, parity.load_custody(root), set(), {'usb-cdc-acm'})

    def test_loader_fail_closed_and_missing_optional_file(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self.assertIsNone(parity.load_custody(root))
            (root / 'manifest').mkdir()
            historical = ROOT / 'manifest/local-source-custody.json'
            shutil.copy2(historical, root / 'manifest/local-source-custody.json')
            self.assertEqual(parity.load_custody(root), json.loads(historical.read_text()))
            (root / 'qualification').mkdir()
            later = root / 'qualification/local-source-custody.json'
            for invalid in (None, [], {'schema': 2, 'sources': []}, {'schema': 1, 'sources': {}}):
                later.write_text(json.dumps(invalid))
                with self.subTest(invalid=invalid), self.assertRaisesRegex(ValueError, 'custody schema'):
                    parity.load_custody(root)
            later.unlink()
            later.symlink_to(historical)
            with self.assertRaisesRegex(ValueError, 'symlink'):
                parity.load_custody(root)

    def test_later_source_mutations_and_refreshed_hash_fail_closed(self):
        entry = next(e for e in parity.load_custody(ROOT)['sources']
                     if e['path'] == 'Drivers/time_sample')
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            subprocess.run(['git', 'clone', '-q', '--shared', '--no-checkout',
                            str(ROOT), str(root)], check=True)
            target = root / entry['path']
            for mutation in ('add', 'delete', 'content', 'mode'):
                if target.exists(): shutil.rmtree(target)
                shutil.copytree(ROOT / entry['path'], target)
                source = target / 'driver.c'
                if mutation == 'add': (target / 'extra.h').write_text('unexpected')
                elif mutation == 'delete': source.unlink()
                elif mutation == 'content': source.write_text('modified')
                else: source.chmod(source.stat().st_mode ^ 0o111)
                declaration = {'schema': 1, 'sources': [copy.deepcopy(entry)]}
                with self.subTest(mutation=mutation):
                    _, errors = parity.validate_custody(root, declaration, set(), set())
                    self.assertIn('local source custody tree mismatch: ' + entry['path'], errors)
                    declaration['sources'][0]['tree_sha'] = parity.local_tree(target)
                    with self.assertRaisesRegex(ValueError, 'provenance tree mismatch'):
                        parity.validate_custody(root, declaration, set(), set())

    def test_historical_manifest_stays_byte_identical(self):
        import hashlib
        raw = (ROOT / 'manifest/local-source-custody.json').read_bytes()
        self.assertEqual(hashlib.sha256(raw).hexdigest(),
                         '57a0d5895ea938f37e48e1ff397930c6ea9994d84687ad267b993c9f78416911')


if __name__ == '__main__':
    unittest.main()

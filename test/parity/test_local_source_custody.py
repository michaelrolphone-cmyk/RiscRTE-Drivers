"""Path-scoped local custody never exempts upstream or release discrepancies."""
import copy
from collections import Counter
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
import check_parity as parity
ROOT = Path(__file__).resolve().parents[2]
FIXTURES = Path(__file__).parent / 'fixtures'

class LocalCustodyTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        subprocess.run(['git', 'init', '-q', str(self.root)], check=True)
        subprocess.run(['git', '-C', str(self.root), 'config', 'gc.auto', '0'], check=True)
        subprocess.run(['git', '-C', str(self.root), 'config', 'maintenance.auto', 'false'], check=True)
        self.custody = parity.load_custody(ROOT)
        # An ordinary clone only carries advertised refs. Hydrated historical
        # commits may be unreferenced and shallow; import each exact witness.
        for pin in sorted({e['provenance']['commit'] for e in self.custody['sources']}):
            subprocess.run(['git', '-C', str(self.root), 'fetch', '--no-tags', '--depth=1', str(ROOT), pin], check=True)
            self.assertEqual(subprocess.check_output(['git', '-C', str(self.root), 'rev-parse', pin+'^{commit}'], text=True).strip(), pin)
        for entry in self.custody['sources']:
            shutil.copytree(ROOT / entry['path'], self.root / entry['path'])
        self.ids = {'usb-cdc-acm'}

    def audit(self):
        return parity.validate_custody(self.root, self.custody, set(), self.ids)

    def test_all_records_match_actual_bytes_and_immutable_provenance(self):
        rows, errors = self.audit()
        self.assertEqual(errors, [])
        self.assertEqual(len(rows), 9)
        for entry in self.custody['sources']:
            p = entry['provenance']
            recorded = subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', p['commit'] + ':' + p['path']], text=True).strip()
            self.assertEqual(recorded, entry['tree_sha'])
        self.assertTrue(all(not r['upstream_parity'] and not r['release_qualified'] for r in rows))

    def test_add_delete_content_and_mode_tampering_rejected_for_every_path(self):
        original = copy.deepcopy(self.custody)
        for entry in original['sources']:
            path = self.root / entry['path']
            for mutation in ('add', 'delete', 'content', 'mode'):
                with self.subTest(path=path, mutation=mutation):
                    shutil.rmtree(path); shutil.copytree(ROOT / entry['path'], path)
                    f = next(p for p in path.iterdir() if p.name != 'manifest.json')
                    if mutation == 'add': (path / 'extra.h').write_text('extra')
                    if mutation == 'delete': f.unlink()
                    if mutation == 'content': f.write_text('changed')
                    if mutation == 'mode': f.chmod(f.stat().st_mode ^ 0o111)
                    self.assertIn('local source custody tree mismatch: ' + entry['path'], self.audit()[1])
            shutil.rmtree(path); shutil.copytree(ROOT / entry['path'], path)

    def test_symlinks_and_missing_directories_fail_closed(self):
        p = self.root / self.custody['sources'][0]['path']
        shutil.rmtree(p)
        with self.assertRaises(ValueError): self.audit()
        p.symlink_to(ROOT / self.custody['sources'][0]['path'], target_is_directory=True)
        with self.assertRaises(ValueError): self.audit()

    def test_duplicate_unsafe_and_occupied_paths_fail_closed(self):
        original = copy.deepcopy(self.custody)
        for path in ('../escape', '/absolute', 'Drivers/a/b', 'Drivers/a/..', None):
            self.custody = copy.deepcopy(original); self.custody['sources'][0]['path'] = path
            with self.subTest(path=path), self.assertRaises(ValueError): self.audit()
        self.custody = original
        self.custody['sources'].append(copy.deepcopy(self.custody['sources'][0]))
        with self.assertRaises(ValueError): self.audit()
        self.custody['sources'].pop()
        with self.assertRaises(ValueError): parity.validate_custody(self.root, self.custody, {self.custody['sources'][0]['path']}, self.ids)

    def test_support_package_insertion_rejected(self):
        for e in self.custody['sources']:
            if e['kind'] == 'support':
                (self.root / e['path'] / 'manifest.json').write_text('{}')
                self.assertIn('local support became a package: ' + e['path'], self.audit()[1])

    def test_invalid_provenance_schema_and_kind_fail_closed(self):
        original = copy.deepcopy(self.custody)
        for key, value in [('kind','ignored'), ('tree_sha','main'), ('provenance',{}), ('provenance',{'repository':'x/y','commit':'main','path':original['sources'][0]['path']})]:
            self.custody = copy.deepcopy(original); self.custody['sources'][0][key] = value
            with self.subTest(key=key, value=value), self.assertRaises(ValueError): self.audit()
        for value in ([], {'schema':2,'sources':[]}, {'schema':1,'sources':{}}, {'schema':1,'sources':[None]}):
            self.custody = value
            with self.assertRaises(ValueError): self.audit()

    def test_manifest_identity_version_and_candidate_lineage_rejected(self):
        e = self.custody['sources'][0]
        for key in ('id', 'version'):
            orig = e[key]; e[key] = 'wrong' if key == 'id' else '9.9.9'
            self.assertIn('local source custody manifest mismatch: ' + e['path'], self.audit()[1]); e[key] = orig
        next(e for e in self.custody['sources'] if e['kind']=='candidate')['candidate_for'] = 'wrong'
        with self.assertRaises(ValueError): self.audit()

    def test_refreshed_hash_cannot_rewrite_provenance(self):
        e = self.custody['sources'][0]
        (self.root / e['path'] / 'driver.c').write_text('tampered')
        e['tree_sha'] = parity.local_tree(self.root / e['path'])
        with self.assertRaisesRegex(ValueError, 'provenance tree mismatch'): self.audit()

    def test_foreign_or_missing_provenance_fails_closed(self):
        e = self.custody['sources'][0]
        e['provenance']['repository'] = 'foreign/repo'
        with self.assertRaisesRegex(ValueError, 'foreign'): self.audit()
        e['provenance']['repository'] = 'michaelrolphone-cmyk/RiscRTE-Drivers'
        e['provenance']['commit'] = 'a' * 40
        with self.assertRaises(subprocess.CalledProcessError): self.audit()

    def test_adapter_cannot_shadow_upstream_identity(self):
        self.ids.add(self.custody['sources'][0]['id'])
        with self.assertRaises(ValueError): self.audit()

    def test_current_tree_removes_only_exact_custody_labels(self):
        snap = json.loads((FIXTURES / 'source.json').read_text())
        released = json.loads((FIXTURES / 'release.json').read_text())
        paths = ('manifest/released-drivers.json','manifest/source-trees.json')
        before_bytes = {p:(ROOT/p).read_bytes() for p in paths}
        args = [ROOT,json.loads(before_bytes[paths[0]]),json.loads(before_bytes[paths[1]]),snap['source'],
                parity.unique(released['drivers'],'released'),snap['pins']['master'],snap['pins']['release-index']]
        before=parity.validate(*args);after=parity.validate(*args,self.custody)
        removed={'untracked local source directory: '+e['path'].split('/')[1] for e in self.custody['sources']}
        self.assertEqual(Counter(after['errors']),Counter(before['errors'])-Counter(removed))
        self.assertEqual(before['drivers'],after['drivers'])
        self.assertEqual(len(before['errors']),74)
        self.assertEqual(len(after['errors']),65)
        self.assertIn('original driver custody mismatch: ble-sensors',after['errors'])
        self.assertEqual({p:(ROOT/p).read_bytes() for p in paths},before_bytes)

    def test_unknown_sibling_still_untracked(self):
        # Keep only actual local directories, independent of upstream identity.
        for e in self.custody['sources']:
            if e['kind'] == 'candidate': e['kind'] = 'adapter'; e.pop('candidate_for')
        (self.root / 'Drivers/pcm_audio_common_extra').mkdir()
        inv={'drivers': [], 'parity_source': {'repository':'x/y','source_master_sha':'a'*40}}
        trees={'drivers':{}, 'source_repository':'x/y','source_master_sha':'a'*40}
        result=parity.validate(self.root, inv, trees, {}, {}, 'a'*40, 'b'*40, self.custody)
        self.assertEqual(result['errors'], ['untracked local source directory: pcm_audio_common_extra'])

if __name__ == '__main__': unittest.main()

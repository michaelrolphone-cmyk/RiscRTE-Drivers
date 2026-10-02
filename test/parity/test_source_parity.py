import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
import check_parity as parity

HEAD = 'a' * 40
RELEASE = 'b' * 40


class SourceParityTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.driver = self.root / 'Drivers/sample'
        self.driver.mkdir(parents=True)
        (self.driver / 'driver.c').write_text('int sample(void) { return 1; }\n')
        (self.driver / 'manifest.json').write_text(json.dumps({'id': 'sample', 'version': '1.0.0'}))
        self.base = parity.local_tree(self.driver)
        self.inventory = {'parity_source': {'repository': 'owner/repo', 'source_master_sha': HEAD},
                          'drivers': [{'id': 'sample', 'version': '1.0.0', 'source_path': 'Drivers/sample'}]}
        self.trees = {'source_repository': 'owner/repo', 'source_master_sha': HEAD,
                      'drivers': {'sample': self.base}}
        self.source = {'sample': {'id': 'sample', 'version': '1.0.0', 'source_path': 'Drivers/sample', 'tree_sha': self.base}}
        self.released = {'sample': {'id': 'sample', 'version': '1.0.0'}}

    def audit(self):
        return parity.validate(self.root, self.inventory, self.trees, self.source, self.released, HEAD, RELEASE)

    def test_tree_hash_matches_git_with_nested_files_modes_and_sorting(self):
        (self.driver / 'x').mkdir()
        (self.driver / 'x/child').write_bytes(b'\0binary\xff')
        (self.driver / 'x.c').write_text('sort before x/')
        executable = self.driver / 'tool'
        executable.write_text('#!/bin/sh\n')
        executable.chmod(0o755)
        subprocess.run(['git', 'init', '-q', str(self.root)], check=True)
        subprocess.run(['git', '-C', str(self.root), 'add', 'Drivers'], check=True)
        tree = subprocess.check_output(['git', '-C', str(self.root), 'write-tree'], text=True).strip()
        expected = subprocess.check_output(['git', '-C', str(self.root), 'rev-parse', tree + ':Drivers/sample'], text=True).strip()
        self.assertEqual(parity.local_tree(self.driver), expected)

    def test_clean_actual_tree_passes_with_pinned_provenance(self):
        report = self.audit()
        self.assertEqual(report['errors'], [])
        self.assertEqual(report['drivers'][0]['state'], 'unchanged')
        self.assertEqual(report['source_commit'], HEAD)
        self.assertEqual(report['release_index_commit'], RELEASE)

    def test_local_source_tamper_cannot_pass_with_unchanged_recorded_hash(self):
        p = self.driver / 'driver.c'
        p.write_text('int sample(void) { return 0; }\n')
        before = p.read_bytes()
        report = self.audit()
        self.assertTrue(report['errors'])
        self.assertEqual(report['drivers'][0]['state'], 'external-only')
        self.assertEqual(p.read_bytes(), before)  # audit must not repair/overwrite

    def test_add_delete_and_mode_changes_are_detected(self):
        p = self.driver / 'driver.c'
        for action in ['add', 'delete', 'mode']:
            with self.subTest(action=action):
                content = p.read_bytes()
                extra = self.driver / 'unexpected.h'
                if action == 'add': extra.write_text('extra')
                elif action == 'delete': p.unlink()
                else: p.chmod(0o755)
                self.assertTrue(self.audit()['errors'])
                if extra.exists(): extra.unlink()
                p.write_bytes(content)
                p.chmod(0o644)

    def test_upstream_only_conflict_and_converged_require_baseline_review(self):
        self.source['sample']['tree_sha'] = 'c' * 40
        self.assertEqual(self.audit()['drivers'][0]['state'], 'upstream-only')
        (self.driver / 'driver.c').write_text('externally improved\n')
        self.assertEqual(self.audit()['drivers'][0]['state'], 'conflict')
        self.source['sample']['tree_sha'] = parity.local_tree(self.driver)
        report = self.audit()
        self.assertEqual(report['drivers'][0]['state'], 'converged')
        self.assertIn('sample: recorded source tree needs refresh', report['errors'])

    def test_inventory_duplicates_missing_released_and_extra_local_fail(self):
        self.inventory['drivers'].append(copy.deepcopy(self.inventory['drivers'][0]))
        with self.assertRaisesRegex(ValueError, 'duplicate local'):
            self.audit()
        self.inventory['drivers'].pop()
        self.released = {}
        self.assertTrue(self.audit()['errors'])
        (self.root / 'Drivers/extra').mkdir()
        self.assertIn('untracked local source directory: extra', self.audit()['errors'])

    def test_path_escape_and_symlink_fail_without_reading_target(self):
        self.inventory['drivers'][0]['source_path'] = 'Drivers/../outside'
        self.assertTrue(self.audit()['errors'])
        self.inventory['drivers'][0]['source_path'] = 'Drivers/sample'
        (self.driver / 'escape').symlink_to('/no-such-parity-target')
        with self.assertRaisesRegex(ValueError, 'unsupported source entry'):
            self.audit()

    def test_source_ahead_of_release_is_explicit_not_a_false_mismatch(self):
        self.inventory['drivers'][0]['source_version'] = '1.0.1'
        self.source['sample']['version'] = '1.0.1'
        self.assertEqual(self.audit()['errors'], [])
        self.inventory['drivers'][0].pop('source_version')
        self.assertIn('source version mismatch: sample', self.audit()['errors'])

    def test_snapshot_pins_refs_once_and_never_swallows_manifest_failure(self):
        class Fake:
            def __init__(self): self.calls = []
            def resolve(self, ref):
                self.calls.append(('resolve', ref))
                return HEAD if ref == 'master' else RELEASE
            def driver_dirs(self, sha):
                self.calls.append(('dirs', sha))
                return [{'path': 'Drivers/sample', 'sha': 'c' * 40}]
            def read_json(self, path, sha):
                self.calls.append(('read', path, sha))
                return {'drivers': [{'id': 'sample', 'version': '1.0.0'}]} if path == 'release-index.json' else {'id': 'sample', 'version': '1.0.0'}
        fake = Fake()
        parity.snapshot(fake, 'master', 'release-index', 'release-index.json')
        self.assertEqual(fake.calls, [('resolve', 'master'), ('resolve', 'release-index'),
            ('read', 'release-index.json', RELEASE), ('dirs', HEAD),
            ('read', 'Drivers/sample/manifest.json', HEAD)])
        with patch.object(fake, 'read_json', side_effect=subprocess.CalledProcessError(1, 'gh')):
            with self.assertRaises(subprocess.CalledProcessError):
                parity.snapshot(fake, 'master', 'release-index', 'release-index.json')

    def test_snapshot_skips_only_configured_shared_non_package_directories(self):
        class Fake:
            def resolve(self, ref): return HEAD if ref == 'master' else RELEASE
            def driver_dirs(self, sha):
                return [{'path': 'Drivers/sample', 'sha': 'c' * 40},
                        {'path': 'Drivers/common', 'sha': 'd' * 40}]
            def read_json(self, path, sha):
                if path == 'Drivers/common/manifest.json':
                    raise AssertionError('shared directory is not a package')
                return {'drivers': [{'id': 'sample', 'version': '1.0.0'}]} if path == 'release-index.json' else {'id': 'sample', 'version': '1.0.0'}

        _, _, source, _ = parity.snapshot(Fake(), 'master', 'release-index', 'release-index.json',
                                          ['Drivers/common'])
        self.assertEqual(set(source), {'sample'})

    def test_unconfigured_or_stale_non_package_exclusions_fail_closed(self):
        class Fake:
            def resolve(self, ref): return HEAD if ref == 'master' else RELEASE
            def driver_dirs(self, sha):
                return [{'path': 'Drivers/sample', 'sha': 'c' * 40},
                        {'path': 'Drivers/common', 'sha': 'd' * 40},
                        {'path': 'Drivers/extra', 'sha': 'e' * 40}]
            def read_json(self, path, sha):
                if path.endswith('/manifest.json') and path != 'Drivers/sample/manifest.json':
                    raise subprocess.CalledProcessError(1, ['gh', 'api', path])
                return {'drivers': [{'id': 'sample', 'version': '1.0.0'}]} if path == 'release-index.json' else {'id': 'sample', 'version': '1.0.0'}

        with self.assertRaises(subprocess.CalledProcessError):
            parity.snapshot(Fake(), 'master', 'release-index', 'release-index.json', ['Drivers/common'])
        with self.assertRaisesRegex(ValueError, 'configured non-package source directory missing upstream'):
            parity.snapshot(Fake(), 'master', 'release-index', 'release-index.json', ['Drivers/stale'])

    def test_duplicate_upstream_release_ids_fail_instead_of_collapsing(self):
        with self.assertRaisesRegex(ValueError, 'duplicate upstream released'):
            parity.unique([{'id': 'x', 'version': '1.0.0'}] * 2, 'upstream released')


if __name__ == '__main__':
    unittest.main()

"""Exact non-package classification must not turn broken packages into libraries."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
import check_parity as parity

HEAD, RELEASE = 'a' * 40, 'b' * 40
SHARED = ('Drivers/common', 'Drivers/storage_fatfs', 'Drivers/x4pro_board')


class RemoteSharedSourceTests(unittest.TestCase):
    def rows(self, paths):
        return [{'path': path, 'sha': f'{index + 1:040x}', 'type': 'dir'}
                for index, path in enumerate(paths)]

    def test_classification_is_exact_and_preserves_ordinary_and_unknown_packages(self):
        self.assertEqual(parity.SHARED_SOURCE_DIRS, frozenset(SHARED))
        ordinary = ['Drivers/sample', 'Drivers/storage_fatfs_extra',
                    'Drivers/x4pro_board_extra', 'Drivers/unknown']
        rows = self.rows([*SHARED, *ordinary])
        source = parity.Source('owner/repo')
        def api(path, ref=None):
            if path == 'contents/Drivers':
                self.assertEqual(ref, HEAD)
                return rows
            self.assertIn(path, ['git/trees/' + row['sha'] for row in rows[:3]])
            return {'tree': [{'path': 'shared.h'}], 'truncated': False}
        with patch.object(source, 'api', side_effect=api):
            self.assertEqual([r['path'] for r in source.driver_dirs(HEAD)], ordinary)

    def test_manifest_in_any_explicit_shared_tree_is_rejected(self):
        for path in SHARED:
            with self.subTest(path=path):
                source = parity.Source('owner/repo')
                with patch.object(source, 'api', side_effect=[self.rows([path]),
                        {'tree': [{'path': 'manifest.json'}]}]):
                    with self.assertRaisesRegex(ValueError, 'became a package'):
                        source.driver_dirs(HEAD)

    def test_truncated_shared_tree_is_rejected_for_every_classification(self):
        for path in SHARED:
            with self.subTest(path=path):
                source = parity.Source('owner/repo')
                with patch.object(source, 'api', side_effect=[self.rows([path]),
                        {'tree': [], 'truncated': True}]):
                    with self.assertRaisesRegex(ValueError, 'truncated shared source tree'):
                        source.driver_dirs(HEAD)

    def test_failed_shared_tree_reads_are_never_ignored(self):
        for path in SHARED:
            with self.subTest(path=path):
                source = parity.Source('owner/repo')
                with patch.object(source, 'api', side_effect=[self.rows([path]),
                        subprocess.CalledProcessError(1, 'gh')]):
                    with self.assertRaises(subprocess.CalledProcessError):
                        source.driver_dirs(HEAD)

    def test_unlisted_manifestless_directories_still_fail_snapshot(self):
        for path in ('Drivers/unknown', 'Drivers/storage_fatfs_extra',
                     'Drivers/x4pro_board_extra'):
            with self.subTest(path=path):
                source = parity.Source('owner/repo')
                with patch.object(source, 'resolve', side_effect=[HEAD, RELEASE]), \
                        patch.object(source, 'api', return_value=self.rows([path])), \
                        patch.object(source, 'read_json', side_effect=[{'drivers': []},
                            subprocess.CalledProcessError(1, 'missing manifest')]) as read:
                    with self.assertRaises(subprocess.CalledProcessError):
                        parity.snapshot(source, 'master', 'release-index', 'release-index.json')
                    self.assertEqual(read.call_args.args, (path + '/manifest.json', HEAD))

    def test_native_controller_conflict_and_version_mismatch_remain_errors(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            path = root / 'Drivers/usb_controller_esp32s3'
            path.mkdir(parents=True)
            (path / 'driver.c').write_text('native controller bytes\n')
            identity = 'usb-controller-esp32s3'
            inventory = {'parity_source': {'repository': 'owner/repo', 'source_master_sha': HEAD},
                         'drivers': [{'id': identity, 'version': '1.0.0',
                                      'source_path': 'Drivers/usb_controller_esp32s3'}]}
            trees = {'source_repository': 'owner/repo', 'source_master_sha': HEAD,
                     'drivers': {identity: 'c' * 40}}
            source = {identity: {'id': identity, 'version': '1.0.1',
                                 'source_path': 'Drivers/usb_controller_esp32s3',
                                 'tree_sha': 'd' * 40}}
            released = {identity: {'id': identity, 'version': '1.0.1'}}
            result = parity.validate(root, inventory, trees, source, released, HEAD, RELEASE)
            self.assertEqual(result['drivers'][0]['state'], 'conflict')
            self.assertIn(identity + ': conflict source drift (preserve/reconcile local edits)',
                          result['errors'])
            self.assertIn('released version mismatch: ' + identity, result['errors'])
            self.assertIn('source version mismatch: ' + identity, result['errors'])
            self.assertIn(identity + ': recorded source tree needs refresh', result['errors'])


class LocalSharedSourceTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.git('init', '-q')
        for path in SHARED:
            library = self.root / path
            library.mkdir(parents=True)
            (library / 'shared.h').write_text('shared support bytes\n')
        package = self.root / 'Drivers/sample'
        package.mkdir()
        (package / 'manifest.json').write_text(json.dumps({'id': 'sample', 'version': '1.0.0'}))
        (self.root / 'release-index.json').write_text(json.dumps({'drivers': []}))
        self.commit()
        self.source = parity.Source('owner/repo', self.root)

    def git(self, *args):
        return subprocess.check_output(['git', '-C', str(self.root), *args], text=True).strip()

    def commit(self):
        self.git('add', '.')
        self.git('-c', 'user.name=dot', '-c', 'user.email=dot@localhost',
                 'commit', '-qm', 'test shared source classification')
        return self.git('rev-parse', 'HEAD')

    def test_local_pinned_trees_exclude_only_explicit_shared_libraries(self):
        head = self.git('rev-parse', 'HEAD')
        self.assertEqual([r['path'] for r in self.source.driver_dirs(head)], ['Drivers/sample'])
        _, _, source, _ = parity.snapshot(self.source, head, head, 'release-index.json')
        self.assertEqual(set(source), {'sample'})

    def test_local_manifest_insertion_is_rejected_for_each_shared_path(self):
        for path in SHARED:
            with self.subTest(path=path):
                manifest = self.root / path / 'manifest.json'
                manifest.write_text('{}')
                head = self.commit()
                with self.assertRaisesRegex(ValueError, 'became a package'):
                    self.source.driver_dirs(head)
                manifest.unlink()
                self.commit()

    def test_local_unknown_and_near_name_directories_require_manifests(self):
        for name in ('unknown', 'storage_fatfs_extra', 'x4pro_board_extra'):
            with self.subTest(name=name):
                path = self.root / 'Drivers' / name
                path.mkdir()
                (path / 'shared.h').write_text('not classified\n')
                head = self.commit()
                self.assertIn('Drivers/' + name, [r['path'] for r in self.source.driver_dirs(head)])
                with self.assertRaises(subprocess.CalledProcessError):
                    parity.snapshot(self.source, head, head, 'release-index.json')
                (path / 'shared.h').unlink()
                path.rmdir()
                self.commit()

    def test_local_shared_tree_read_errors_are_not_suppressed(self):
        for path in SHARED:
            with self.subTest(path=path):
                row = '040000 tree ' + 'c' * 40 + '\t' + path + '\n'
                with patch.object(self.source, 'git', side_effect=[row,
                        subprocess.CalledProcessError(1, 'git')]):
                    with self.assertRaises(subprocess.CalledProcessError):
                        self.source.driver_dirs(HEAD)


if __name__ == '__main__':
    unittest.main()

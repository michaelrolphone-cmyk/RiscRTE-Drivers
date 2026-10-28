"""Real Git restoration and rejection of corrupt/misidentified recovery inputs."""
import copy
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from badge_source_pack import restore


class Recovery(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.origin = self.root / 'origin'
        self.target = self.root / 'target'
        self.parts = self.root / 'parts'
        self.parts.mkdir()
        for p in (self.origin, self.target):
            subprocess.run(['git', 'init', '--quiet', p], check=True)
        subprocess.run(['git', '-C', self.origin, 'config', 'user.name', 'Recovery Test'], check=True)
        subprocess.run(['git', '-C', self.origin, 'config', 'user.email', 'recovery@example.invalid'], check=True)
        (self.origin / 'sdk.h').write_text('exact canonical SDK bytes\n')
        subprocess.run(['git', '-C', self.origin, 'add', '.'], check=True)
        subprocess.run(['git', '-C', self.origin, 'commit', '--quiet', '-m', 'source'], check=True)
        self.commit = self.git('rev-parse', 'HEAD')
        self.tree = self.git('rev-parse', 'HEAD^{tree}')
        data = subprocess.check_output(['git', '-C', self.origin, 'pack-objects', '--stdout', '--all'])
        (self.parts / 'objects-000.packpart').write_bytes(data)
        self.manifest = {'schema': 1, 'repository': 'owner/repository', 'source_commit': self.commit,
                         'source_tree': self.tree, 'object_count': int.from_bytes(data[8:12], 'big'),
                         'pack_sha256': hashlib.sha256(data).hexdigest(), 'shallow_boundaries': [],
                         'parts': [{'path': 'objects-000.packpart', 'bytes': len(data),
                                    'sha256': hashlib.sha256(data).hexdigest()}]}

    def git(self, *args):
        return subprocess.check_output(['git', '-C', self.origin, *args], text=True).strip()

    def run_restore(self, manifest=None):
        (self.parts / 'manifest.json').write_text(json.dumps(manifest or self.manifest))
        return restore(self.target, self.parts, 'owner/repository')

    def test_empty_consumer_restores_exact_sdk_and_tree(self):
        receipt = self.run_restore()
        self.assertEqual(receipt['source_tree'], self.tree)
        self.assertEqual(subprocess.check_output(['git', '-C', self.target, 'show',
                                                 self.commit + ':sdk.h']), b'exact canonical SDK bytes\n')

    def test_corrupt_part_is_rejected_before_import(self):
        (self.parts / 'objects-000.packpart').write_bytes(b'corrupt')
        with self.assertRaisesRegex(ValueError, 'part integrity'):
            self.run_restore()
        result = subprocess.run(['git', '-C', self.target, 'cat-file', '-e', self.commit],
                                stderr=subprocess.DEVNULL)
        self.assertNotEqual(result.returncode, 0)

    def test_manifest_mutations_fail_closed(self):
        mutations = [('repository', 'other/repository'), ('pack_sha256', '0' * 64),
                     ('object_count', 9999), ('source_tree', '0' * 40),
                     ('source_commit', 'main'), ('shallow_boundaries', ['0' * 40])]
        for key, value in mutations:
            with self.subTest(key=key):
                manifest = copy.deepcopy(self.manifest)
                manifest[key] = value
                with self.assertRaises((ValueError, subprocess.CalledProcessError)):
                    self.run_restore(manifest)

    def test_traversal_and_duplicate_parts_rejected(self):
        for parts in ([{**self.manifest['parts'][0], 'path': '../objects-000.packpart'}],
                      self.manifest['parts'] * 2):
            with self.subTest(parts=parts):
                manifest = copy.deepcopy(self.manifest)
                manifest['parts'] = parts
                with self.assertRaises(ValueError):
                    self.run_restore(manifest)

    def test_explicit_shallow_boundary_retained(self):
        manifest = copy.deepcopy(self.manifest)
        manifest['shallow_boundaries'] = [self.commit]
        self.run_restore(manifest)
        self.assertEqual((self.target / '.git/shallow').read_text(), self.commit + '\n')


if __name__ == '__main__':
    unittest.main()

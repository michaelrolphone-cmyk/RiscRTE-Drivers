"""Historical board custody is independent of additive board namespaces."""
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location(
    'board_preservation', ROOT / 'scripts/verify_usb_hid_preservation.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
NON_UTF8_PATH = b'boards/non-utf8-\xff/source.c'.decode('utf-8', 'surrogateescape')


class BoardCohortPreservation(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.git('init', '-q')
        self.git('config', 'user.name', 'Preservation test')
        self.git('config', 'user.email', 'preservation@example.invalid')
        self.git('config', 'core.fileMode', 'true')
        for path in ('boards/old.json', 'boards/old-board/source.c',
                     'boards/old-board/nested/pins.h',
                     'boards/literal[1]/source.c', 'boards/line\nbreak/source.c',
                     'boards/carriage\rreturn/source.c', NON_UTF8_PATH,
                     'Drivers/usb_hid/driver.c', 'Drivers/usb_host_v2/driver.c',
                     'sdk/locked.h', 'manifest/source-trees.json',
                     'candidates/old/source.c', 'release/locked.json'):
            self.write(path, 'historical\n')
        self.write('scripts/build_usb_hid.py', '# unchanged legacy builder\n')
        self.git('add', '.')
        self.git('commit', '-qm', 'historical cohort')
        self.baseline = self.git('rev-parse', 'HEAD').strip()

    def git(self, *args):
        return subprocess.check_output(
            ['git', *args], cwd=self.root, text=True, stderr=subprocess.STDOUT)

    def write(self, path, data):
        target = self.root / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(data)
        return target

    def reset(self):
        self.git('reset', '--hard', self.baseline)
        self.git('clean', '-fd')

    def verify(self):
        module.verify_preserved_board_roots(self.root, self.baseline)

    def test_multiple_new_sibling_roots_pass_staged_and_committed(self):
        for path in ('boards/alpha/source.c', 'boards/beta/nested/pins.h',
                     'boards/common/helper.c', 'boards/new.json',
                     'boards/literal1/source.c'):
            self.write(path, 'new independent source\n')
        self.git('add', 'boards')
        self.verify()
        self.git('commit', '-qm', 'independent boards')
        self.verify()

    def test_historical_edits_fail_unstaged_staged_and_committed(self):
        for path in ('boards/old.json', 'boards/old-board/nested/pins.h',
                     'boards/literal[1]/source.c', 'boards/line\nbreak/source.c',
                     'boards/carriage\rreturn/source.c', NON_UTF8_PATH):
            with self.subTest(path=path):
                self.reset()
                self.write(path, 'changed\n')
                with self.assertRaises(subprocess.CalledProcessError): self.verify()
                self.git('add', 'boards')
                with self.assertRaises(subprocess.CalledProcessError): self.verify()
                self.git('commit', '-qm', 'changed history')
                with self.assertRaises(subprocess.CalledProcessError): self.verify()

    def test_historical_delete_rename_mode_and_type_changes_fail(self):
        for mutation in ('delete', 'rename', 'mode', 'type'):
            with self.subTest(mutation=mutation):
                self.reset()
                target = self.root / 'boards/old-board/source.c'
                if mutation == 'delete': target.unlink()
                elif mutation == 'rename': target.rename(target.with_name('renamed.c'))
                elif mutation == 'mode': target.chmod(0o755)
                else:
                    target.unlink()
                    target.symlink_to('nested/pins.h')
                with self.assertRaises(subprocess.CalledProcessError): self.verify()
                self.git('add', 'boards')
                with self.assertRaises(subprocess.CalledProcessError): self.verify()

    def test_additions_inside_historical_subtree_fail(self):
        self.write('boards/old-board/nested/new.c', 'unexpected\n')
        # Preserve git diff's existing behavior: untracked files are not inputs.
        self.verify()
        self.git('add', 'boards')
        with self.assertRaises(subprocess.CalledProcessError): self.verify()
        self.git('commit', '-qm', 'addition inside historical subtree')
        with self.assertRaises(subprocess.CalledProcessError): self.verify()

    def test_working_tree_is_compared_not_only_head_or_index(self):
        self.write('boards/old.json', 'staged change\n')
        self.git('add', 'boards/old.json')
        self.write('boards/old.json', 'historical\n')
        self.verify()
        self.git('reset', '--', 'boards/old.json')
        self.write('boards/old.json', 'unstaged change\n')
        with self.assertRaises(subprocess.CalledProcessError): self.verify()

    def test_main_accepts_new_roots_but_preserves_other_prefix_guards(self):
        source = 'Drivers/usb_hid/driver.c'
        self.write('docs/usb-hid-checked-source-lock.json', json.dumps({
            'cohorts': {'pr40': {'commit': self.baseline, 'files': {
                source: {'sha256': hashlib.sha256((self.root / source).read_bytes()).hexdigest()}
            }}}
        }))
        self.write('candidates/usb-hid-checked-cleanup/manifest.json', json.dumps({
            'id': 'usb-hid', 'version': '0.1.3',
            'status': 'experimental-opt-in-checked-cleanup'
        }))
        self.write('boards/new-board/source.c', 'new\n')
        self.git('add', '.')
        with patch.object(module, 'ROOT', self.root), patch.object(module, 'ADDITIVE_SDK', {}), patch.object(module, 'EXTENDED_SDK', {}):
            module.main()
            for path in ('Drivers/usb_host_v2/driver.c', 'sdk/locked.h',
                         'manifest/source-trees.json', 'candidates/old/source.c',
                         'release/locked.json'):
                with self.subTest(path=path):
                    self.write(path, 'changed\n')
                    with self.assertRaises(subprocess.CalledProcessError): module.main()
                    self.write(path, 'historical\n')
            self.write(source, 'changed\n')
            with self.assertRaisesRegex(SystemExit, 'preserved source drift'):
                module.main()


if __name__ == '__main__':
    unittest.main()

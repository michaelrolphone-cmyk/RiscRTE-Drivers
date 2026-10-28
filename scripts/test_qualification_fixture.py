#!/usr/bin/env python3
import hashlib
import contextlib
import io
from unittest.mock import patch
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
import qualification_fixture as q


class FixtureTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.base = Path(self.tmp.name)
        self.source = self.base/'source'
        self.source.mkdir()
        self.git('init', '-q')
        self.git('config', 'user.email', 'fixture@example.invalid')
        self.git('config', 'user.name', 'Fixture')
        for name in ('scripts/run.py', 'scripts/shared.py', 'tests/observe.inc', 'sdk/api.h', 'README.md'):
            p = self.source/name
            p.parent.mkdir(exist_ok=True)
            p.write_text(name+'\n')
        self.git('add', '.')
        self.git('commit', '-qm', 'fixture source')
        self.pin = self.git('rev-parse', 'HEAD')
        self.profile = self.base/'profile'
        self.profile.mkdir()
        files = []
        for name in ('scripts/run.py', 'tests/observe.inc'):
            p = self.profile/name
            p.parent.mkdir(exist_ok=True)
            p.write_text('print("observer")\n')
            files.append({'path': name, 'sha256': q.sha(p)})
        (self.profile/'profile.json').write_text(json.dumps({'schema':1,'profile':'test',
            'runner':'scripts/run.py','files':files}))
        self.out = self.base/'out'

    def git(self, *args):
        return subprocess.check_output(['git','-C',str(self.source),*args],text=True).strip()

    def setup(self, **kw):
        return q.setup(self.profile,self.source,self.pin,self.out,**kw)

    def test_fresh_existing_and_check(self):
        expected = self.setup()
        before = (self.out/q.RECEIPT).stat().st_mtime_ns
        self.assertEqual(self.setup(), expected)
        self.assertEqual(self.setup(check=True), expected)
        self.assertEqual(before, (self.out/q.RECEIPT).stat().st_mtime_ns)
        self.assertFalse((self.out/'scripts/run.py').is_symlink())
        self.assertEqual(os.readlink(self.out/'scripts/shared.py'), str(self.source/'scripts/shared.py'))

    def test_missing_profile_or_materialization(self):
        with self.assertRaises(ValueError): self.setup(check=True)
        (self.profile/'tests/observe.inc').unlink()
        with self.assertRaises(ValueError): self.setup()
        self.assertFalse(self.out.exists())

    def test_tampered_profile(self):
        (self.profile/'tests/observe.inc').write_text('tampered')
        with self.assertRaises(ValueError): self.setup()
        self.assertFalse(self.out.exists())

    def test_missing_tampered_output_fail_without_repair(self):
        self.setup()
        p = self.out/'tests/observe.inc'
        p.write_text('tampered')
        with self.assertRaises(ValueError): self.setup(rebind=True)
        self.assertEqual(p.read_text(), 'tampered')
        p.unlink()
        with self.assertRaises(ValueError): self.setup()

    def test_unsafe_link_and_unexpected_file(self):
        self.setup()
        p=self.out/'scripts/shared.py';p.unlink();p.symlink_to('/etc/passwd')
        with self.assertRaises(ValueError): self.setup(rebind=True)
        p.unlink();p.symlink_to(self.source/'scripts/shared.py')
        (self.out/'extra').write_text('untracked')
        with self.assertRaises(ValueError): self.setup()

    def test_output_symlink_rejected(self):
        self.out.symlink_to(self.source, target_is_directory=True)
        with self.assertRaises(ValueError): self.setup()

    def test_explicit_source_rebind_same_path(self):
        old=self.setup()
        (self.source/'scripts/new.py').write_text('new\n')
        self.git('add','.');self.git('commit','-qm','new source')
        self.pin=self.git('rev-parse','HEAD')
        with self.assertRaises(ValueError): self.setup()
        with self.assertRaises(ValueError): self.setup(check=True,rebind=True)
        new=self.setup(rebind=True)
        self.assertNotEqual(old['shared_source']['commit'],new['shared_source']['commit'])
        self.assertTrue((self.out/'scripts/new.py').is_symlink())
        self.assertEqual(self.setup(check=True),new)

    def test_source_rebinding_another_checkout(self):
        self.setup()
        other=self.base/'other'
        subprocess.run(['git','clone','-q',str(self.source),str(other)],check=True)
        self.source=other
        with self.assertRaises(ValueError): self.setup()
        self.setup(rebind=True)
        self.assertEqual(os.readlink(self.out/'sdk'),str(other/'sdk'))

    def test_dirty_or_wrong_source_rejected(self):
        (self.source/'README.md').write_text('dirty')
        with self.assertRaises(ValueError): self.setup()
        self.assertFalse(self.out.exists())

    def test_forged_receipt_layout_rejected(self):
        self.setup()
        p=self.out/q.RECEIPT;d=json.loads(p.read_text())
        d['links'].remove('README.md');(self.out/'README.md').unlink()
        p.write_text(json.dumps(d))
        with self.assertRaises(ValueError):self.setup(rebind=True)

    def test_lane_launch_is_after_preflight_only(self):
        args = ['qualification_fixture.py', '--profile', 'local-quick', '--source', str(self.source),
                '--source-revision', self.pin, '--output', str(self.out), '--run', '--', '--help']
        with patch('sys.argv', args), patch.object(q.subprocess, 'call', return_value=0) as launch, \
                contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(q.main(), 0)
            launch.assert_called_once()
            self.assertEqual(launch.call_args.args[0][-1], '--help')
            self.assertTrue((self.out/q.RECEIPT).is_file())
        (self.out/'tests/badge_quick_execution.inc').unlink()
        with patch('sys.argv', args), patch.object(q.subprocess, 'call') as launch, \
                contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            q.main()
        launch.assert_not_called()

    def test_checked_in_profile_exact_three(self):
        _,files,_=q.profile_load(q.ROOT/'tests/qualification_profiles/local-quick')
        self.assertEqual(set(files),{
          'tests/badge_quick_execution.inc',
          'tests/badge_prototype_execution.cpp',
          'scripts/test_badge_prototype_execution.py'})
        # Hash values belong to the selected profile manifest, not a second
        # stale copy in this test. Mutation tests above reject changed bytes.
        for name,digest in files.items():
            self.assertEqual(digest,q.sha(q.ROOT/'tests/qualification_profiles/local-quick'/name))



if __name__=='__main__':unittest.main()

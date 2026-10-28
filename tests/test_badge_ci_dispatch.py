"""Check actual runner command dispatch; this is not target execution evidence."""
import importlib.util
from pathlib import Path
import shlex
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import run_badge_prototype_ci as runner


class StopAfterTarget(Exception):
    pass


class Dispatch(unittest.TestCase):
    def test_workflow_selected_recipe_reaches_native_and_application_builders(self):
        root = Path(__file__).resolve().parents[1]
        workflow = (root / '.github/workflows/badge-prototype.yml').read_text()
        selected = workflow.split('- profile: selected-full', 1)[1].split('recovery_flags:', 1)[0]
        flags = shlex.split(selected.split("flags: '", 1)[1].split("'", 1)[0])
        commands = []

        def record(command, **kwargs):
            commands.append(list(map(str, command)))
            if Path(command[1]).name == 'build_badge_prototype.py':
                raise StopAfterTarget()

        with tempfile.TemporaryDirectory() as temp:
            workspace = Path(temp) / 'workspace'
            output = Path(temp) / 'output'
            argv = ['runner', '--workspace', str(workspace), '--idf', str(Path(temp) / 'idf'),
                    '--cc', '/pinned/xtensa-esp32s3-elf-gcc', '--output', str(output),
                    '--spiffs-source', str(workspace / 'spiffs'), *flags]
            with patch.object(sys, 'argv', argv), patch.object(runner.subprocess, 'run', record):
                with self.assertRaises(StopAfterTarget):
                    runner.main()
        native = next(c for c in commands if Path(c[1]).name == 'build_badge_native.py')
        target = next(c for c in commands if Path(c[1]).name == 'build_badge_prototype.py')
        self.assertEqual(native[native.index('--native-stripe-rows') + 1], '16')
        for flag in ('--quick-actions', '--time-server-sync', '--performance-trace', '--spiffs-source',
                     '--reuse-columns', '--queued-pixels', '--reuse-unchanged-stripes'):
            self.assertIn(flag, native)
        for flag in ('--include-timecard', '--productivity', '--native-alarm-geometry',
                     '--native-wifi-geometry', '--foreground-idle', '--include-files',
                     '--native-daily', '--native-alarms', '--quick-actions',
                     *['--' + f for f in runner.SUCCESSOR_FLAGS]):
            self.assertIn(flag, target)
        self.assertTrue(any(Path(c[1]).name == 'test_time_server_sync.py' for c in commands))


if __name__ == '__main__':
    unittest.main()

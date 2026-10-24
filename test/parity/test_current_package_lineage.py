"""Regression checks for source changes and stable current package lineages."""
import json
from pathlib import Path
import re
import subprocess
import unittest
ROOT = Path(__file__).resolve().parents[2]
BASE = 'a7ae39994603c4545a3c014f09051ecfc12084d2'

def version(value):
    if not isinstance(value, str) or not re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+', value):
        raise ValueError('invalid numeric package version')
    return tuple(map(int, value.split('.')))

class CurrentLineage(unittest.TestCase):
    def test_version_parser_rejects_prerelease_and_malformed_versions(self):
        for invalid in ('0.1.0-preview', '0.1', '1.2.3.4', '', None):
            with self.assertRaises(ValueError): version(invalid)

    def test_native_cdc_is_newer_canonical_lineage_not_abi_fork(self):
        path = ROOT/'Drivers/usb_cdc_native/manifest.json'
        if not path.exists(): self.skipTest('no native CDC candidate in this partition')
        manifest = json.loads(path.read_text())
        self.assertEqual(manifest['id'], 'usb-cdc-acm')
        self.assertGreater(version(manifest['version']), (0, 1, 8))
        self.assertEqual(manifest['driver_abi'], 2)
        source = (path.parent/'driver.c').read_text()
        self.assertIn('"usb-cdc-acm", "serial.port"', source)
        self.assertNotIn('"usb-cdc-acm-v2", "serial.port"', source)

    def test_ble_changed_behavior_has_a_successor_version(self):
        path = 'Drivers/ble_sensors'
        old = json.loads(subprocess.check_output(['git', '-C', str(ROOT), 'show', BASE+':'+path+'/manifest.json']))
        current = json.loads((ROOT/path/'manifest.json').read_text())
        changed = subprocess.check_output(['git', '-C', str(ROOT), 'diff', BASE, '--', path+'/ble_scan_core.h'])
        self.assertEqual(current['id'], old['id'])
        if changed: self.assertGreater(version(current['version']), version(old['version']))
        else: self.assertGreaterEqual(version(current['version']), version(old['version']))

if __name__ == '__main__': unittest.main()

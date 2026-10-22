#!/usr/bin/env python3
"""Reject source drift in the preserved raw-HID/checked-host qualification cohort."""
from pathlib import Path
import hashlib, json, subprocess
ROOT = Path(__file__).resolve().parents[1]
lock = json.loads((ROOT / 'docs/usb-hid-checked-source-lock.json').read_text())
for path, expected in lock['cohorts']['pr40']['files'].items():
    actual = hashlib.sha256((ROOT / path).read_bytes()).hexdigest()
    if actual != expected['sha256']:
        raise SystemExit('preserved source drift: ' + path)
for prefix in ('Drivers/usb_hid', 'Drivers/usb_host_v2', 'sdk', 'manifest',
               'candidates', 'boards', 'release'):
    subprocess.run(['git', 'diff', '--exit-code', lock['cohorts']['pr40']['commit'],
                    '--', prefix, ':(exclude)candidates/usb-hid-checked-cleanup'],
                   cwd=ROOT, check=True)
manifest = json.loads((ROOT / 'candidates/usb-hid-checked-cleanup/manifest.json').read_text())
assert manifest['id'] == 'usb-hid' and manifest['version'] == '0.1.3'
assert manifest['status'] == 'experimental-opt-in-checked-cleanup'
# A separate explicit builder is the only candidate output selector.
legacy = (ROOT / 'scripts/build_usb_hid.py').read_text()
assert 'usb_hid_checked_cleanup' not in legacy
assert 'usb-hid-checked-cleanup' not in legacy
print('PASS preserved PR40 source/ABI/pins/inventories; separate opt-in candidate')

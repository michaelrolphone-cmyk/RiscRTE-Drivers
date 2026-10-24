#!/usr/bin/env python3
"""Reject source drift in the preserved raw-HID/checked-host qualification cohort."""
from pathlib import Path
import hashlib, json, subprocess
ROOT = Path(__file__).resolve().parents[1]
# Only this new original-provider entry may be removed for historical comparison.
# The resulting bytes must still match the unchanged PR40 SHA-256 lock.
CST_ENTRY = {'id': 'cst9217-touch', 'version': '0.1.0',
             'source_path': 'Drivers/cst9217_touch',
             'source_tree_sha': 'cb0bf865118d842a08fb4d0ed6eab13bea9bacc4'}

def preserved_bytes(path, data):
    if path != 'manifest/released-drivers.json':
        return data
    inventory = json.loads(data)
    if not inventory.get('original_drivers') or inventory['original_drivers'][-1] != CST_ENTRY:
        raise ValueError('missing or changed exact CST9217 inventory addition')
    # Use the pinned entry's field order too; dict equality alone would let a
    # reordered addition disappear before the historical-byte comparison.
    inventory['original_drivers'][-1] = CST_ENTRY
    # Reject formatting drift as well as any extra fields, duplicate entries or edits.
    if (json.dumps(inventory, indent=2)+'\n').encode() != data:
        raise ValueError('inventory serialization drift')
    inventory['original_drivers'].pop()
    return (json.dumps(inventory, indent=2)+'\n').encode()

def verify_preserved_board_roots(root, baseline):
    """Keep historical board files/subtrees exact; allow new sibling roots."""
    children = subprocess.check_output(
        ['git', 'ls-tree', '-z', '--name-only', baseline + ':boards'],
        cwd=root).split(b'\0')
    for child in children:
        if child:
            # Literal paths also preserve names containing Git pathspec syntax.
            # Keep raw bytes: Git names need not be UTF-8 or line-oriented.
            # Compare the working tree, as the original prefix-wide diff did.
            subprocess.run(['git', 'diff', '--exit-code', baseline, '--',
                            b':(literal)boards/' + child], cwd=root, check=True)

def main():
    lock = json.loads((ROOT / 'docs/usb-hid-checked-source-lock.json').read_text())
    for path, expected in lock['cohorts']['pr40']['files'].items():
        actual = hashlib.sha256(preserved_bytes(path, (ROOT / path).read_bytes())).hexdigest()
        if actual != expected['sha256']:
            raise SystemExit('preserved source drift: ' + path)
    for prefix in ('Drivers/usb_hid', 'Drivers/usb_host_v2', 'sdk', 'manifest',
                   'candidates', 'boards', 'release'):
        if prefix == 'boards':
            verify_preserved_board_roots(ROOT, lock['cohorts']['pr40']['commit'])
            continue
        subprocess.run(['git', 'diff', '--exit-code', lock['cohorts']['pr40']['commit'],
                        '--', prefix, ':(exclude)candidates/usb-hid-checked-cleanup',
                        ':(exclude)manifest/released-drivers.json'],
                       cwd=ROOT, check=True)
    manifest = json.loads((ROOT / 'candidates/usb-hid-checked-cleanup/manifest.json').read_text())
    assert manifest['id'] == 'usb-hid' and manifest['version'] == '0.1.3'
    assert manifest['status'] == 'experimental-opt-in-checked-cleanup'
    # A separate explicit builder is the only candidate output selector.
    legacy = (ROOT / 'scripts/build_usb_hid.py').read_text()
    assert 'usb_hid_checked_cleanup' not in legacy
    assert 'usb-hid-checked-cleanup' not in legacy
    print('PASS preserved PR40 source/ABI/pins/inventories; separate opt-in candidate')

if __name__ == '__main__':
    main()

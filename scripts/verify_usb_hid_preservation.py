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


# Exact published additions: Badge source a45497f5 and passive BLE receiver
# 864b076e. The reviewed round enum changes no existing layout or flag value.
# These allowances do not authorize future SDK edits or additional headers.
ADDITIVE_SDK = {"sdk/driver/RiscBluetoothSensorLinkV1.h":"de1e11824b6bb98859fc31ab39e530dcec41f5fc00b785dc669bef547956d5e2","sdk/driver/RiscPowerKeyNeutralizeV1.h":"5ec7d904b25d8ea8d515ceac88605d1f183cfe48740a30133a16c983d7d7f111","sdk/driver/RiscTimeSampleV1.h":"3298d2826a55263a5a5478f6f230030588ad7538aca5076d124d70f3e795eb2b"}
EXTENDED_SDK = {"sdk/driver/RiscDisplayOutputV1.h":["8111c8fb6d28542f3d2f5ae7ce7497891b1c89bd2488735c817542b955bdd726","11e747aecadcdfd0b37c79d057208ca9249714cb7482ca32b4a7a3296648c619"]}

def sdk_bytes(root, path):
    import stat
    member = root / path
    mode = member.lstat().st_mode
    if not stat.S_ISREG(mode) or mode & 0o111:
        raise ValueError('SDK member is not a regular non-executable file: ' + path)
    return member.read_bytes()

def verify_sdk(root, baseline):
    for path, digest in ADDITIVE_SDK.items():
        exists = subprocess.run(['git', 'cat-file', '-e', baseline + ':' + path],
            cwd=root, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if exists.returncode == 0:
            raise ValueError('SDK allowance is additive only: ' + path)
        if hashlib.sha256(sdk_bytes(root, path)).hexdigest() != digest:
            raise ValueError('changed additive SDK header: ' + path)
    for path, (before, after) in EXTENDED_SDK.items():
        historical = subprocess.check_output(['git', 'show', baseline + ':' + path], cwd=root)
        if hashlib.sha256(historical).hexdigest() != before:
            raise ValueError('changed historical SDK baseline: ' + path)
        if hashlib.sha256(sdk_bytes(root, path)).hexdigest() != after:
            raise ValueError('changed reviewed SDK extension: ' + path)

def main():
    lock = json.loads((ROOT / 'docs/usb-hid-checked-source-lock.json').read_text())
    for path, expected in lock['cohorts']['pr40']['files'].items():
        actual = hashlib.sha256(preserved_bytes(path, (ROOT / path).read_bytes())).hexdigest()
        if actual != expected['sha256']:
            raise SystemExit('preserved source drift: ' + path)
    verify_sdk(ROOT, lock['cohorts']['pr40']['commit'])
    for prefix in ('Drivers/usb_hid', 'Drivers/usb_host_v2', 'sdk', 'manifest',
                   'candidates', 'boards', 'release'):
        if prefix == 'boards':
            verify_preserved_board_roots(ROOT, lock['cohorts']['pr40']['commit'])
            continue
        subprocess.run(['git', 'diff', '--exit-code', lock['cohorts']['pr40']['commit'],
                        '--', prefix, ':(exclude)candidates/usb-hid-checked-cleanup',
                        ':(exclude)manifest/released-drivers.json',
                        *[':(exclude,literal)' + path for path in (*ADDITIVE_SDK, *EXTENDED_SDK)]],
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

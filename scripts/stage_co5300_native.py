#!/usr/bin/env python3
"""Stage the native CO5300 component into a clean Runtime-owned extension path.

This copies no vendor library and does not modify Runtime sources or its build
files. Include the emitted native/co5300/component.cmake from the consuming
Runtime product profile. --profile selects a self-contained header by path;
without it the component retains its default rectangular 2.06-inch profile.
The provider must be built with that same profile. No board ID is embedded here.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
NATIVE = ROOT / 'components/co5300/native'
INCLUDE = ROOT / 'components/co5300/include'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime', type=Path, required=True, help='Runtime checkout or clean staging root')
    parser.add_argument('--idf', type=Path, required=True, help='Qualified actual ESP-IDF checkout')
    parser.add_argument('--profile', type=Path, help='Self-contained build profile header to copy; defaults otherwise')
    args = parser.parse_args()
    runtime = args.runtime.resolve()
    destination = runtime / 'native/co5300'
    provenance = json.loads((NATIVE / 'co5300-native-provenance.json').read_text())
    revision = subprocess.check_output(['git', '-C', str(args.idf), 'rev-parse', 'HEAD'], text=True).strip()
    if revision != provenance['idf_revision']:
        raise RuntimeError('Native CO5300 staging requires the qualified ESP-IDF revision')
    if (runtime / 'lib/WaveshareVendorGfx').exists():
        raise RuntimeError('Native CO5300 staging refuses a mixed Arduino_GFX Runtime')
    for old in ('src/VendorDisplay.cpp', 'src/Co5300Display.cpp'):
        if (runtime / old).exists():
            raise RuntimeError('Native CO5300 staging refuses a competing display backend: ' + old)
    if destination.exists():
        raise RuntimeError('Native CO5300 destination already exists; select a clean staging root')
    # Read every input before creating output so a missing profile/source is not
    # mistaken for a successfully selected (or partially replaced) component.
    files = {name: (NATIVE / name).read_bytes()
             for name in ('Co5300Display.cpp', 'Co5300DisplayIdf6.inc', 'co5300-native-provenance.json')}
    files['RiscCo5300Profile.h'] = (INCLUDE / 'RiscCo5300Profile.h').read_bytes()
    for name in ('PROVENANCE.md', 'THIRD_PARTY_NOTICES.md', 'source-map.json', 'licenses/Apache-2.0.txt', 'licenses/Arduino_GFX-BSD.txt'):
        files[name] = (NATIVE.parent / name).read_bytes()
    if args.profile:
        files['SelectedProfile.h'] = args.profile.read_bytes()
    fragment = '''# Generated native-only component selection. Include from the Runtime product profile.
include_guard(GLOBAL)
list(APPEND RISC_NATIVE_EXTRA_SOURCES "${CMAKE_CURRENT_LIST_DIR}/Co5300Display.cpp")
list(APPEND RISC_NATIVE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_LIST_DIR}")
list(APPEND RISC_NATIVE_LINK_OPTIONS "-Wl,-u,risc_co5300_native_display_anchor")
'''
    if args.profile:
        fragment += 'list(APPEND RISC_NATIVE_COMPILE_OPTIONS "SHELL:-include \\\"${CMAKE_CURRENT_LIST_DIR}/SelectedProfile.h\\\"")\n'
    files['component.cmake'] = fragment.encode()
    proof = {
        'idf_revision': revision,
        'reviewed_candidate_revision': provenance['reviewed_candidate_revision'],
        'profile': 'selected' if args.profile else 'default',
        'sha256': {name: hashlib.sha256(data).hexdigest() for name, data in files.items()},
        'runtime_contract': 'RiscCpu::selectedDisplayBackend',
        'link_anchor': 'risc_co5300_native_display_anchor',
        'scope': 'Source staging only; provider profile consistency and final firmware/hardware qualification remain required.',
    }
    destination.mkdir(parents=True)
    for name, data in files.items():
        (destination / name).parent.mkdir(parents=True, exist_ok=True)
        (destination / name).write_bytes(data)
    (destination / 'staging.json').write_text(json.dumps(proof, indent=2) + '\n')
    print('Native CO5300 staged without Arduino or a product-checkout dependency.')
    print('Include from the Runtime product profile: ' + str(destination / 'component.cmake'))
    if args.profile:
        print('Build the provider with the same profile: ' + str(destination / 'SelectedProfile.h'))
    print('This stages source only; it does not build, modify the Runtime build, or qualify hardware.')


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""Verify real USB provider packages, including HID classes and ELF pointers."""
import argparse
import hashlib
import json
import re
from pathlib import Path
import struct
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from build_installed_usb_stack import DRIVERS
from generate_privileged_imports_v1 import extract_imports, encode_imports
from verify_provider_relocation_map import audit_loader_map
PACKAGES = ROOT / 'dist/packages'
CATALOG = PACKAGES / 'usb-provider-catalog.json'
BRIDGE = 'risc_fw_i2c_transact_v1'
EXPECTED = {
    'platform-clock-v1': ('platform.clock', []),
    'i2c-esp32s3-v2': ('i2c.bus', []),
    'gt911-touch': ('input.touch.raw', ['i2c.bus', 'platform.clock']),
    't5s3-usb-power-profile': ('board.power.bq25896.profile', []),
    'board-power-t5s3-v2': ('board.power.vbus',
                             ['i2c.bus', 'platform.clock', 'board.power.bq25896.profile']),
    'usb-controller-esp32s3': ('usb.controller', ['board.power.vbus']),
    'usb-host-v2': ('usb.host', ['usb.controller']),
    'usb-mass-storage': ('storage.volume', ['usb.host']),
    'usb-cdc-acm-v2': ('serial.port', ['usb.host']),
    'usb-cp210x-v2': ('serial.port', ['usb.host']),
    'usb-ch34x-v2': ('serial.port', ['usb.host']),
    'usb-ftdi': ('serial.port', ['usb.host']),
    'usb-stlink': ('debug.vendor.stlink', ['usb.host', 'platform.clock']),
    'usb-msp': ('debug.vendor.msp', ['usb.host', 'platform.clock']),
    'program-msp': ('program.msp', ['debug.vendor.msp']),
    'usb-hid': ('usb.hid', ['usb.host']),
    'usb-hid-keyboard': ('usb.hid.keyboard', ['usb.hid']),
    'usb-hid-text-input': ('input.text', ['usb.hid.keyboard']),
    'usb-hid-gamepad': ('usb.hid.gamepad', ['usb.hid', 'platform.clock']),
    'usb-xinput-gamepad': ('usb.xinput.gamepad', ['usb.host', 'platform.clock']),
    'usb-ui-navigation': ('input.navigation',
                          ['input.text', 'usb.hid.gamepad', 'usb.xinput.gamepad']),
}
EXPECTED_ABSOLUTE_POINTERS = {
    'usb-controller-esp32s3': {0x600c0000, 0x60039000, 0x60008000,
                               0x60038000, 0x60080000},
}


def mutation_rejected(elf: bytes, site: int, replacement: int) -> bool:
    data = bytearray(elf)
    hdr = struct.unpack_from('<HHIIIIIHHHHHH', data, 16)
    shoff, shentsize, shnum = hdr[5], hdr[10], hdr[11]
    for i in range(shnum):
        section = struct.unpack_from('<IIIIIIIIII', data, shoff + i * shentsize)
        virtual, file_offset, size = section[3], section[4], section[5]
        if virtual <= site and site + 4 <= virtual + size and section[1] != 8:
            struct.pack_into('<I', data, file_offset + site - virtual, replacement)
            with tempfile.TemporaryDirectory() as dirname:
                path = Path(dirname) / 'mutant.elf'
                path.write_bytes(data)
                observed = audit_loader_map(path)
                return bool(observed['unmapped_relative_values'])
    raise AssertionError('RELATIVE pointer source is not mapped')


def run(identities=None):
    selected = set(identities) if identities else set(EXPECTED)
    assert selected and selected <= set(EXPECTED), selected
    selected_capabilities = {EXPECTED[identity][0] for identity in selected}
    catalog = json.loads(CATALOG.read_text(encoding='utf-8'))
    assert catalog['schema'] == 1 and len(catalog['packages']) == len(selected)
    available = {}
    observed_ids = set()
    for record in catalog['packages']:
        id = record['id']
        assert id in selected and id not in observed_ids, id
        observed_ids.add(id)
        folder = PACKAGES / id
        manifest_bytes = (folder / '.package.json').read_bytes()
        assert len(manifest_bytes) <= 4096
        manifest = json.loads(manifest_bytes)
        assert set(manifest) == {'schema', 'kind', 'id', 'version', 'artifact',
                                 'architecture', 'min_runtime_api', 'entries', 'requires'}
        assert manifest['schema'] == 1 and manifest['kind'] == 'driver'
        source_name = next(source for identity, source, _, _ in DRIVERS if identity == id)
        expected_version = json.loads((ROOT / 'Drivers' / source_name / 'manifest.json').read_text()).get('version', '0.1.0')
        assert manifest['id'] == id and manifest['version'] == expected_version
        assert record['version'] == expected_version
        assert manifest['artifact'] == 'driver.elf'
        assert manifest['architecture'] == 'xtensa-esp32s3'
        assert manifest['min_runtime_api'] == 2
        cap, deps = EXPECTED[id]
        assert record['capability'] == cap and record['api'] == 1
        assert [r['capability'] for r in manifest['requires']] == deps
        for requirement in manifest['requires']:
            assert requirement == {'capability': requirement['capability'], 'min_api': 1}
            if requirement['capability'] in selected_capabilities:
                assert available.get(requirement['capability']) == 1, (id, requirement)
        entries = manifest['entries']
        names = {e['name'] for e in entries}
        assert names == {'driver.elf', 'provider-abi.v1', 'privileged-imports.v1'}
        assert {path.name for path in folder.iterdir()} == names | {'.package.json'}
        for entry in entries:
            name = entry['name']
            body = (folder / name).read_bytes()
            assert len(body) == entry['size_bytes'] and body
            assert entry['executable'] == (name == 'driver.elf')
        elf_path = folder / 'driver.elf'
        elf = elf_path.read_bytes()
        assert elf[:7] == b'\x7fELF\x01\x01\x01' and int.from_bytes(elf[16:18], 'little') == 3
        assert int.from_bytes(elf[18:20], 'little') == 94
        mapping = audit_loader_map(elf_path)
        assert mapping['relocations_examined'] > 0, id
        assert not mapping['unmapped_relocations'], (id, mapping['unmapped_relocations'])
        assert not mapping['unmapped_relative_values'], (id, mapping['unmapped_relative_values'])
        assert not mapping['unmapped_executable_sections'], (id, mapping['unmapped_executable_sections'])
        absolute = mapping['absolute_peripheral_relocations']
        assert {int(item['address'], 16) for item in absolute} == EXPECTED_ABSOLUTE_POINTERS.get(id, set()), id
        if absolute:
            site = int(absolute[0]['offset'], 16)
            assert mutation_rejected(elf, site, 0x70000000), id
            assert mutation_rejected(elf, site, 0x60004010), id
        profile = (folder / 'provider-abi.v1').read_text(encoding='ascii')
        assert profile == f'os-cpu-abi=1\nprovides={cap}\napi=1\n'
        imports_bytes = (folder / 'privileged-imports.v1').read_bytes()
        names = extract_imports(elf_path)
        assert imports_bytes == encode_imports(names), id
        assert names == sorted(set(names))
        assert (BRIDGE in names) == (id == 'i2c-esp32s3-v2'), id
        if id == 'i2c-esp32s3-v2':
            assert not any(name.startswith(('i2c_', 'gpio_', 'periph_module_'))
                           for name in names), names
        assert {e['name'] for e in record['files']} == {'driver.elf',
            'provider-abi.v1', 'privileged-imports.v1', '.package.json'}
        available[cap] = 1
    assert observed_ids == selected

    # The firmware compatibility bridge must probe every installed serial.port
    # class package. Keep this coupled to the canonical package inventory so a
    # newly added serial driver cannot be released but remain unreachable by
    # Serial Monitor.
    bridge_source = (ROOT / 'src/native/NativeUsbBridge.cpp').read_text(encoding='utf-8')
    match = re.search(r'const char\* choices\[\]\s*=\s*\{([^}]*)\};', bridge_source)
    assert match, 'USB serial provider choice table missing'
    runtime_serial = set(re.findall(r'"([a-z0-9-]+)"', match.group(1)))
    expected_serial = {identity for identity, (capability, _) in EXPECTED.items()
                       if capability == 'serial.port'}
    assert runtime_serial == expected_serial, (runtime_serial, expected_serial)

    print(f'{len(selected)} selected USB ELF packages: MMIO, negative mutations, imports, SHA-256 PASS')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ids', nargs='+', help='verify only these canonical driver package IDs')
    args = parser.parse_args()
    try:
        run(args.ids)
    except (AssertionError, OSError, ValueError, KeyError) as exc:
        print(f'USB stack package verification FAIL: {exc}', file=sys.stderr)
        sys.exit(1)

#!/usr/bin/env python3
"""Generate source-independent bounded ABI metadata from an actual linked ELF.

The import sidecar uses sorted newline-delimited ASCII names. A self-contained
ELF with zero imports uses precisely one LF, which remains a nonempty hashed
ordinary-package entry. Neither metadata nor SHA-256 is an authorization grant.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

CAPABILITY = re.compile(r'[a-z0-9](?:[a-z0-9._-]*[a-z0-9])?\Z', re.ASCII)
PACKAGE_ID = re.compile(r'[a-z0-9](?:[a-z0-9_-]*[a-z0-9])?\Z', re.ASCII)
MAX_API = 0xffffffff


def canonical_capability(value: object) -> bool:
    return isinstance(value, str) and 0 < len(value) < 64 and '..' not in value and (
        CAPABILITY.fullmatch(value) is not None)


def manifest_os_cpu_abi(manifest: dict, *, allow_native5_candidate: bool = False) -> int:
    revision = manifest.get('os_cpu_abi', 1)
    supported = (1, 2, 3, 5) if allow_native5_candidate is True else (1, 2, 3)
    if type(revision) is not int or revision not in supported:
        raise ValueError('unsupported OS/CPU ABI revision')
    return revision


def canonical_manifest(path: Path, *, allow_native5_candidate: bool = False) -> tuple[str, int]:
    if path.stat().st_size > 4096:
        raise ValueError('provider manifest is oversized')
    def object_pairs(pairs: list[tuple[str, object]]) -> dict[str, object]:
        result: dict[str, object] = {}
        for key, value in pairs:
            if key in result:
                raise ValueError('duplicate provider manifest field: ' + key)
            result[key] = value
        return result
    manifest = json.loads(path.read_text(encoding='utf-8'), object_pairs_hook=object_pairs)
    if not isinstance(manifest, dict) or manifest.get('type') not in ('driver', 'service', 'provider') or (
        type(manifest.get('driver_abi')) is not int or manifest['driver_abi'] != 2):
        raise ValueError('expected one installed module with provider ABI v2')
    if __package__:
        from .package_resource_paths import validate_resource_imports
    else:
        from package_resource_paths import validate_resource_imports
    if manifest.get('payload', 'executable') != 'executable':
        raise ValueError('provider ABI requires executable payload')
    validate_resource_imports(manifest.get('resource_imports', []))
    package_id = manifest.get('id')
    if manifest.get('type') == 'driver' and package_id == 'usb-cdc-acm-v2':
        raise ValueError('retired CDC alias is not a current package identity')
    if not isinstance(package_id, str) or not 0 < len(package_id) < 64 or (
        PACKAGE_ID.fullmatch(package_id) is None):
        raise ValueError('invalid provider identity')
    if manifest.get('architecture') != 'xtensa-esp32s3' or (
        manifest.get('file_name') != 'driver.elf'):
        raise ValueError('unexpected architecture or executable artifact')
    manifest_os_cpu_abi(manifest, allow_native5_candidate=allow_native5_candidate)
    provides = manifest.get('provides')
    if not isinstance(provides, list) or len(provides) != 1 or (
        not isinstance(provides[0], dict)):
        raise ValueError('driver must expose exactly one canonical capability')
    capability, api = provides[0].get('capability'), provides[0].get('api')
    if not canonical_capability(capability) or type(api) is not int or (
        api <= 0 or api > MAX_API):
        raise ValueError('invalid provided capability/api')
    requires = manifest.get('requires')
    if not isinstance(requires, list) or len(requires) > 16:
        raise ValueError('invalid dependency declarations')
    seen: set[str] = set()
    for requirement in requires:
        if not isinstance(requirement, dict):
            raise ValueError('invalid dependency shape')
        name, min_api = requirement.get('capability'), requirement.get('api')
        if not canonical_capability(name) or type(min_api) is not int or (
            min_api <= 0 or min_api > MAX_API or name in seen):
            raise ValueError('invalid/duplicate required capability')
        seen.add(name)
    return capability, api


def prepare(elf: Path, manifest: Path, destination: Path, *,
            allow_native5_candidate: bool = False) -> tuple[Path, Path]:
    # Manifest-only release planning does not need the ELF build toolchain.
    if __package__:
        from .generate_privileged_imports_v1 import extract_imports, encode_imports
    else:
        from generate_privileged_imports_v1 import extract_imports, encode_imports
    capability, api = canonical_manifest(manifest, allow_native5_candidate=allow_native5_candidate)
    revision = manifest_os_cpu_abi(json.loads(manifest.read_text(encoding="utf-8")),
                                   allow_native5_candidate=allow_native5_candidate)
    names = extract_imports(elf)  # Both .dynsym and .symtab; zero is legitimate.
    profile = f'os-cpu-abi={revision}\nprovides={capability}\napi={api}\n'.encode('ascii')
    imports = encode_imports(names)
    if len(profile) > 160 or len(imports) > 128 * 128:
        raise ValueError('profile or import resource exceeds provider metadata bounds')
    destination.mkdir(parents=True, exist_ok=True)
    output_profile = destination / 'provider-abi.v1'
    output_imports = destination / 'privileged-imports.v1'
    output_profile.write_bytes(profile)
    output_imports.write_bytes(imports)
    print(f'Provider capability: {capability}@{api} (generic ABI {revision})')
    print(f'Exact linked ELF imports: {len(names)}')
    for label, value in [('ELF', elf), ('provider-abi.v1', output_profile),
                         ('privileged-imports.v1', output_imports)]:
        print(f'{label} SHA-256: {hashlib.sha256(value.read_bytes()).hexdigest()}')
    print('Unsigned provider metadata generated; SHA-256 is integrity, not authority.')
    return output_profile, output_imports


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', required=True, type=Path)
    parser.add_argument('--manifest', required=True, type=Path)
    parser.add_argument('--output-dir', required=True, type=Path)
    parser.add_argument('--allow-native5-candidate', action='store_true',
                        help='Explicit private5 metadata only; not native execution authority')
    arguments = parser.parse_args()
    try:
        prepare(arguments.elf, arguments.manifest, arguments.output_dir,
                allow_native5_candidate=arguments.allow_native5_candidate)
        return 0
    except (OSError, ValueError, UnicodeError, json.JSONDecodeError) as error:
        print(f'Provider package input rejected: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())

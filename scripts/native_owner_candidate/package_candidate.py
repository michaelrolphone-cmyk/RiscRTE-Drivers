#!/usr/bin/env python3
"""Preflight and emit a PRIVATE native5 owner candidate, never install it.

The existing ordinary generator remains unchanged. Product loader sources and
separately pinned packaging tools have distinct provenance; metadata is not an
execution grant, physical admission, or proof of a complete owner link.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import re
import sys
import tempfile

from core_binding import validate_core_source
from package_admission import (build_validator, digest, json_object, pinned,
                               preflight, snapshot, structural_preflight)

TOOL_PINS = {
    'generate_privileged_imports_v1.py': 'a65818d5ee8f4fb68e256666b8fb6680e69c8d96249a2e65b09815d994f876e0',
    'generate_provider_package_inputs_v1.py': '8e1808b43460dc0b0bd88d395cca5743f9f4617124a07e7f6ced3d9db5294f94',
    'package_resource_paths.py': 'c7a495ca62fc5725c9da189a267a65b1fe630e9e43d44f5131445c2d5a74422b',
}


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def copy_tooling(arguments, work):
    # Repository-owned helpers only. The build inputs cannot nominate Python
    # code to execute; all three helpers are versioned beside this producer.
    files = {name: {'path': str(Path(__file__).resolve().with_name(name)), 'sha256': value}
             for name, value in TOOL_PINS.items()}
    work.mkdir()
    for name, record in files.items():
        (work / name).write_bytes(pinned(record['path'], record['sha256'], 512 * 1024))
    return files


def validate_config(config):
    """The existing owner composition contract, without executing a composer."""
    fields = {'id', 'version', 'stack_bytes', 'working_stack_bytes',
              'interface_generation', 'configuration_generation'}
    if not isinstance(config, dict) or set(config) != fields:
        raise ValueError('explicit build config required; mapping/authority are not configuration')
    if not isinstance(config['id'], str) or not re.fullmatch(
            r'[a-z0-9](?:[a-z0-9_-]{0,61}[a-z0-9])?', config['id']):
        raise ValueError('invalid candidate id')
    if not isinstance(config['version'], str) or len(config['version']) >= 32 or not re.fullmatch(
            r'[0-9]+\.[0-9]+\.[0-9]+', config['version']):
        raise ValueError('explicit numeric candidate version required')
    if any(int(part) > 0xffffffff for part in config['version'].split('.')):
        raise ValueError('candidate version component overflow')
    for name in fields - {'id', 'version'}:
        maximum = 0xffffffff if 'stack' in name else 0xffffffffffffffff
        if type(config[name]) is not int or not 0 < config[name] <= maximum:
            raise ValueError('invalid bounded nonzero ' + name)
    if config['working_stack_bytes'] >= config['stack_bytes']:
        raise ValueError('total stack must leave a nonzero resident cleanup reserve')
    # Not proof of actual resident-floor, destructor or working-stack adequacy.
    return config


def verify_sources(source, members):
    if not members: raise ValueError('empty owner source composition')
    for name, value in members.items():
        relative = Path(name)
        if relative.is_absolute() or '..' in relative.parts:
            raise ValueError('invalid owner source path')
        pinned(Path(source) / relative, value)


def emit_candidate(a):
    if a.output.exists(): raise ValueError('fresh private output required')
    if not a.output.parent.is_dir(): raise ValueError('existing private output parent required')
    composition_raw = pinned(a.composition, a.composition_sha256, 2 * 1024 * 1024)
    source_manifest_raw = pinned(a.source_manifest, a.source_manifest_sha256, 2 * 1024 * 1024)
    composition, members = json_object(composition_raw), json_object(source_manifest_raw)
    if composition.get('source_manifest_sha256') != digest(source_manifest_raw):
        raise ValueError('source composition mismatch')
    verify_sources(a.source, members)
    # All subsequent parsers and emitted driver bytes use this one snapshot.
    raw = snapshot(a.elf); image_hash = digest(raw)
    manifest = json_object(snapshot(a.manifest, 4096))
    with tempfile.TemporaryDirectory(prefix='.native5-preflight-', dir=str(a.output.parent)) as directory:
        work = Path(directory)
        tools = work / 'tooling'; tooling = copy_tooling(a, tools)
        config = validate_config(composition['config'])
        expected = {'id': config['id'], 'version': config['version'], 'type': 'provider',
                    'driver_abi': 2, 'os_cpu_abi': 5, 'architecture': 'xtensa-esp32s3',
                    'file_name': 'driver.elf',
                    'provides': [{'capability': 'platform.http-client', 'api': 1}], 'requires': []}
        if manifest != expected: raise ValueError('manifest differs from exact composed owner contract')
        image = work / 'snapshot.elf'; image.write_bytes(raw)
        validator = build_validator(a.admission_source, a.compiler,
                                    a.compiler_sha256, work / 'validator', host_sdk=a.host_sdk)
        structural_preflight(validator, image, image_hash)
        extractor = load_module('pinned_import_extractor', tools / 'generate_privileged_imports_v1.py')
        imports = extractor.extract_imports(image)
        declaration_raw = extractor.encode_imports(imports)
        declaration = work / 'privileged-imports.v1'; declaration.write_bytes(declaration_raw)
        c_result = preflight(validator, image, declaration, image_hash, digest(declaration_raw))
        if validate_core_source(a.admission_source) != validator['core_binding']:
            raise ValueError('Core binding changed before mapping')
        # Not merely a caller comment: mapping cannot be reached after either
        # the actual structural or authorization/exact-set gate refuses.
        from package_mapping import inspect_mapping
        mapping = inspect_mapping(raw, privileged_candidate=True)
        exports = mapping['loader_exports']
        if len(exports) != 1 or exports[0]['name'] != 't5_driver_get' or exports[0]['type'] != 'STT_FUNC':
            raise ValueError('loader-visible owner exports must be exactly t5_driver_get')
        # Current native policy resolves the descriptor getter only from the
        # ordinary executable section. Internal mapping support is not entry
        # authority; do not emit a candidate the actual policy must refuse.
        if exports[0]['section'] != '.text':
            raise ValueError('native provider getter must remain in ordinary .text')
        private = ('native_network_', 'compiled_trust_data', 'OwnerDispatch',
                   'NativeNetworkGraph', 'NativeSessionControl')
        if any(any(fragment in name for fragment in private) for name in imports):
            raise ValueError('unlinked provider-private leaf in ELF imports')
        required = ('risc_cpu_worker_start_v5', 'risc_cpu_worker_join_v5', 'risc_cpu_worker_release_v5')
        if any(name not in imports for name in required):
            raise ValueError('actual v5 owning root import closure missing')
        if any(name.startswith('risc_cpu_worker_') and not name.endswith('_v5') for name in imports):
            raise ValueError('mixed worker revisions in owning image')
        package = work / 'package'; package.mkdir()
        (package / 'driver.elf').write_bytes(raw)
        manifest.update(size_bytes=len(raw), sha256=image_hash)
        manifest_raw = (json.dumps(manifest, indent=2, sort_keys=True) + '\n').encode()
        (package / 'manifest.json').write_bytes(manifest_raw)
        # These names are fixed to the copied closure, never ambient modules.
        old_modules = {n: sys.modules.get(n) for n in ('generate_privileged_imports_v1', 'package_resource_paths')}
        try:
            sys.modules['generate_privileged_imports_v1'] = extractor
            sys.modules['package_resource_paths'] = load_module('package_resource_paths', tools / 'package_resource_paths.py')
            generator = load_module('pinned_package_generator', tools / 'generate_provider_package_inputs_v1.py')
            generator.prepare(package / 'driver.elf', package / 'manifest.json', package,
                              allow_native5_candidate=True)
        finally:
            for name, previous in old_modules.items():
                if previous is None: sys.modules.pop(name, None)
                else: sys.modules[name] = previous
        if snapshot(package / 'driver.elf') != raw or snapshot(package / 'privileged-imports.v1', 16384) != declaration_raw:
            raise ValueError('generated package differs from validated immutable snapshot')
        if snapshot(package / 'manifest.json', 4096) != manifest_raw:
            raise ValueError('generated manifest differs from exact owner contract')
        if snapshot(package / 'provider-abi.v1', 160) != b'os-cpu-abi=5\nprovides=platform.http-client\napi=1\n':
            raise ValueError('generated provider profile differs from exact owner contract')
        names = ['driver.elf', 'manifest.json', 'provider-abi.v1', 'privileged-imports.v1']
        if sorted(p.name for p in package.iterdir()) != sorted(names):
            raise ValueError('unexpected generated package member')
        entries = []
        for name in names:
            value = snapshot(package / name)
            entries.append({'name': name, 'size_bytes': len(value), 'sha256': digest(value),
                            'executable': name == 'driver.elf'})
        envelope = {'schema': 1, 'kind': 'provider', 'id': config['id'], 'version': config['version'],
                    'artifact': 'driver.elf', 'architecture': 'xtensa-esp32s3', 'min_runtime_api': 2,
                    'entries': entries, 'requires': []}
        envelope_raw = (json.dumps(envelope, indent=2, sort_keys=True) + '\n').encode()
        (package / '.package.json').write_bytes(envelope_raw)
        verify_sources(a.source, members)
        for name, record in tooling.items():
            pinned(record['path'], record['sha256'], 512 * 1024)
            pinned(tools / name, record['sha256'], 512 * 1024)
        pinned(a.composition, a.composition_sha256, 2 * 1024 * 1024)
        pinned(a.source_manifest, a.source_manifest_sha256, 2 * 1024 * 1024)
        if validate_core_source(a.admission_source) != validator['core_binding']:
            raise ValueError('Core binding changed before emission')
        # Final output does not exist until every admission and provenance gate
        # passed. Exclusive mkdir prevents overwriting a pre-existing output.
        a.output.mkdir()
        destination = a.output / 'Providers' / config['id']; destination.parent.mkdir()
        package.rename(destination)
        if snapshot(destination / 'driver.elf') != raw:
            raise ValueError('emitted ELF differs from admitted snapshot')
        for entry in entries:
            pinned(destination / entry['name'], entry['sha256'])
        pinned(destination / '.package.json', digest(envelope_raw))
        report = {'status': 'private-native5-candidate-preflight-passed', 'elf_sha256': image_hash,
                  'composition_sha256': a.composition_sha256, 'imports': imports,
                  'tooling_source': 'repository-owned exact helper closure',
                  'tooling': tooling, 'admission': c_result, 'mapping': mapping,
                  'validator': {k: str(v) if isinstance(v, Path) else v for k, v in validator.items()
                                if k != 'binary'},
                  'package': str(destination), 'envelope_sha256': digest(envelope_raw),
                  'selected_policy_emitted': False, 'boot_bindings_emitted': False,
                  'runtime_admission': False, 'ordinary_profile_accepted': False,
                  'internal_port_available': False, 'physical_admission': False,
                  'physical_retirement': False, 'working_stack_qualified': False,
                  'activated': False, 'deployed': False,
                  'remaining': ['complete actual owner link and behavior qualification',
                                'authenticated selected-policy/runtime admission',
                                'internal executable-memory provision and cache-off closure',
                                'concrete physical/endpoint retirement qualification']}
        (a.output / 'PRIVATE_PACKAGE_RESULT.json').write_text(json.dumps(report, indent=2, sort_keys=True) + '\n')
        return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('elf', 'composition', 'source', 'source-manifest', 'manifest',
                 'admission-source',
                 'compiler', 'host-sdk', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    for name in ('composition', 'source-manifest', 'compiler'):
        parser.add_argument('--' + name + '-sha256', required=True)
    arguments = parser.parse_args()
    sys.dont_write_bytecode = True
    print(json.dumps(emit_candidate(arguments), indent=2, sort_keys=True))


if __name__ == '__main__':
    main()

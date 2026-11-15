"""Actual C gate + producer tests using explicitly constructed metadata fixtures.

Run through test/run_native_owner_candidate.py. RISC_PACKAGE_TEST_CONTEXT
names its pinned qualification context with the real source/compiler/tooling paths.
These tests do not establish a linked native owner or physical admission.
"""
import argparse
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts/native_owner_candidate'))
import package_candidate as producer
import generate_provider_package_inputs_v1 as package_metadata
from package_admission import build_validator, digest, preflight, structural_preflight
from elf_fixture import make_elf
from core_binding import load_binding
import shutil

WORKERS = ['risc_cpu_worker_start_v5', 'risc_cpu_worker_join_v5', 'risc_cpu_worker_release_v5']


class ConfigTests(unittest.TestCase):
    def config(self):
        return {'id': 'network-test', 'version': '1.0.0', 'stack_bytes': 32768,
                'working_stack_bytes': 16384, 'interface_generation': 1,
                'configuration_generation': 1}

    def test_config_accepts_exact_bounded_composition(self):
        value = self.config()
        self.assertEqual(producer.validate_config(value), value)
        value['interface_generation'] = 0xffffffffffffffff
        value['configuration_generation'] = 0xffffffffffffffff
        value['version'] = '4294967295.4294967295.0'
        self.assertEqual(producer.validate_config(value), value)

    def test_config_refuses_identity_path_and_authority_extensions(self):
        for identity in ('../escape', '/absolute', 'a/b', '', 'a' * 64):
            with self.subTest(identity=identity), self.assertRaises(ValueError):
                producer.validate_config(dict(self.config(), id=identity))
        with self.assertRaises(ValueError):
            producer.validate_config(dict(self.config(), runtime_authority=True))

    def test_config_refuses_bool_overflow_and_zero_cleanup_reserve(self):
        for name, value in (('stack_bytes', True), ('interface_generation', 0),
                            ('configuration_generation', 1 << 64),
                            ('stack_bytes', 1 << 32), ('working_stack_bytes', 32768),
                            ('version', '4294967296.0.0')):
            with self.subTest(name=name, value=value), self.assertRaises(ValueError):
                producer.validate_config(dict(self.config(), **{name: value}))

    def test_metadata_candidate_mode_is_explicit_not_authority(self):
        for revision in (1, 2, 3):
            self.assertEqual(package_metadata.manifest_os_cpu_abi({'os_cpu_abi': revision}), revision)
        for revision in (4, 5, True, '5'):
            with self.subTest(revision=revision), self.assertRaises(ValueError):
                package_metadata.manifest_os_cpu_abi({'os_cpu_abi': revision})
        self.assertEqual(package_metadata.manifest_os_cpu_abi(
            {'os_cpu_abi': 5}, allow_native5_candidate=True), 5)
        with self.assertRaises(ValueError):
            package_metadata.manifest_os_cpu_abi({'os_cpu_abi': 5}, allow_native5_candidate=1)


class CandidateTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.context = json.loads(Path(os.environ['RISC_PACKAGE_TEST_CONTEXT']).read_text())
        cls.temporary = tempfile.TemporaryDirectory(prefix='package-producer-tests-',
                                                     dir=cls.context['work_parent'])
        cls.root = Path(cls.temporary.name)
        cls.validator = build_validator(cls.context['admission_source'], cls.context['compiler'],
            cls.context['compiler_sha256'], cls.root / 'gate', host_sdk=cls.context['host_sdk'])

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def setUp(self):
        self.work = self.root / self._testMethodName; self.work.mkdir()

    def gate(self, raw, declared):
        image = self.work / 'direct.elf'; image.write_bytes(raw)
        declaration = self.work / 'direct-imports'; declaration.write_bytes(declared)
        return preflight(self.validator, image, declaration, digest(raw), digest(declared))

    def arguments(self, raw=None):
        source = self.work / 'source'; source.mkdir()
        source_bytes = b'/* Constructed test source, not a complete native owner. */\n'
        (source / 'fixture.c').write_bytes(source_bytes)
        source_manifest = self.work / 'SOURCE.json'
        source_manifest.write_text(json.dumps({'fixture.c': digest(source_bytes)}))
        config = {'id': 'network-test', 'version': '1.0.0', 'stack_bytes': 32768,
                  'working_stack_bytes': 16384, 'interface_generation': 1,
                  'configuration_generation': 1}
        composition = self.work / 'COMPOSITION.json'
        composition.write_text(json.dumps({'config': config,
            'source_manifest_sha256': digest(source_manifest.read_bytes())}))
        manifest = self.work / 'manifest.json'
        manifest.write_text(json.dumps({'id': config['id'], 'version': config['version'],
            'type': 'provider', 'driver_abi': 2, 'os_cpu_abi': 5,
            'architecture': 'xtensa-esp32s3', 'file_name': 'driver.elf',
            'provides': [{'capability': 'platform.http-client', 'api': 1}], 'requires': []}))
        image = self.work / 'input.elf'; image.write_bytes(make_elf(imports=WORKERS) if raw is None else raw)
        values = {key: self.context[key] for key in (
            'admission_source', 'compiler', 'compiler_sha256', 'host_sdk')}
        for key in ('admission_source', 'compiler', 'host_sdk'):
            values[key] = Path(values[key])
        return argparse.Namespace(**values, elf=image, composition=composition, source=source,
            source_manifest=source_manifest, manifest=manifest, output=self.work / 'output',
            composition_sha256=digest(composition.read_bytes()),
            source_manifest_sha256=digest(source_manifest.read_bytes()))

    def test_actual_gate_zero_import_and_exact_set(self):
        self.assertTrue(self.gate(make_elf(), b'\n')['manifest_import_set_exact'])
        with self.assertRaisesRegex(ValueError, 'import-policy-or-exact-set'):
            self.gate(make_elf(), b'malloc\n')

    def test_actual_gate_rejects_unknown_import_and_wrong_declaration(self):
        with self.assertRaisesRegex(ValueError, 'import-policy-or-exact-set'):
            self.gate(make_elf(imports=['unauthorized_device_driver']), b'unauthorized_device_driver\n')
        with self.assertRaisesRegex(ValueError, 'import-policy-or-exact-set'):
            self.gate(make_elf(imports=WORKERS), b'\n')
        for revision in (3, 4):
            old_workers = [name.replace('_v5', '_v%d' % revision) for name in WORKERS]
            declared = ('\n'.join(sorted(old_workers)) + '\n').encode()
            with self.subTest(revision=revision), self.assertRaisesRegex(ValueError, 'import-policy-or-exact-set'):
                self.gate(make_elf(imports=old_workers), declared)

    def test_actual_gate_rejects_bad_structure_and_noncanonical_declaration(self):
        with self.assertRaisesRegex(ValueError, 'structural-rejected'):
            self.gate(b'not-an-ELF', b'\n')
        for declared in (b'', b' malloc\n', b'malloc\n\n', b'malloc'):
            with self.subTest(declared=declared), self.assertRaisesRegex(ValueError, 'declaration-rejected'):
                self.gate(make_elf(), declared)

    def test_producer_emits_exact_validated_bytes_and_false_authority(self):
        a = self.arguments(); raw = a.elf.read_bytes()
        report = producer.emit_candidate(a)
        self.assertEqual((Path(report['package']) / 'driver.elf').read_bytes(), raw)
        self.assertEqual(report['elf_sha256'], digest(raw))
        self.assertTrue(report['admission']['candidate5_import_allowlist_compatible'])
        for name in ('runtime_admission', 'internal_port_available', 'physical_admission', 'activated', 'deployed'):
            self.assertIs(report[name], False)

    def test_structural_rejection_precedes_python_elf_and_mapping(self):
        a = self.arguments(b'invalid')
        real_loader = producer.load_module
        def guarded(name, path):
            if name == 'pinned_import_extractor': self.fail('extractor reached before structural refusal')
            return real_loader(name, path)
        with mock.patch.object(producer, 'load_module', side_effect=guarded), \
             self.assertRaisesRegex(ValueError, 'structural admission refused'):
            producer.emit_candidate(a)
        self.assertFalse(a.output.exists())

    def test_import_refusal_precedes_mapping_and_emission(self):
        a = self.arguments(make_elf(imports=WORKERS + ['unauthorized_device_driver']))
        with mock.patch('package_mapping.inspect_mapping', side_effect=AssertionError('mapping reached')), \
             self.assertRaisesRegex(ValueError, 'import-policy-or-exact-set'):
            producer.emit_candidate(a)
        self.assertFalse(a.output.exists())

    def test_mapping_refusal_precedes_emission(self):
        rows = [{'name': '.text', 'address': 0x1000, 'data': bytes(16), 'flags': 6, 'align': 4},
                {'name': '.data', 'address': 0x2000, 'data': struct.pack('<I', 0x5000) + bytes(12),
                 'flags': 3, 'align': 4}]
        a = self.arguments(make_elf(imports=WORKERS, sections=rows,
                                   relocations=[{'offset': 0x2000, 'type': 5}]))
        with self.assertRaisesRegex(ValueError, 'unmapped RELATIVE'):
            producer.emit_candidate(a)
        self.assertFalse(a.output.exists())

    def test_hidden_loader_export_is_not_silently_accepted(self):
        symbols = [{'name': 't5_driver_get', 'section': '.text', 'value': 0x1000, 'size': 4, 'type': 2},
                   {'name': 'hidden_object', 'section': '.data', 'value': 0x2000,
                    'size': 4, 'type': 1, 'visibility': 2}]
        a = self.arguments(make_elf(imports=WORKERS, symbols=symbols))
        with self.assertRaisesRegex(ValueError, 'loader-visible owner exports'):
            producer.emit_candidate(a)
        self.assertFalse(a.output.exists())

    def test_internal_getter_refused_by_actual_entry_contract(self):
        sections = [
            {'name': '.text', 'address': 0x1000, 'data': bytes(16), 'flags': 6, 'align': 4},
            {'name': '.risc.internal.text', 'address': 0x3000, 'data': bytes(16),
             'flags': 6, 'align': 4}]
        symbols = [{'name': 't5_driver_get', 'section': '.risc.internal.text',
                    'value': 0x3000, 'size': 4, 'type': 2}]
        a = self.arguments(make_elf(imports=WORKERS, sections=sections, symbols=symbols))
        with self.assertRaisesRegex(ValueError, 'getter must remain in ordinary .text'):
            producer.emit_candidate(a)
        self.assertFalse(a.output.exists())

    def test_source_and_tooling_pins_fail_closed(self):
        a = self.arguments(); (a.source / 'fixture.c').write_text('changed')
        with self.assertRaisesRegex(ValueError, 'pin mismatch'):
            producer.emit_candidate(a)
        self.assertFalse(a.output.exists())

    def copy_core(self, a):
        target = self.work / 'core-copy'; target.mkdir()
        for name in load_binding()['files']:
            dest = target / name; dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(a.admission_source / name, dest)
        a.admission_source = target
        return target / 'lib/elf_loader/src/esp_privileged_imports.c'

    def test_changed_core_source_refused(self):
        a = self.arguments(); path = self.copy_core(a)
        path.write_bytes(path.read_bytes() + b'\n')
        with self.assertRaisesRegex(ValueError, 'Core source pin mismatch'):
            producer.emit_candidate(a)
        self.assertFalse(a.output.exists())

    def test_core_source_drift_before_emission_refused(self):
        a = self.arguments(); path = self.copy_core(a); real_loader = producer.load_module
        def tampering(name, file):
            module = real_loader(name, file)
            if name == 'pinned_package_generator':
                real_prepare = module.prepare
                def prepare(*args, **kwargs):
                    result = real_prepare(*args, **kwargs)
                    path.write_bytes(path.read_bytes() + b'\n')
                    return result
                module.prepare = prepare
            return module
        with mock.patch.object(producer, 'load_module', side_effect=tampering), \
             self.assertRaisesRegex(ValueError, 'Core source pin mismatch'):
            producer.emit_candidate(a)
        self.assertFalse(a.output.exists())

    def test_original_path_change_cannot_change_emitted_snapshot(self):
        a = self.arguments(); original = a.elf.read_bytes(); real_loader = producer.load_module
        def replacing(name, path):
            module = real_loader(name, path)
            if name == 'pinned_import_extractor':
                a.elf.write_bytes(b'changed after immutable snapshot')
            return module
        with mock.patch.object(producer, 'load_module', side_effect=replacing):
            report = producer.emit_candidate(a)
        self.assertEqual((Path(report['package']) / 'driver.elf').read_bytes(), original)

    def test_generator_cannot_change_validated_elf(self):
        a = self.arguments(); real_loader = producer.load_module
        def tampering(name, path):
            module = real_loader(name, path)
            if name == 'pinned_package_generator':
                real_prepare = module.prepare
                def prepare(image, manifest, output, **kwargs):
                    result = real_prepare(image, manifest, output, **kwargs)
                    image.write_bytes(b'changed generated image')
                    return result
                module.prepare = prepare
            return module
        with mock.patch.object(producer, 'load_module', side_effect=tampering), \
             self.assertRaisesRegex(ValueError, 'validated immutable snapshot'):
            producer.emit_candidate(a)
        self.assertFalse(a.output.exists())

    def test_existing_output_is_preserved(self):
        a = self.arguments(); a.output.mkdir(); (a.output / 'sentinel').write_text('keep')
        with self.assertRaisesRegex(ValueError, 'fresh private output'):
            producer.emit_candidate(a)
        self.assertEqual((a.output / 'sentinel').read_text(), 'keep')

    def test_generator_cannot_change_manifest_contract(self):
        a = self.arguments(); real_loader = producer.load_module
        def tampering(name, path):
            module = real_loader(name, path)
            if name == 'pinned_package_generator':
                real_prepare = module.prepare
                def prepare(image, manifest, output, **kwargs):
                    result = real_prepare(image, manifest, output, **kwargs)
                    manifest.write_text('{}')
                    return result
                module.prepare = prepare
            return module
        with mock.patch.object(producer, 'load_module', side_effect=tampering), \
             self.assertRaisesRegex(ValueError, 'generated manifest differs'):
            producer.emit_candidate(a)
        self.assertFalse(a.output.exists())


if __name__ == '__main__':
    unittest.main()

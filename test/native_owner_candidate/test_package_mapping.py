"""Unexecuted constructed mapping regressions; not target owner qualification."""
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts/native_owner_candidate'))
from package_mapping import inspect_mapping
from elf_fixture import make_elf


def sections(value=0):
    return [{'name': '.text', 'address': 0x1000, 'data': bytes(16), 'flags': 6, 'align': 4},
            {'name': '.data', 'address': 0x2000, 'data': struct.pack('<I', value) + bytes(12),
             'flags': 3, 'align': 4}]


class MappingTests(unittest.TestCase):
    def test_ordinary_and_exact_exports(self):
        result = inspect_mapping(make_elf())
        self.assertEqual(result['exported_functions'], ['t5_driver_get'])
        self.assertEqual(result['mapped_sections'], ['.text', '.data'])
        self.assertEqual(result['allocation_capacity'], 35)
        self.assertFalse(result['physical_admission'])
        self.assertFalse(result['runtime_authority_granted'])
        self.assertFalse(result['requires_privileged_runtime_scope'])

    def test_zero_size_duplicate_is_refused(self):
        rows = sections() + [{'name': '.data', 'address': 0x3000, 'data': b'', 'flags': 3}]
        with self.assertRaisesRegex(ValueError, 'duplicate'):
            inspect_mapping(make_elf(sections=rows))

    def test_overlap_and_bad_alignment(self):
        for field, value in [('address', 0x1004), ('align', 3), ('align', 0x4000)]:
            rows = sections(); rows[1][field] = value
            with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                inspect_mapping(make_elf(sections=rows))

    def test_alignment_padding_budget(self):
        rows = sections() + [{'name': '.bss', 'address': 0x800000,
                             'kind': 8, 'size': 1, 'flags': 3, 'align': 0x800000}]
        with self.assertRaisesRegex(ValueError, 'padded image budget'):
            inspect_mapping(make_elf(sections=rows))

    def test_internal_shapes_and_budget(self):
        rows = sections() + [{'name': '.risc.internal.text', 'address': 0x5000,
                             'data': bytes(8), 'flags': 6, 'align': 4},
                            {'name': '.risc.internal.bss', 'address': 0x6000,
                             'kind': 8, 'size': 8, 'flags': 3, 'align': 8}]
        result = inspect_mapping(make_elf(sections=rows))
        self.assertEqual(result['allocation_capacity'], 35 + 12 + 15)
        self.assertFalse(result['internal_port_available'])
        rows[2]['flags'] = 7
        with self.assertRaisesRegex(ValueError, 'internal output class'):
            inspect_mapping(make_elf(sections=rows))

    def test_relative_null_mapped_bss_and_abs(self):
        relocation = [{'offset': 0x2000, 'type': 5}]
        for value in (0, 0x1000, 0x200f):
            self.assertEqual(inspect_mapping(make_elf(sections=sections(value),
                relocations=relocation))['relocations'][0]['initial_value'], value)
        rows = sections() + [{'name': '.bss', 'address': 0x3000, 'kind': 8,
                             'size': 4, 'flags': 3, 'align': 4}]
        result = inspect_mapping(make_elf(sections=rows,
            relocations=[{'offset': 0x3000, 'type': 5}]))
        self.assertTrue(result['relocations'][0]['zero_initialized'])
        for address in (0x60000000, 0x600fdfff):
            with self.subTest(peripheral=address):
                result = inspect_mapping(make_elf(sections=sections(address),
                    static_symbols=[{'name': 'peripheral', 'section': 'ABS', 'value': address}],
                    relocations=relocation), privileged_candidate=True)
                self.assertTrue(result['relocations'][0]['privileged_abs_mmio'])
                self.assertTrue(result['requires_privileged_runtime_scope'])
                self.assertFalse(result['runtime_authority_granted'])
                self.assertFalse(result['physical_admission'])

    def test_abs_does_not_authorize_ordinary_mapping(self):
        raw = make_elf(sections=sections(0x60000000),
            static_symbols=[{'name': 'peripheral', 'section': 'ABS', 'value': 0x60000000}],
            relocations=[{'offset': 0x2000, 'type': 5}])
        with self.assertRaisesRegex(ValueError, 'unmapped RELATIVE'):
            inspect_mapping(raw)
        with self.assertRaisesRegex(ValueError, 'boolean'):
            inspect_mapping(raw, privileged_candidate=1)

    def test_abs_exception_boundaries_and_evidence(self):
        for value, proof in [(0x60000000, False), (0x5fffffff, True), (0x600fe000, True),
                             (0x2000 + 16, True)]:
            with self.subTest(value=value, proof=proof), self.assertRaises(ValueError):
                inspect_mapping(make_elf(sections=sections(value),
                    static_symbols=([{'name': 'abs', 'section': 'ABS', 'value': value}] if proof else []),
                    relocations=[{'offset': 0x2000, 'type': 5}]), privileged_candidate=True)

    def test_all_rela_and_exact_span(self):
        for offset in (0x2001, 0x200e, 0x3000):
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                inspect_mapping(make_elf(relocation_name='.rela.debug',
                    relocations=[{'offset': offset, 'type': 5}]))

    def test_none_and_repeated_mutating_sites(self):
        inspect_mapping(make_elf(relocations=[{'offset': 0, 'type': 0}]))
        with self.assertRaisesRegex(ValueError, 'noncanonical NONE'):
            inspect_mapping(make_elf(relocations=[{'offset': 1, 'type': 0}]))
        with self.assertRaisesRegex(ValueError, 'repeated mutating'):
            inspect_mapping(make_elf(relocations=[{'offset': 0x2000, 'type': 5}],
                extra_tables=[{'name': '.rela.debug', 'relocations': [{'offset': 0x2000, 'type': 5}]}]))
        inspect_mapping(make_elf(relocations=[{'offset': 0x2000, 'type': 2},
                                             {'offset': 0x2000, 'type': 5}]))

    def test_hidden_functions_and_objects_are_actual_exports(self):
        symbols = [{'name': 't5_driver_get', 'section': '.text', 'value': 0x1000, 'type': 2},
                   {'name': 'hidden', 'section': '.text', 'value': 0x1004, 'size': 4,
                    'type': 2, 'visibility': 2},
                   {'name': 'object', 'section': '.data', 'value': 0x2000, 'size': 4,
                    'type': 1, 'visibility': 2}]
        result = inspect_mapping(make_elf(symbols=symbols))
        self.assertEqual(result['exported_functions'], ['hidden', 't5_driver_get'])
        self.assertEqual([row['name'] for row in result['loader_exports']],
                         ['t5_driver_get', 'hidden', 'object'])

    def test_global_function_full_extent_and_local_nonexport(self):
        symbol = {'name': 'bad', 'section': '.text', 'value': 0x100c, 'size': 8, 'type': 2}
        with self.assertRaises(ValueError):
            inspect_mapping(make_elf(symbols=[symbol]))
        symbol['bind'] = 0
        self.assertEqual(inspect_mapping(make_elf(symbols=[symbol]))['loader_exports'], [])

    def test_import_and_definition_relocations(self):
        result = inspect_mapping(make_elf(imports=['risc_cpu_worker_start_v5'],
            relocations=[{'offset': 0x2000, 'type': 4, 'symbol': 'risc_cpu_worker_start_v5'}]))
        self.assertEqual(result['relocations'][0]['import'], 'risc_cpu_worker_start_v5')
        with self.assertRaisesRegex(ValueError, 'RELATIVE requires canonical'):
            inspect_mapping(make_elf(relocations=[{'offset': 0x2000, 'type': 5,
                                                   'symbol': 't5_driver_get'}]))


if __name__ == '__main__':
    unittest.main()

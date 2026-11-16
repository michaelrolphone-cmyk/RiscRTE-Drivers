"""Strict fixed-Core binding schema; no caller-selected manifest authority."""
import json
from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts/native_owner_candidate'))
import core_binding as binding


class BindingTests(unittest.TestCase):
    def value(self):
        return binding.load_binding()

    def reject(self, value):
        with self.assertRaises(ValueError):
            binding.parse_binding(json.dumps(value).encode())

    def test_exact_committed_binding_accepted(self):
        value = self.value()
        self.assertEqual(binding.parse_binding(json.dumps(value).encode()), value)
        self.assertEqual(len(value['files']), 13)

    def test_exact_schema_fields_and_integer_required(self):
        for key in self.value():
            value = self.value(); del value[key]; self.reject(value)
        value = self.value(); value['extra'] = True; self.reject(value)
        for schema in (True, '1', 0, 2):
            value = self.value(); value['schema'] = schema; self.reject(value)

    def test_repository_commit_and_abi_identity_strict(self):
        for key, bad in (('repository', 'https://example.invalid/core'),
                         ('commit', '0' * 40), ('os_cpu_abi', True),
                         ('os_cpu_abi', '5'), ('os_cpu_abi', 2), ('os_cpu_abi', 3), ('os_cpu_abi', 4)):
            value = self.value(); value[key] = bad; self.reject(value)

    def test_exact_member_set_required(self):
        value = self.value(); value['files'].pop(next(iter(value['files']))); self.reject(value)
        value = self.value(); value['files']['../unowned.h'] = '0' * 64; self.reject(value)
        for revision in (3, 4):
            value = self.value(); value['files']['lib/elf_loader/include/private/privileged_os_cpu_symbols_v%d.def' % revision] = '0' * 64
            self.reject(value)

    def test_digest_syntax_and_duplicate_keys_refused(self):
        for bad in (None, True, 7, 'A' * 64, '0' * 63):
            value = self.value(); value['files'][next(iter(value['files']))] = bad; self.reject(value)
        raw = json.dumps(self.value()).replace('"schema": 1', '"schema": 1, "schema": 1', 1).encode()
        with self.assertRaisesRegex(ValueError, 'duplicate'):
            binding.parse_binding(raw)


if __name__ == '__main__':
    unittest.main()

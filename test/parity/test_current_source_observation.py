import copy
import importlib.util
from pathlib import Path
import sys
import unittest

# Reuse the existing actual-directory fixture, not a second hashing model.
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
import check_parity as parity
import test_source_parity as fixtures


class CurrentObservationTests(fixtures.SourceParityTests):
    def observed(self):
        return self.audit()['current_source_observation']

    def test_imported_is_exact_bytes_not_release_or_behavior_claim(self):
        row = self.observed()['rows'][0]
        self.assertEqual(row['status'], 'imported')
        self.assertIn('not established', row['intent_and_behavior'])

    def test_new_release_does_not_rewrite_historical_declaration(self):
        self.released['sample']['version'] = '2.0.0'
        report = self.audit()
        row = report['current_source_observation']['rows'][0]
        self.assertEqual(row['historical_release_declaration']['version'], '1.0.0')
        self.assertEqual(row['observed_upstream_release']['version'], '2.0.0')
        self.assertIn('released version mismatch: sample', report['errors'])
        self.assertEqual(self.inventory['drivers'][0]['version'], '1.0.0')

    def test_variant_retains_drift_and_does_not_infer_intention(self):
        (self.driver / 'driver.c').write_text('deliberate local behavior\n')
        report = self.audit()
        row = report['current_source_observation']['rows'][0]
        self.assertEqual(row['status'], 'variant')
        self.assertFalse(row['directory_drift_resolved'])
        self.assertIn('unknown', row['migration_qualification'])
        self.assertTrue(report['errors'])

    def test_missing_migration_has_exact_upstream_manifest(self):
        del self.inventory['drivers'][:]
        report = self.audit()
        row = report['current_source_observation']['rows'][0]
        self.assertEqual(row['status'], 'missing_local')
        self.assertEqual(row['upstream_source'], self.source['sample'])
        self.assertIn('source inventory mismatch: sample', report['errors'])

    def test_absent_upstream_is_not_deletion_authority(self):
        self.source.clear()
        self.assertEqual(self.observed()['rows'][0]['status'], 'upstream_absent')
        self.assertTrue((self.driver / 'driver.c').exists())

    def test_freshness_detects_pin_manifest_tree_and_missing_row(self):
        current = self.observed()
        self.assertEqual(parity.verify_observation(copy.deepcopy(current), current), [])
        for key in ('source_commit', 'release_index_commit', 'rows'):
            changed = copy.deepcopy(current)
            changed[key] = [] if key == 'rows' else 'f' * 40
            self.assertTrue(parity.verify_observation(changed, current))
        changed = copy.deepcopy(current)
        changed['rows'][0]['upstream_source']['version'] = '8.0.0'
        self.assertTrue(parity.verify_observation(changed, current))

    def test_historical_manifest_raw_bytes_bound_not_refreshed(self):
        manifest = self.root / 'manifest'; manifest.mkdir()
        p = manifest / 'released-drivers.json'; p.write_bytes(b'historical accepted declaration\n')
        before = self.observed()
        self.assertEqual(p.read_bytes(), b'historical accepted declaration\n')
        p.write_bytes(b'changed historical declaration\n')
        self.assertTrue(parity.verify_observation(before, self.observed()))

    def test_local_custody_keeps_path_identity_not_logical_id_union(self):
        custody = [{'path': 'Drivers/local_adapter', 'id': 'sample', 'state': 'custody'}]
        result = parity.current_observation(self.root, self.inventory, self.trees, self.source, self.released, fixtures.HEAD, fixtures.RELEASE, custody)
        self.assertEqual(result['local_custody_observations'], custody)
        self.assertEqual(len(result['rows']), 1)

    def test_cli_consumes_expected_observation_and_keeps_drift_failure(self):
        import json
        from unittest.mock import patch
        from contextlib import redirect_stdout, redirect_stderr
        import io
        manifest = self.root / 'manifest'; manifest.mkdir()
        self.inventory['parity_source'].update(branch='release-index', path='index.json')
        (manifest / 'released-drivers.json').write_text(json.dumps(self.inventory))
        (manifest / 'source-trees.json').write_text(json.dumps(self.trees))
        expected = self.root / 'expected.json'
        expected.write_text(json.dumps(self.observed()))
        output = self.root / 'report.json'
        args = ['check_parity', '--expected-observation', str(expected), '--output', str(output)]
        snap = (fixtures.HEAD, fixtures.RELEASE, self.source, self.released)
        with patch.object(parity, 'ROOT', self.root), patch.object(parity, 'snapshot', return_value=snap), patch.object(sys, 'argv', args), redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
            self.assertEqual(parity.main(), 0)
            (self.driver / 'driver.c').write_text('changed implementation')
            self.assertEqual(parity.main(), 1)
            report = json.loads(output.read_text())
            self.assertTrue(any('source drift' in e for e in report['errors']))
            self.assertTrue(any('frozen observation' in e for e in report['errors']))
            # Even a refreshed observation cannot waive real historical drift.
            expected.write_text(json.dumps(report['current_source_observation']))
            self.assertEqual(parity.main(), 1)
            self.assertFalse(any('frozen observation' in e for e in json.loads(output.read_text())['errors']))

    def test_missing_local_path_keeps_original_diagnostics(self):
        del self.inventory['drivers'][0]['source_path']
        report = self.audit()
        self.assertIn('invalid or duplicate source path: sample', report['errors'])
        self.assertIn('untracked local source directory: sample', report['errors'])
        row = report['current_source_observation']['rows'][0]
        self.assertEqual(row['acquisition_error'], 'invalid local source path')
        self.assertFalse(row['path_matches_upstream'])

    def test_duplicate_paths_are_never_directory_resolved(self):
        other = dict(self.inventory['drivers'][0], id='other')
        self.inventory['drivers'].append(other)
        self.source['other'] = dict(self.source['sample'], id='other')
        self.trees['drivers']['other'] = self.base
        report = self.audit()
        self.assertIn('invalid or duplicate source path: other', report['errors'])
        for row in report['current_source_observation']['rows']:
            self.assertFalse(row['directory_drift_resolved'])
            self.assertEqual(row['acquisition_error'], 'duplicate local source path')

    def test_baseline_only_orphan_stays_visible_unresolved(self):
        self.trees['drivers']['orphan'] = 'c' * 40
        report = self.audit()
        self.assertIn('source inventory mismatch: orphan', report['errors'])
        row = next(r for r in report['current_source_observation']['rows'] if r['id'] == 'orphan')
        self.assertEqual(row['recorded_baseline_tree'], 'c' * 40)
        self.assertEqual(row['status'], 'unresolved')
        self.assertFalse(row['directory_drift_resolved'])
        self.assertIsNone(row['upstream_source'])

    def test_same_tree_different_path_not_silent_import(self):
        self.source['sample']['source_path'] = 'Drivers/other'
        self.assertEqual(self.observed()['rows'][0]['status'], 'variant')


if __name__ == '__main__':
    unittest.main()

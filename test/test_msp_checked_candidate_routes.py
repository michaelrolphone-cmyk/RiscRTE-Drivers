"""Candidate output identities and immutable historical producer separation."""
import ast
import json
from pathlib import Path
import subprocess
import sys
import unittest
import yaml
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from verify_usb_hid_preservation import preserved_bytes, verify_manifest

BASE = 'a7ae39994603c4545a3c014f09051ecfc12084d2'
class CandidateRoutes(unittest.TestCase):
    def test_historical_builders_and_accepted_bytes_unchanged(self):
        for driver in ('usb_msp', 'program_msp'):
            path = f'scripts/build_{driver}.py'
            expected = subprocess.check_output(['git', '-C', str(ROOT), 'show', BASE+':'+path])
            self.assertEqual((ROOT/path).read_bytes(), expected)
        self.assertEqual(subprocess.check_output(['git', '-C', str(ROOT), 'diff', BASE, '--', 'dist', 'release']), b'')
        verify_manifest(ROOT, BASE)
        path = 'manifest/released-drivers.json'
        baseline = subprocess.check_output(['git', '-C', str(ROOT), 'show', BASE+':'+path])
        # Share the exact allowed addition and canonical serialization checks with
        # the PR40 guard, then require the historical inventory's original bytes.
        self.assertEqual(preserved_bytes(path, (ROOT/path).read_bytes()), baseline)
    def test_candidate_identities_and_paths(self):
        for driver in ('usb_msp', 'program_msp'):
            manifest = json.loads((ROOT/f'Drivers/{driver}/manifest.json').read_text())
            self.assertEqual(manifest['version'], '0.1.2')
            self.assertEqual(manifest['status'], 'experimental-local-checked-consumer')
            source = (ROOT/f'scripts/build_{driver}_checked_consumer.py').read_text()
            tree = ast.parse(source)
            assignments = {n.targets[0].id: n.value for n in tree.body if isinstance(n, ast.Assign) and isinstance(n.targets[0], ast.Name)}
            required = ast.literal_eval(assignments['required'])
            self.assertEqual(required, manifest)
            self.assertIn('dist/'+manifest['id']+'-checked-consumer', ast.unparse(assignments['OUT']))
            self.assertNotIn('CANONICAL_SHA256', source)
            self.assertIn('canonical_byte_parity_claim=False', source)
    def test_workflow_separates_current_from_historical(self):
        workflow = yaml.safe_load((ROOT/'.github/workflows/driver-parity.yml').read_text())
        steps = workflow['jobs']['historical-release-parity']['steps']
        historical = next(s for s in steps if s.get('name') == 'Check out immutable historical MSP producers')
        self.assertEqual(historical['with']['ref'], BASE)
        self.assertFalse(historical['with']['persist-credentials'])
        producer = next(s for s in steps if s.get('name') == 'Require unchanged historical MSP release bytes')
        self.assertEqual(producer['working-directory'], '.source/legacy-msp')
        for driver in ('usb_msp', 'program_msp'):
            self.assertIn(f'build_{driver}.py --require-byte-parity', producer['run'])
        all_steps = [s for j in workflow['jobs'].values() for s in j['steps']]
        for driver in ('usb_msp', 'program_msp'):
            self.assertTrue(any(s.get('run') == f'python scripts/build_{driver}_checked_consumer.py' for s in all_steps))
if __name__ == '__main__': unittest.main()

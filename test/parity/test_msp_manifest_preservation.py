"""MSP invokes the same custody policy as HID, keeping its historical baseline."""
import importlib.util
import pathlib
import sys
import unittest
from unittest.mock import patch

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
import verify_manifest_preservation as cli
spec = importlib.util.spec_from_file_location('custody_regressions', ROOT / 'test/parity/test_custody_manifest_preservation.py')
base = importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)
MSP = 'a7ae39994603c4545a3c014f09051ecfc12084d2'

class MSPManifestPreservation(base.CustodyManifestPreservation):
    def setUp(self):
        super().setUp()
        # These two historical manifest trees are identical; only the caller pin differs.
        self.baseline = MSP
    def verify(self):
        with patch.object(cli, 'ROOT', self.root):
            cli.main(['--baseline', self.baseline])
    def test_workflow_preserves_other_checks(self):
        workflow = (ROOT / '.github/workflows/driver-parity.yml').read_text()
        self.assertIn('git diff --exit-code ' + MSP + ' -- dist release', workflow)
        self.assertIn('python scripts/verify_manifest_preservation.py --baseline ' + MSP, workflow)
        self.assertIn('python test/test_msp_checked_candidate_routes.py', workflow)
    def test_msp_route_guard_shares_helper_and_keeps_byte_lock(self):
        routes = (ROOT / 'test/test_msp_checked_candidate_routes.py').read_text()
        self.assertIn('verify_manifest(ROOT, BASE)', routes)
        self.assertIn("'diff', BASE, '--', 'dist', 'release'", routes)
        self.assertIn("self.assertEqual(preserved_bytes(path, (ROOT/path).read_bytes()), baseline)", routes)
    def test_baseline_required(self):
        with self.assertRaises(SystemExit): cli.main([])

if __name__ == '__main__': unittest.main()

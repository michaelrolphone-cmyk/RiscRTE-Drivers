"""Keep the corrected producer distinct from immutable release replay."""
import json
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]


class ProgramMspReplay(unittest.TestCase):
    def test_candidate_identity_does_not_replace_canonical_digest(self):
        manifest = json.loads((ROOT / 'Drivers/program_msp/manifest.json').read_text())
        self.assertEqual(manifest['version'], '0.1.1')
        builder = (ROOT / 'scripts/build_program_msp.py').read_text()
        self.assertIn('"version":"0.1.1"', builder)
        self.assertIn('CANONICAL_SIZE=9128', builder)
        self.assertIn('19b0999f41e45522e877097addf3cfd55651b2fd00ae925fa6084b769b66527f', builder)
        self.assertIn('canonical_version="0.1.0"', builder)
        self.assertIn('if REQUIRE_BYTE_PARITY and not byte_parity:', builder)

    def test_replay_uses_pinned_separate_producer(self):
        workflow = (ROOT / '.github/workflows/driver-parity.yml').read_text()
        current, historical = workflow.split('  historical-release-parity:', 1)
        self.assertIn('python scripts/build_program_msp.py', current)
        loop = re.search(r'for driver in (.*?); do', historical).group(1).split()
        self.assertNotIn('program_msp', loop)
        self.assertIn('ref: a7ae39994603c4545a3c014f09051ecfc12084d2', historical)
        self.assertIn('path: .source/legacy-program-msp', historical)
        self.assertIn('python .source/legacy-program-msp/scripts/build_program_msp.py --require-byte-parity', historical)


if __name__ == '__main__':
    unittest.main()

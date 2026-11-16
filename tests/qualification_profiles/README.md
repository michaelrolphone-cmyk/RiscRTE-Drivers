# Qualification observer profiles (test-only)

`local-quick` preserves the existing three local Quick observer files exactly.
They are test overlays, not firmware, and are deliberately separate from shared
Drivers production/test source. `profile.json` locks every override byte and
identifies the existing runner. Never hand-copy another fixture directory.

## Set up before any lanes

From the selected Drivers tooling checkout:

```sh
python scripts/qualification_fixture.py \
  --profile local-quick --source /absolute/selected-drivers \
  --source-revision <exact-40-character-commit> \
  --output /absolute/build/local-quick-fixture
```

The source must be a clean tracked Git worktree at that exact revision. Setup
validates all three bundled observers first, creates a complete overlay in a
sibling temporary directory, verifies it, then renames it into place. Shared
source files are linked to the selected checkout; scripts/tests are real overlay
directories, preserving the existing fixture structure. Only tracked shared
entries are materialized (no copied build outputs or __pycache__). The receipt
records the source checkout, exact commit/tree, profile hash, observer hashes,
and exact shared-link layout. The runner still records its own used dependency
hashes; this setup receipt does not replace execution or target qualification.

Re-running with identical verified output is idempotent. `--check` fails if
setup is missing and performs no repair. Missing/tampered observers, wrong links,
extra output files, source mismatch or dirty tracked source fail closed. Existing
manual overlays without the new receipt are intentionally not adopted silently:
keep them as historical evidence and choose a fresh output path.

## Mandatory per-lane preflight

Use the launcher rather than invoking the copied runner directly:

```sh
python scripts/qualification_fixture.py \
  --profile local-quick --source /absolute/selected-drivers \
  --source-revision <exact-commit> --output /absolute/build/local-quick-fixture \
  --check --run -- <the existing execution runner arguments>
```

Every launch rechecks the source and entire overlay before starting the unchanged
observer runner. Python bytecode writes are disabled. To intentionally bind to a
new selected checkout/commit, run setup with `--rebind` first. It verifies the old
overlay's observer hashes and links against its recorded Git tree, then constructs
a new overlay; it never replaces tampered/missing output by pretending it was
valid. Rebinding is not concurrent-safe with a running lane; use separate output
paths or wait for its owner. Do not edit a source checkout during qualification.

Tests: `python scripts/test_qualification_fixture.py`. Includes fresh/existing,
missing/tampered profile and output, unsafe links, source rebinding (new checkout
and same checkout/new commit), forged provenance, and launch blocked before any
runner callback on failed preflight. No firmware build or product lane is run.

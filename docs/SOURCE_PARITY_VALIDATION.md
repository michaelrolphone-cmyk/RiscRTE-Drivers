# Source-aware driver parity validation

The prior gate checked recorded source-tree hashes against upstream but did not
hash local implementation files. A local implementation edit could therefore
pass with stale inventory metadata. Separate reads of mutable master could also
combine trees and manifests from different commits. This increment closes both
gaps without changing any driver payload, SDK, manifest version or released bytes.

## Current behavior

`python scripts/check_parity.py --output dist/source-parity.json` resolves master
and release-index exactly once, then uses those commit SHAs for all reads.
Each subprocess has a 60-second timeout; a read failure ends the gate. Upstream
manifest failures and duplicate IDs are errors, not skipped inventory entries.

The source-tree configuration explicitly lists `Drivers/common` as a shared
non-package directory. Discovery skips only those exact configured paths, and
fails if a configured directory disappears. Any other upstream directory still
requires a valid driver manifest; a missing manifest remains a hard failure.

The gate hashes actual local driver directory bytes recursively using Git tree
ordering and executable modes. It includes untracked files within those source
directories and detects added/missing files, extra source directories, changed
contents and executable-bit drift. Symlinks/special files and escaping/duplicate
inventory paths fail closed. It does not change the index, Git objects or sources.

Each report row contains the package ID, path, recorded baseline tree, actual
local tree, immutable upstream tree and a three-way drift classification.
Upstream-only, external-only and conflict differences must be reconciled by the
maintainer; the gate never copies source. Converged bytes still require the
recorded tree baseline to be updated intentionally. Released/source versions
remain separate when `source_version` explicitly records an unreleased change.

CI runs the negative regression tests before parity and preserves the JSON
report for 14 days, including drift diagnostics when validation fails. If reads
fail before a complete snapshot exists, no misleading report is manufactured;
the failed job/log is the evidence. Artifact upload cannot turn a failed gate green.

## Offline immutable audit

With an existing read-only Reader checkout containing both commits:

```sh
python -m unittest discover -s test/parity -v
python scripts/check_parity.py --reader /path/to/Reader \
  --source-ref 1e0188c1ff0234dd33fe054c9a6fb4fde36596df \
  --release-ref 572746f4fcf3fde19947a066b7e5c8028cd76d21 \
  --output dist/source-parity.json
```

The fresh local audit finds 21 released identities and 23 actual driver trees in
parity at those snapshots. Ten regression tests cover local tampering with stale
metadata, add/delete/mode changes, Git-compatible nested tree hashing, upstream
and conflicting edits, duplicate inventory, source/release version separation,
unsafe paths/symlinks, immutable reads and failed reads. CI independently repeats
the real network-backed audit against newly resolved refs.

## Readiness boundary and provenance

Target base is `c094089c7f0eccc2ff25c52cf0dafede3100b006`; maintenance claim #7
owns this validation-only increment. Final exact-head and postmerge CI evidence
is recorded in that claim and the PR. No distributable inputs change, so no
package version is incremented and no new published identity is asserted.

This proves driver source inventory/bytes at a snapshot, not SDK/header provenance,
ABI compatibility, dependency resolution, runtime behavior, package ZIP validity
or publication/cutover readiness. Existing independent ELF/build/host checks stay
separate and unchanged. Existing historical application byte mismatches remain
unresolved and are not affected by this work.

The continuation handoff reports prospective U1 CDC candidate `5466d93c`, canonical
`usb-cdc-acm` 0.1.8 and Driver Manager 1.0.8 with target CI pending. Those are not
current master parity inputs. No candidate code or metadata is imported here;
future synchronization requires re-inspecting the actually merged source.

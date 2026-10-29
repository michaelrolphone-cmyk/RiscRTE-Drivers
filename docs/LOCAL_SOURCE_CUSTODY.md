# Local-only source custody

This independent unit classifies eight existing local source directories without
changing their files, historical source locks, released inventory, accepted
release bytes, or upstream source-tree baseline. It does not migrate upstream
inventory or reconcile actual implementation drift.

`manifest/local-source-custody.json` records complete Git tree hashes and exact
repository/commit/path provenance for two manifestless support directories, five
local adapters, and the native CDC candidate. Classification is explicit by path,
not a prefix exemption. Actual bytes and executable modes must match. New siblings,
unsafe or duplicate paths, unsupported kinds, malformed provenance, missing trees,
symlinks, inserted support manifests, adapter identity collisions, candidate lineage
errors and manifest identity/version mutations fail closed.

The native CDC candidate intentionally shares `usb-cdc-acm` with the historical
source entry, through explicit `candidate_for`. It does not replace that entry or
resolve its source-path, version, inventory or implementation diagnostics. Custody
rows are separate from upstream package rows and explicitly claim neither upstream
parity nor release qualification. Custody is not a build, runtime, hardware, artifact
or publication acceptance decision. The gate verifies the provenance repository and resolves each exact historical
commit/path to its recorded tree, rejecting rewritten hashes and missing objects. The source history must be present (CI uses full-depth checkout).

## Exact diagnostic comparison

On 2026-10-24 the immutable upstream source commit
`9c8783d6f20659a514bfaeb3847a77c7213dc4f6` and release-index commit
`19bb3dcb5cb763c4eacd884ad5fc0bcab57ff2ed` reproduce the PR44
`f1b207ac6553609e4a313d1ed7f4ca09aeebd89c` baseline's 65 diagnostics exactly.
With custody validation, only these eight untracked-directory diagnostics disappear:

- `legacy_display_power`
- `legacy_power_compat`
- `legacy_power_key`
- `legacy_power_sleep`
- `pcm_audio_common`
- `pcm_audio_input`
- `pcm_audio_output`
- `usb_cdc_native`

All 57 remaining diagnostics are unchanged: 46 source/release metadata migration
items and 11 actual source divergences. No added errors, blanket waiver, rewritten
historical metadata or source/release cutover is involved. The parity gate remains
red, correctly. Repository regression fixtures retain the exact inspected fields
(ID/version/path/tree) from those immutable upstream reads, plus the complete
baseline diagnostic set; they are not a claim about later moving upstream heads.

## Reproduce

Run `python -m unittest discover -s test/parity -v` and
`python scripts/verify_usb_hid_preservation.py` from the repository root.
The pinned-snapshot regression compares complete before/after diagnostic sets,
verifies unchanged upstream package report rows and frozen inventory bytes, and
rejects content/add/delete/mode mutations across all eight custodied directories.
The ordinary parity command continues to resolve the current upstream commits.

## Remaining work

The separate inventory migration must reconcile 46 metadata diagnostics without
rewriting frozen historical acceptance records. Eleven implementation divergences
require source-specific reconciliation and qualification. No historical replay,
new firmware build, hardware qualification, release, merge or publication is claimed
by this custody-only unit.

## Current-tree reconciliation (2026-10-29)

The historical witness remains pinned to `f1b207ac6553609e4a313d1ed7f4ca09aeebd89c`: 65 diagnostics before custody, 57 afterward. It is not rebound to a newer checkout. Against the retained current PR46 input `5f26ed6aa2735bc0c42da60173d5210774cde7c9`, with the same explicit source/release fixture inputs, there are 66 diagnostics before and 58 afterward. The additional later `time_sample` custody gap is still visible; this unit does not invent provenance for it. Both regressions compare diagnostic multiplicities, not only unique sets, and preserve the complete upstream classification and frozen inventory bytes. This is not a claim that moving upstream inputs have been re-audited.

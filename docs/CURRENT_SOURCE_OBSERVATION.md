# Current-source observation versus historical release replay

The normal `scripts/check_parity.py --output ...` consumer now emits
`current_source_observation` alongside its existing report. No new inventory,
source baseline, release declaration, accepted-byte lock or driver is substituted.
The existing error list and failing exit status remain authoritative. This is a
scoped inventory representation, not a declaration that migration is complete.

Each logical package row preserves the complete local declaration, complete
pinned upstream source manifest/tree, historical release declaration, and
observed upstream release entry separately. In particular, a newer upstream
release is not evidence that the historical accepted release should be rewritten.
Existing independent historical byte-preservation/replay jobs remain required;
the observation's raw declaration hashes are not a substitute for those jobs.

Directory statuses are `imported` (same path and Git tree), `variant` (different
path and/or tree), `missing_local`, `upstream_absent`, or `unresolved` (including malformed/duplicate paths and baseline-only orphans). Path and
byte comparisons are separately exposed. An imported directory does not prove
ABI/build/runtime migration. A variant is not automatically intentional, safe,
or authorized; the report explicitly leaves that judgment unestablished.
Local custody rows and declarations retain their separate path identity,
provenance, candidate_for relationships, and non-release qualification. Original
experimental sources are separately retained. Untracked paths still fail.

For an exact repeat, save the `current_source_observation` object and pass it as
`--expected-observation path.json`. This compares all pins, manifests, paths,
actual directory hashes, custody, and historical declaration hashes. A changed
upstream commit is stale even if its directory bytes happen to match. A failed
freshness check adds an error; a successful check cannot waive any original
source or release diagnostic. A missing/malformed observation fails, not skips.

This first migration step resolves representational conflation, not outstanding
source work: historical and current release versions can now be consumed as
separate facts, and missing/current/variant sources are explicit. Existing version
mismatch diagnostics are deliberately retained until technical design review of the consuming validator
change maps them to the appropriate current-source or historical replay gate.
No claim is made that the 46 metadata migration issues, 11 source divergences,
or untracked time_sample have been fixed by this addition.

Technical review here is ordinary implementation review within task0224, not a
new requirement for user approval of each metadata fix.

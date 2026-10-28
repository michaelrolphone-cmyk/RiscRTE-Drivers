# Explicit owning sources for a reused store

A native-only successor can retain an already qualified, byte-identical app
store without pretending its host scenarios ran again. The packager accepts
`--target-driver-source PATH --target-driver-revision EXACT_COMMIT` together.
Both default to the current Drivers tree when omitted, preserving prior behavior.

The selected target owning tree must be clean, at the exact supplied commit,
with the exact source lock and every recorded store-build source input hash.
Native source hashes still validate against the packager's current owning tree.
The output records the target owning commit/tree and all input hashes separately.
No mismatch is silently ignored; dirty sources, wrong commits/locks/hashes,
escaping paths and empty source inventories are rejected.

This is source provenance, not a behavioral equivalence waiver. The package still
requires exact store bytes, admission against the actual new native ELF, unchanged
Runtime/source contracts, complete host execution receipts and all existing
focused gates. Prior host receipts remain identified as reused observations.
Changed native behavior additionally needs its own real-source tests and selected
composition coverage. The four-row candidate has normal/sanitizer/target backend
coverage and actual facade-to-native frame/cancellation/resume coverage; these
are host software observations, not physical DMA, timing or sleep acceptance.

For this successor, the old fe37 store was rebuilt from the new source and all52
files compared byte-for-byte identical. The immutable old store/build receipt is
used for retained host evidence, with its owning source explicitly pinned. New
native admission is run separately. This avoids relabeling a new build-metadata
hash as an old test result or requiring unrelated host tests solely because the
native source inventory recorded alongside the store has changed.

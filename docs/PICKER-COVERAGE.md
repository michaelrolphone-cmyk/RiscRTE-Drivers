# Picker coverage and short-contact stress are separate contracts

The normal picker execution lane retains all34 face identities, >10 preview
size changes, all five categories, cancellation/return, no persistent writes,
OOM retry, malformed contact recovery and terminal-custody assertions.

It supplies only raw CST contacts. Each point is held until the real picker
reports that coordinate and a completed frame acknowledges progress. Eight
movement segments, each held at least20 ms after acknowledgement, preserve
observable displacement; a120 ms endpoint dwell
removes dependence on residual fling velocity. Each vertical gesture must end
at exactly the expected next category before the driver advances. Each gesture
has a15-second progress-failure watchdog. Failed acknowledgements do not become
success, retries, or synthetic application-state changes. The existing opening
holds, OOM retry and malformed input remain explicit modeled boundaries.

`--timed-stress` replays the original400 ms fixed-time drags and700 ms neutral
intervals. This lane reports its exact seen-face mask, size changes and category
mask, but does not claim all-face coverage. It still asserts navigation,
cancellation/return and custody safety. The former combined assertion failure
is retained in the diagnostic evidence, not relabeled as a historical pass.

Under the deliberately blocking120 ms display-row fault, the old32-row facade
and new128-row facade can observe different portions of the same400 ms contact.
Observed step37 in steady picker (not startup): rawDOWN at cycleoffset100 ms;
first acquisition174 ms vs212 ms gives74 ms vs112 ms delay. Observed vertical
travel254 px vs211 px straddles the233 px nearest-category threshold. Both
clear stale velocity after>90 ms. This is genuine acquisition-latency evidence,
not proof every hardware position was preserved. These holds are coverage-driver pacing, not product timing contracts or latency
qualification. No picker physics is changed.
The native facade's backend.row ABI does not take the facade remaining-time
argument; the120 ms host injection deliberately exceeds the8 ms owner budget.
It is not a normal16-row QSPI payload-time measurement or physical FPS result.

## Cancellation recovery

If the actual picker or Quick neutral gate reports cancellation during an
acknowledged gesture, the coverage driver emits real rawUP/neutral. It waits
until both recognizers are neutral, the picker remains open, Quick is not
visible, and a new frame has completed. Only then does it restart the same
unfinished gesture from a fresh DOWN. The original gesture watchdog start is
unchanged; no GAP, identity, resize or expected-category assertion is skipped.
The malformed lane requires observed cancellation and completed neutral recovery.

The runner may relocate a recorded Home source root to an immutable worktree
with the same selected commit. Every relocated source still must match the
original target recipe SHA256, definitions, source list and Home file manifest;
recorded compilation identities are not rewritten. This avoids live-integration
source races while preserving exact source admission.

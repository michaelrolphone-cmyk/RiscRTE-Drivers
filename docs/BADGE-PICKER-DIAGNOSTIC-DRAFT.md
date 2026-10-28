# Raw-CST picker reproducer: normal software qualification

Based on Drivers6b79973a1b807f24440a98e0460741fdf6cdb3c2; selected immutable
Watch03aa63fb4830987c854e05a878c00c8cdde9577c and
Systembe142dbec17881ac09e4b1617606d161a8bf5204 remain unchanged.

New scripts/test_badge_picker_execution.py derives from the existing exact
product execution runner. It consumes the original target compiler recipes,
checks their input hashes, recompiles production Home/Runtime/providers for
host execution and adds output-local read-only picker/preview probes. A
host-only first-allocation refusal is explicit; source snapshot/ELFs are not
modified. Actual compiled feature selectors remain those recorded by target.

Four trajectories: all-category/card movement and cancel/return;120ms modeled
per-frame display delay; first allocation fails then fresh neutral/hold retries;
malformed raw-CST multicontact report then neutral/recovery. Inputs are written
to actual hardware report bytes, not directly to picker state. Five collection
mask, all34 cached-face IDs, changed positions, repeated native preview sizes,
completed picker frames and long-key cancel → swipe → Springboard → short-key
return to fresh Home are asserted. These trajectory expectations must still be
validated, not weakened if initially wrong.

Update18:48UTC: the normal four-case matrix compiled and passed. Exact receipt
and preserved earlier fixture failures are in /tmp/badge-picker-evidence.
Sanitizer and typed terminal-fault extension remain pending. The long95-gesture traversal and120-second subprocess wall
limit may need adjustment based on measured host runtime, without turning it
into hardware timing. Full product fixture uses RAM preferences and POSIX
AppData, not actual flash. Final success exits at a substantive completed third
Home frame; prior two invocations must have exited, while final Home is live.

Typed malformed reports traverse production error handling. Unknown/retained
provider-result injection is not yet independently implemented in this new
suite; inherited provider-custody assertions remain. Add a focused bounded
terminal variant before claiming full retained-path coverage. No crash cause,
performance repair or physical success is established by this source draft.

Next: coordinate capacity for sanitizer and terminal-fault extension. Normal
trajectory setup corrections and exact failed attempt logs are preserved. Keep source-proven product faults distinct from fixture
failures and no reproduction. No source-publication retry is authorized here.

Update18:56UTC: original four cases passed both normal and ASan/UBSan, with leak
checking explicitly disabled. Added distinct case4: after actual stream next
returns during picker movement, host-only boundary overrides its result to
terminal−2. Production classifier retains; actual Runtime reports failed and
retained; hardware callback count stays fixed. Normal and sanitized expected-
retained receipts passed separately. This is not a fifth ordinary UI success,
not a real hardware fault and not a reproduction of the reported crash.

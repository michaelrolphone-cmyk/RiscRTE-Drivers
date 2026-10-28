# Focused helper qualification, 2026-10-25

Scope: `foreground_light_sleep.c`, the real `wake_key_filter/driver.c` with the
negotiated neutralize suffix, and checked host provider/Runtime fixtures. No
hardware or remote state was changed.

Sources used:

- Runtime `7c6b0ac3cf309fea9e690a8270562557d4ebc376`.
- System foreground-idle worktree based on
  `6e0e4c9b64047ff7be43e8d26ec8ecd588e4c19e`, with its shared idle adapter changes.
- Current local Drivers foreground-idle worktree; historical delivery unchanged.
- Host compiler: Debian GCC 14.2.0.

The following commands passed, each compiling the standalone helper as well as
the fixture. `RUNTIME` is the canonical Runtime source above and `SYSTEM` is the
new System foreground-idle worktree.

```
python3 scripts/test_portable_light_sleep.py --runtime "$RUNTIME" --system "$SYSTEM"
python3 scripts/test_portable_light_sleep.py --runtime "$RUNTIME" --system "$SYSTEM" --sanitize address
python3 scripts/test_portable_light_sleep.py --runtime "$RUNTIME" --system "$SYSTEM" --sanitize undefined
```

Both sanitizers use fail-fast diagnostics. LeakSanitizer is explicitly disabled:
this executor rejects its ptrace-based leak scan. An initial leak-scan attempt
failed for that environmental reason, not a reported leak. The helper and fixture
perform no heap allocation; the standalone object is separately checked for
allocation/free dependencies. ASan memory-safety and UBSan checks passed.

Coverage:

- 1,000 sequential Light cycles through one live instance of the real wake-key
  filter, without provider restart, consuming held wake, release, late SHORT and
  LONG latches; only a fresh observed press rearms subsequent application events.
- Provider-owned image restoration at 466×466, 240×240, 317×159 with 13-byte row
  padding, 3×2 with padding, and 1×1 with padding. Original completed framebuffer
  remains byte-identical. The helper never invokes output/gauge operations.
- Original in-memory draft/state canary remains unchanged.
- Canonical native transaction token pairing, BUSY/UNSUPPORTED pre-entry refusal,
  MEDIA_UNAVAILABLE clean restoration, and successful GPIO/OTHER Light wakes.
- Failed display preparation still resumes display; failed sleep preparation
  resumes sleep and display; known entry refusals restore both in reverse order.
- Truncated/absent Runtime transaction or key-neutralize suffix refuses safely.
- Light CONTEXT, retained/unknown statuses, malformed wake sizes/causes, dirty or
  missing native tokens, failed acquisition, failed neutralize, failed cleanup,
  and failed/uncleared releases all fence immediately. Exact callback traces
  prove no provider operation follows the first uncertainty, including repeated
  calls after terminal retention.
- No allocation/free symbols or direct Deep/timed/handoff dependencies in the
  standalone helper object. Deep/timed callbacks in the fixture are absent; all
  successful cycles use only `light_sleep`.

These are focused software results. Actual target build, linked Runtime graph
execution, physical current draw, wake reliability and electrical display
behavior are separate qualifications. Geometry restoration tests model the
selected provider contract; they do not replace CO5300 backend qualification.

## Timer and key-provider regression checks

The new preference test runner exercises all 3,596 valid encoded seconds with
fallbacks 5, 15 and 3600, normal and ASan/UBSan; malformed versions/checksums/
lengths, missing-only fallback, clean unavailability, transient BUSY/IO recovery,
CONTEXT/unknown results, and failed or uncleared release. Pure cached queries
and confirmed-save updates perform no acquisition or writes.

The existing provider service suite also passes normal and ASan/UBSan with the
new key-neutralize suffix. The new test fails against the exact delivered
5df9e17 filter (no live rearm contract) and passes with 1,000 repeated
held/released/late-latch cycles. The suffix preserves the original base ABI.

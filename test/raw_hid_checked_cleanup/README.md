# Raw HID checked-cleanup composition qualification

Run from a Drivers checkout containing the two exact host commits:

```sh
python3 test/raw_hid_checked_cleanup/run.py
SANITIZE=1 python3 test/raw_hid_checked_cleanup/run.py
```

The runner builds separate ELF shared objects with C11, `-O1`, strict warnings,
and hidden visibility. It does not use the shipping legacy raw HID source.
The candidate comes from `candidates/usb-hid-checked-cleanup/driver.c` and keeps
`usb-hid`, `usb.hid@1`, the unchanged provider ABI, and manifest version 0.1.3.

## Exact inputs and scope

- Required host: Drivers PR40, `90e7a8faae1a6678a24c92c32f95032b2fbe1e24`.
- Distinct regression host: Drivers PR41, `501e22b5bc41d39fee6b15346a25a87ab7ad5df3`.
- Unchanged mouse: Reader `c334919a05d393b7b3a536fceb117cfbb573a4ef`.
  `pinned_reader/` contains the exact source and transitive SDK headers retrieved
  through the GitHub connector. The source manifest itself is SHA-256 pinned;
  each file's Git blob SHA is checked before compilation. These are read-only
  test inputs, not an installed mouse provider or a new SDK/ABI.
- Host and candidate compile against exact Drivers SDK snapshots. Mouse and
  the harness compile independently against exact Reader headers. No header
  merging, ABI expansion, provider implementation substitution, or ref changes
  occur. Host code is extracted from exact Git objects, never current HEAD.

The only behavioral fake is a deterministic controller boundary, plus a
monotonic clock. Transparent observation tables record calls and then invoke
the actual host or raw provider. Legacy release/control sentinels deliberately
abort if called. This proves host-native serialized composition semantics,
not physical USB, IDF callback timing, target installation, or hardware parity.

## Required PR40 cases

1. Dependency/count/capability/API rejection, seven missing callback refusals,
   genuinely heap-sized short tables at every byte length from the header
   through the checked prefix, and optional legacy callbacks absent.
2. Exact-token checked close failure, revoked raw I/O/presence, foreign/stale
   token refusal, preserved receipt after host graph cleanup, and close/reopen.
3. False/nonzero failed-open custody, immediate cleanup failure, unpublished
   orphan pinning, and exact-token quiesce retry after physical cleanup.
4. False/nonzero failed-open custody with immediately successful cleanup.
5. False/zero failed claim without manufactured cleanup custody.
6. Negative, short, and oversized checked-control responses, correct nonzero
   interface recipient, wrong-recipient/stale/endpoint denial, no legacy fallback.
7. Detach, failed cleanup and exact-token retry, then generation-safe reattach.
8. Unchanged mouse quiesce retaining its exact raw session on failed close.
9. Mouse detach cleanup retry performing only the same-token close.
10. Mouse setup-control failure retaining its raw session through failed close.
11. Mouse three-attempt terminal retention, with no subsequent dependency calls.

Four source mutations must be rejected by their specific assertions: false
successful close dropping custody, failed-open custody discard, legacy device
control fallback, and accepting a shortened checked prefix. Compilation or
unrelated loader failure is not accepted as a mutation-test success.

## PR41 is not interchangeable with PR40

PR41 passes the nine applicable compatibility cases. Its host discards a
controller's nonzero custody output when claim returns false. Two explicit
negative witnesses require that gap to remain observable: raw receives zero,
no checked release is called, the physical claim remains live, and controller
quiescence prevents host unload. They are recorded as
`expected-host-custody-gap-observed`, not as full PR41 qualification. A raw HID
consumer cannot repair a custody token that its host never returned.

## Evidence and sanitizers

Each run writes portable normalized logs, SHA-256 log hashes, exact source and
SDK digests, compiler/options, binary hashes, and per-case results beneath
`dist/raw-hid-checked-cleanup-ci/{normal,asan-ubsan}/`. `results.sha256` protects
`results.json`. Any required case, unexpected witness result, pin failure,
compiler error, or unrelated mutation failure exits nonzero.

ASan and UBSan are enabled together. By default LSan is disabled because the
local executor uses ptrace, which LSan does not support. This is not a leak
qualification. `ASAN_OPTIONS` can override that default on supported CI hosts,
for example `ASAN_OPTIONS=abort_on_error=1 SANITIZE=1 python3 .../run.py`.
Terminal-retention cases intentionally keep providers mapped until process exit,
matching the reboot-only boundary rather than pretending cleanup succeeded.

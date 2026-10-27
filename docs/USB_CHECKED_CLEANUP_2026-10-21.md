# USB checked cleanup: isolated local prerequisite

## Result and boundary

This candidate implements checked host claim closure and a native IDF 6.1
nonblocking transfer-cleanup path. A timeout no longer appends a fixed 500 ms
drain or waits synchronously for a channel-halt IRQ. Missing acknowledgements
retain the exact controller DMA, claim, endpoint and dependency across retries.
It is a software prerequisite, **not a delivered native Serial Monitor or MSP
flasher**, a full native callback deadline proof, a target build, or hardware
qualification.

The full focused runner passes normally and with ASan/UBSan (LeakSanitizer off).
The separate serial reuse/consumer runner also passes 20 invocations per mode
(40 total). The focused suite includes three rejected ownership/deadline mutations in each mode, original
baseline counterexamples, and the unchanged MSP consumer's detach limitation.
See `evidence/usb-checked-cleanup-20261021/` for exact logs and source locks.

## Exact reconciled cohort

- Drivers production base: `a7ae39994603c4545a3c014f09051ecfc12084d2`.
- Preserved qualification parent: `0dd55680bf565edd8bcb30405d14d2f9b02cd956`.
- Native .123 Reader: `c98e4222048244b1e729869ed47aa99130025fd3`.
- Historical Reader: `34d8e694d89a1e72d8854403d8592c289fae3ddc`. Its selected
  host source and two USB SDK headers are byte-identical to native c98e422;
  that equality was checked, not assumed from the historical product pin.
- Native Runtime: `6a3b3d46f5b2857eacc6e9863799dec1f466b036`.
- Native SDK: ESP-IDF `fff9895c82d744c7237be8847347bdd1b07c6643`, esp-usb
  `0f6820940f9e8075c0cc1483d9c2f5e89cc5b97a` (USB 1.6.0), GCC
  `esp-15.2.0_20251204`; source pins/configuration remain unchanged.
- Composed MSP negative evidence: `4db1ca4f189ff5c227b9e8f2a14069cec149fe80`;
  only its two test files are reused. MSP/program-MSP production is unchanged.

Native Reader's controller 0.1.20 contributes `ClaimReleasePolicy.h` and
close-stage custody semantics. The controller was not replaced wholesale:
Drivers' existing native IRQ, PHY, DMA, host/HCD/hub lifecycle and role ownership
adaptations remain. No Runtime, System, Utilities or product files changed.

## Version and ABI compatibility

Local source versions are `usb-host-v2` **0.1.6-checked-cleanup.1** and
`usb-controller-esp32s3` **0.1.26-checked-cleanup.1**. The latter is distinct from
both historical controller 0.1.19/0.1.20 and the existing native builder's 0.1.25
candidate identity. These versions are not published or selected by a product.

The original host, discovery, interrupt and diagnostic member offsets remain
unchanged. Checked release, claim-scoped control and snapshot reuse native
Reader's already-existing append-only structs. Old void-release clients remain
valid, verified by independently compiling a consumer against Drivers baseline
headers and loading the new actual host. The old device-token control behavior
is deliberately retained; this is not a claim that old clients gain the stricter
claim-scoped authorization of the new suffix. New clients must use
`control_claim` for claim-scoped commands. Provider APIs remain opaque to Runtime.

`release_checked(false)` revokes use but retains claim custody. If graph
quiescence subsequently finishes physical cleanup, a terminal receipt remains
until the checked caller retries. Quiesce and stop cannot consume that receipt;
legacy void-release cleanup does not acquire checked-caller custody.

Candidate builders are separate:

- `scripts/build_usb_host_checked_cleanup.py` writes only
  `dist/usb-host-v2-checked-cleanup` and has no canonical-byte claim.
- `scripts/build_usb_controller_checked_cleanup_idf61.py` requires explicit
  native compile database, exact SDK/esp-usb checkouts and output directory. It
  adapts `usbh.c` as well as HCD/host, records the new adapter and SDK-header
  hashes, and keeps the original import/loader/ABI checks.

Historical replay builders, dist/release bytes, source/release inventories,
boards and product pins are unchanged. Ordinary historical parity should not
be claimed for this separately versioned local source candidate.

## Native implementation

1. Controller transfer waits service only the provider's finite client
   completion batch, plus EP0 completion retirement when its transfer is still
   in flight. They do not run daemon discovery/reset/enumeration work. Bulk/control
   accounting starts at operation entry, not after submission; the regression
   injects 2 fake-ms submission cost into a 3 fake-ms total budget. A separate
   check consumes the budget in endpoint setup and confirms no submission;
   wraparound accounting is exercised across the 32-bit tick boundary.
2. Halt polling has persistent HCD state. A missing IRQ returns
   `ESP_ERR_NOT_FINISHED` without a task-notification wait. HALT/ERROR IRQs
   acknowledge that state without requiring a sleeping caller; existing public
   synchronous HALT behavior remains available to legacy SDK paths.
3. Flush/clear/free/new submission cannot bypass pending cancellation. Port
   invalidation follows the pinned HAL's halted-channel path and prevents a
   late IRQ from parsing a retired buffer. EP0 cancellation rejects
   enumeration-owned devices. Endpoint lookup uses zero-timeout try-locks.
4. Controller bulk/control and interrupt cleanup separately retain halt,
   flush and callback completion. A naturally completed callback does not
   imply that a concurrent halt request has been acknowledged. Failed flush,
   callback pump, endpoint clear, transfer free, interface release or device
   close preserves unfinished state and forbids slot/buffer reuse.
5. Native private interface release does not sleep 10 ticks for pending client
   events. Its try-lock failure returns pending. Device close retains opened
   bookkeeping when the lower close fails. Successful interface release is
   never repeated after a later failed device close.
6. Failed open/descriptor processing retains every returned device handle in
   an unpublished slot until verified close; generation exhaustion acquires
   no new handle. EP0 completion processing never forwards an idle transfer's
   stale handle after device closure.

All task-side USB calls remain on the existing single serialized provider
executor. No extra USB worker, firmware USB bridge, persistent access, power
policy or X4 wiring assumption was introduced. Existing internal SDK mutex
usage in claim/free internals relies on that executor invariant; this candidate
is not a general concurrent USB stack or a hard real-time scheduler guarantee. The candidate builder asserts 1000Hz
32-bit ticks. The exact pinned SDK conversion macro was reviewed: budgets up to
1000ms do not overflow, but arbitrary long raw uint32 timeouts can wrap to a
shorter budget and are not qualified here. Runtime
`sdk/driver/RiscStreamSessionProviderV1.h` requires a total budget <=1000ms; this
candidate does not yet supply the native class/session adapter.

## Qualification and reproducibility

Run with the exact pinned esp-usb checkout; the runner never edits that checkout:

```sh
bash test/run_usb_checked_cleanup_test.sh /path/to/esp-usb
SANITIZE=1 ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 \
  UBSAN_OPTIONS=halt_on_error=1 \
  bash test/run_usb_checked_cleanup_test.sh /path/to/esp-usb
```

Coverage includes actual host source; actual extracted controller
transfer/release/discovery bodies; the actual `PendingInterrupt.h`; actual
transformed HCD halt/flush/clear/ISR/free/submit gates and buffer/URB handling;
actual USBH endpoint lookup and EP0 dequeue; and actual transformed private host
release/close bodies. OS, HAL, hardware registers and USB traffic are fake.
Delayed/missing IRQ, natural completion versus cancellation, error precedence,
port invalidation/late IRQ, locks, EP0, RX/TX, descriptor/open failure, detach,
failed physical close, retry receipts, old-header consumers and existing native
lifecycle adapters are covered. The 1 ms transfer counterexample is now a
1 fake-ms retained timeout, not 501 fake-ms. Fifty successive cleanup attempts
with no halt acknowledgement add no fake-clock delay and never release custody.

Three mutations are rejected: adding a 500 ms timeout overrun, skipping halt
acknowledgement, and forgetting a claim on failed close. The original baseline
probe now explicitly reads the unchanged a7ae399 controller via `git show`, so
it remains a reproducible negative reference rather than testing new code under
an obsolete assertion. The first runner draft omitted one policy-header include;
that failed log is retained, the include was fixed, and final runs are separate.

Strongest compilation evidence is host C/C++ compilation of those real source
bodies with strict warnings and normal/sanitized execution. It is **not** full
SDK translation-unit preprocessing/type checking or Xtensa target compilation.
No Xtensa compiler was available on the inspected PATH; the full pinned IDF
checkout/native compile database are not present here. No target ELF, full CI,
leak-check, native stream-session integration, or physical timing pass is claimed.

## Remaining work

- Add a native CDC stream-session adapter using Runtime's existing total-budget
  ABI, preserving queues until checked close, and recover/implement its matching
  portable client. Propagate the remaining total budget across all class calls.
- Keep discovery/role changes outside those callbacks or explicitly establish a
  separate safe integration. Ordinary daemon paths still include debounce,
  reset, enumeration and `portMAX_DELAY` waits; this patch does not claim to
  bound them or controller-wide quiescence. The legacy non-IDF-6.1 fallback
  intentionally retains historical synchronous drain behavior.
- Qualify full native target compilation, exact packaging/import/loader closure
  and target behavior before any native candidate can be selected.
- Adapt MSP consumers to use checked closure. The unchanged composed MSP fixture
  still reports old-close=false after both successful and failed physical
  release; lower-layer repair alone does not repair that consumer contract.
- X4 VBUS/role-root proof remains a separate hardware admission blocker.

No push, PR, merge, tag, release, catalog edit, accepted-byte overwrite,
product pin change, hardware operation, flashing, serial/device access or radio
action occurred.

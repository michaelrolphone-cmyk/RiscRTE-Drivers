# Native CDC stream-session and portable-client candidate

## Outcome and boundary

A native CDC provider adapter and matching portable client now compose through
Runtime's existing invocation-bound stream broker, provider graph and queues.
The candidate has been qualified with host-executed real source, not physical
USB hardware. This is **not a delivered Serial Monitor UI or selected X4 product**.
No firmware/product pin, accepted byte, catalog, release or remote branch changed.

The source checkpoint is Drivers `d4c9fe436c5bd6b73e1feac87ac0df7e04ba0bf3`,
a descendant of the preserved checked-cleanup checkpoint
`b4334b76124f80d1252eb7dda9e903fd1b7b4d39`. A subsequent separate commit contains
only candidate overlays/builders, composed fixtures and evidence. System client
commit is `f9d55dbeb941ba40fb965f53172ce3e1b85af376`, exactly one commit above
`0651a55513b0aa0dff1b6a9072303def08a97378`. Its independent qualification and
restorable artifacts accompany this work under `usb-native-cdc/client-artifacts`.

## Exact composition

- Runtime: `6a3b3d46f5b2857eacc6e9863799dec1f466b036` (.123 input).
- Native Reader: `c98e4222048244b1e729869ed47aa99130025fd3`. Only the existing
  generic serial declaration and exact CDC parser were reused. Historical
  Reader `34d8e694` was not substituted for the native composition.
- Drivers: preserved checked host/controller source plus a new, separate
  `Drivers/usb_cdc_native` source and one reviewed host partial-claim correction.
- System: the new stream-only `PortableSerialClient` uses the existing Runtime
  stream-client and serial-request declarations, not a replacement ABI.
- Lower native SDK remains ESP-IDF `fff9895c82d744c7237be8847347bdd1b07c6643`,
  esp-usb `0f6820940f9e8075c0cc1483d9c2f5e89cc5b97a`, GCC distribution
  `esp-15.2.0_20251204`. No SDK policy or source pin changed.

The exact Runtime provider/stream headers were copied verbatim; the existing
base/diagnostic prefix offsets remain unchanged. The native Runtime already
registers trusted `platform.clock@1`, whose declaration is byte-identical to the
Drivers declaration. The candidate adds that existing dependency. No privileged
clock import, new ABI, generic framework or firmware USB bridge was introduced.

## Ownership and deadline behavior

- The adapter exports the complete existing poll suffix followed by the exact
  tagged native session suffix. Its raw legacy prefix remains usable, but raw
  operations cannot adopt or bypass a native-owned session.
- Open validates the exact device and generation, then claims, configures and
  publishes a distinct RX/TX pair. Partial open records every returned claim
  and queue. Clean failure zeros output; uncertain cleanup returns RETAINED
  with exact surviving custody.
- Close stops the pump before fallible work, finishes queue use, obtains both
  checked physical-claim receipts, and only then destroys queues. Failed finish,
  physical close or partial queue close retains exact remaining state. Native
  retained sessions are never cleaned by stop/quiesce behind Runtime's back.
- Every native open/call/close uses one positive total budget of 1..1000 ms.
  All lower operations have pre-call checkpoints; control and bulk calls get
  only remaining time, capped to 1 ms per bulk pump operation. No fresh cleanup
  budget is appended. Exhaustion performs no further host/queue callback.
  Claim/configuration/list/checked-release/queue methods have no budget argument;
  they are finite/nonblocking in the pinned lower implementation and their
  elapsed work is charged before any subsequent operation. This is not a
  measured worst-case target deadline or scheduler-preemption guarantee.
- Inventory reads only the host's already-published device list and cached
  descriptors. No bounded callback invokes daemon discovery, reset, enumeration,
  role transitions or power policy. Unknown/malformed inventory pauses physical
  RX/TX without dropping staging/accepted queue bytes; a successful same-token
  check resumes. Definite detach stops use and requires explicit close/reopen.
- The portable client preserves explicit inventory-index and provider-instance
  selection. Instance zero means Runtime's existing uniqueness rule, not a
  first-match or automatic hardware selector. RETAINED requires prompt return
  with no cleanup, polling, yield, frees or retries.
- Pinned Runtime may spend an open sub-budget again on internal rollback. The
  client reserves half of its total budget for that path. Consequently 1 ms
  open fails locally before I/O; existing-session controls still support 1 ms.
  Capability acquisition/release and inventory callbacks are not claimed as a
  preemptive end-to-end wall-clock bound.

## Review findings repaired and tested

1. The prerequisite host discarded a nonzero physical claim when a controller
   returned false. An actual-host/fake-controller reproduction returned clean
   native open failure while leaving a data claim live. The host now reserves
   identity before acquisition and retains the exact returned physical claim
   in a closing slot, exposing the token for checked cleanup. Legacy callers
   ignoring failed output leave a host-owned orphan for quiescence. The exact
   pinned controller currently writes output only on success; this is generic
   ABI hardening, not a demonstrated physical failure of that controller.
2. After unknown inventory the initial adapter still pumped physical data.
   A pause latch now preserves data until a successful exact-device check.
3. Client review repaired unexpected positive control-status handling, clean
   capability-acquisition refusal validation, and hidden Runtime rollback-budget
   accounting. Its normal/sanitized regression tests cover all three.

Independent review replayed the provider fixes and found no further confirmed
issue in the reviewed source scope. Hardware/PHY/DMA timing was not reviewed by
executing a target.

## Numeric candidate manifests

The prerequisite's `0.1.6-checked-cleanup.1` and `0.1.26-checked-cleanup.1`
labels are **rejected** by the pinned Runtime's numeric-only version parser.
Their manifests, builders and immutable checkpoint are preserved. Separate
local overlays supply host **0.1.7** and controller **0.1.27**; native CDC is
**0.1.1**. These versions are unpublished and are not product-selected.

The current remote Drivers source and released-driver inventory were read-only
checked: host 0.1.3, controller 0.1.19, CDC-v2 0.1.0. Native Reader has host 0.1.5
and controller 0.1.20; the existing native controller builder has 0.1.25 and the
prerequisite label has 0.1.26. The new numeric values were not found among those
checked identities. This is not a global reservation across unobserved work.
See `evidence/usb-native-cdc-20261021/remote-version-check.json` for exact blobs.

Builders write separate outputs: `build_usb_cdc_native.py`,
`build_usb_host_native_stream.py`, and
`build_usb_controller_native_stream_idf61.py`. The controller builder preserves
the pinned compile-database/SDK/import/loader checks and records the overlay hash.
All three identities pass the actual Runtime `makeIdentity`; preserved prerelease
identities are negative controls. A separate shared parser regression at MSP
commit `0bff96535f2d06c51ec134bae2616e7e8b9bd5bc` also invokes the unchanged
pinned Runtime `manifest()`, `makeIdentity` and update-version parser, accepting
all three overlays and rejecting prerelease/build/malformed controls. It uses
`-fno-access-control` solely to invoke the private parser in its host test;
Runtime source and policy remain unchanged. Exact test hashes/logs are included. Runtime integration loads the **exact** host
and CDC candidate manifests. Its controller and registered clock remain explicit
software fixtures. This is not full native controller/ELF/package admission.

## Reproducible qualification

From this repository, with the exact external checkouts:

```sh
bash test/run_usb_cdc_native_test.sh
bash test/run_usb_cdc_runtime_test.sh /path/to/RiscRTE /path/to/System
python3 test/usb_native_candidate_versions_test.py /path/to/RiscRTE
bash test/run_usb_checked_cleanup_test.sh /path/to/esp-usb
```

Repeat the shell runners with `SANITIZE=1`,
`ASAN_OPTIONS=detect_leaks=0:abort_on_error=1` and `UBSAN_OPTIONS=halt_on_error=1`.
LeakSanitizer is unsupported under this executor's ptrace and no leak pass is
claimed. Final logs and source locks are committed in
`evidence/usb-native-cdc-20261021/`.

- 31 native adapter/actual-host/old-header scenarios pass in each mode.
- Five deliberately mutated implementations plus the exact prerequisite-host
  partial-claim counterexample are rejected in each mode.
- 10 actual Runtime/CDC/checked-host/portable-client compositions pass per mode:
  normal, unknown inventory, detach, retained close and retained partial open,
  each under eager and demand activation. The source Runtime broker, queues,
  graph, loader and client are exercised; OS/controller/USB traffic are fake.
- The unchanged 22-case Runtime serial witness/client suite passes per mode in
  the independent System qualification. It is additional client evidence, not
  another production USB implementation.
- Full checked-cleanup focused regression passes normally and with ASan/UBSan.
  Existing historical CDC source and accepted/replay builders remain unchanged.

The first drafts corrected fixture expectations (clean helper stopped a provider
before reopen; one RX byte remained staged; host quiesce visits open control
before closing data), duplicate header search paths, and trusted platform-clock
registration. Those were fixture corrections, not passing evidence. The two
confirmed production review findings remain executable negative controls.

## Remaining deliverables

1. Integrate an actual native Serial Monitor app/UI with explicit endpoint and
   instance selection. The old Reader Serial Monitor 1.2.7 is not current delivery.
2. Establish a safe, separately scheduled host discovery/role integration outside
   stream callbacks. The composed fixture's explicit host poll is test-only and
   must not be mistaken for product policy or automatic real-device selection.
3. Build exact Xtensa packages and validate native controller imports, ELF loader
   closure, full SDK translation units and measured callback behavior. Both
   CDC/host target builders currently stop at missing Xtensa compiler; the full
   pinned IDF checkout and native compile database are also unavailable here.
4. Resolve X4 VBUS/role-root electrical and ownership proof before device
   admission. No hardware, USB-device, serial-port, flashing or radio action,
   new user testing request, push, PR, merge, tag, release or catalog change ran.

Thus source progress is real, but the developer-tool monitoring outcome remains
in progress, not completed or ready for a new user hardware test request.

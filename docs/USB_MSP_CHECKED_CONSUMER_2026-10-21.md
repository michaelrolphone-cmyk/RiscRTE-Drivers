# MSP checked-cleanup consumer candidate

**Packaging correction:** this report records checkpoint ed7886d. Its prerelease
version strings were later found inadmissible to pinned Runtime. See
[the numeric-candidate correction](USB_MSP_NUMERIC_CANDIDATES_2026-10-21.md)
for the descendant manifests and actual Runtime parser regression. The
ownership source/tests and original checkpoint artifacts remain preserved.

## Result and exact composition

The local candidate closes the demonstrated MSP custody gap using the already
reviewed host's checked-release and claim-scoped-control suffix. It does not
modify the SDK, host, controller, native product selection or board policy.
It is **not a delivered native flasher UI, target build, physical programming
result, hardware acceptance, or full hotplug/programming recovery claim**.

- Repository: RiscRTE-Drivers; branch `tasker/usb-msp-checked-consumer-20261021`.
- Reviewed prerequisite/base: `b4334b76124f80d1252eb7dda9e903fd1b7b4d39`.
- Original programmer safety source and regression reused from
  `1f86a08db75a98247bab47f0f1800c64a4e2075f`. That checkpoint remains unchanged.
- Negative detach fixture preserved from
  `4db1ca4f189ff5c227b9e8f2a14069cec149fe80`. Its original host/MSP source blobs
  equal the immutable `a7ae39994603c4545a3c014f09051ecfc12084d2` baseline.
- Exact underlying native Reader/Runtime/ESP-IDF/esp-usb cohort is recorded in
  `USB_CHECKED_CLEANUP_2026-10-21.md` and its source lock. Every inherited
  `candidate_files` hash still matches in this checkout.
- Local versions: `usb-msp` and `program-msp` both
  **0.1.2-checked-consumer.1**, status `experimental-local-checked-consumer`.
  Neither version is published or selected by a product.

Historical Reader migration flashing evidence remains distinct from native
X4 0.1.123. This source composition does not establish a selected native UI,
complete package/import closure, USB root role, or X4 VBUS electrical contract.

## Ownership and close phases

1. MSP accepts the original discovery prefix. Each optional checked-release or
   claim-control member is inspected only when its complete member lies inside
   the host's advertised `struct_size` and its pointer is non-null.
2. Claims are stored in their session slot immediately, before subsequent
   acquisition/configuration/protocol work. Rejected opens retain unpublished
   custody until checked physical closure succeeds. Retained slots are not
   reallocated. The existing shared control/data-claim path releases only once.
3. Detach revokes MSP operations but preserves the exact public token, interface
   claims, RX buffer and dependency. Missing devices are not release evidence.
   Each claim is cleared only after its own checked release returns true.
4. A public session remains a caller-owned terminal receipt after physical
   cleanup. Poll, quiesce and stop can progress cleanup, but only `close(token)`
   consumes that receipt. Repeated close after success rejects the stale token.
   Unpublished failed-open slots have no caller receipt and can be reclaimed.
5. `program-msp` retains the original failed-open quarantine repair. After its
   target sync/reset-and-release phase succeeds, a narrow `closing` latch
   prevents repeating those target actions. Subsequent attempts retry transport
   close only. Write/verify are revoked while that close is pending.

No new public ABI or tombstone API was invented. All execution remains under
existing serialized provider assumptions. MSP's existing protocol timeouts and
work bounds are unchanged; this work does not establish a new total callback
budget or physical timing guarantee.

## Important conservative limits

**Legacy compatibility is ABI-safe, not full close-functional compatibility.**
For an old prefix, truncated checked-release member, or null checked-release
pointer, MSP requests the void release once per exact claim, then retains the
session and dependency indefinitely. Void release cannot prove physical
completion. Neither quiescence nor absence is used to turn that uncertainty into
success. Those intentional process-end fixture cases do not explicitly unload
providers or free their dependency tables while custody remains.

**Detach before target-close completion remains fail-closed.** If a published
programmer session loses its transport before its target sync/release phase
succeeds, programmer close still fails and retains its session/dependency. The
new latch does not infer reset, target-running, or programming success from
transport disappearance. Detach after that phase, and failed-open quarantine
without a published target session, can drain through the checked-close path.
A different policy for a previously active target requires separate work.

## Reproduce

From the repository root, on a host with a C compiler and Python:

```sh
bash test/run_usb_msp_checked_consumer_test.sh
bash test/run_program_msp_test.sh
python3 test/usb_msp_checked_mutation_test.py
bash test/run_usb_msp_detach_contract_test.sh
python3 -m unittest discover -s test -p test_msp_checked_candidate_routes.py -v
```

For sanitizer runs, prefix each C/fixture command with:

```sh
SANITIZE=1 ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 \
  UBSAN_OPTIONS=halt_on_error=1
```

The composed runner invokes **eight processes per mode**: two real three-provider
composition scenarios and six host-table variants (prefix, truncated release,
null release, release-only, truncated control, full). The three providers are
compiled from actual production source and dynamically loaded separately. Only
the controller and TI probe are fake. Coverage includes successful/failed
release, partial claim cleanup, no double physical release, detach, delayed
cleanup, repeated close, intervening host/MSP quiesce and stop, stale tokens,
restart, four retained session slots, exhaustion/reuse, protocol/configuration
failed opens, exact RX storage retention, dependency pinning, programmer
failed-open quarantine and non-repeated target-close phases.

Normal and ASan/UBSan passes also include the programmer unit regression and the
two historical detach observations. LeakSanitizer is disabled because the
execution environment uses ptrace; no leak-check pass is claimed.

Four negative variants are rejected at meaningful assertions in both modes:
original MSP lost custody, swallowed terminal receipt, ignored failed physical
release, and repeated programmer target-close phase. The historical diagnostic
runner explicitly compiles immutable baseline host/MSP/header sources rather
than applying obsolete expected-failure assertions to the repaired consumer.
Its two `OBSERVED` lines describe the old defect, not current acceptance.

The inherited full checked-cleanup runner passes normally and sanitized against
esp-usb `0f6820940f9e8075c0cc1483d9c2f5e89cc5b97a`:

```sh
bash test/run_usb_checked_cleanup_test.sh /path/to/pinned/esp-usb
```

This rechecks actual host and extracted/transformed controller/SDK source paths
with fake OS/HAL/USB behavior. It remains source-level simulation, not a complete
SDK translation-unit or Xtensa compilation. Logs and source hashes are in
`docs/evidence/usb-msp-checked-consumer-20261021/`.

## Build, replay and publication separation

The new scripts `build_usb_msp_checked_consumer.py` and
`build_program_msp_checked_consumer.py` write only distinct
`dist/usb-msp-checked-consumer` and `dist/program-msp-checked-consumer` paths.
They validate separate candidate manifests and retain import/export/ELF checks,
without claiming canonical byte parity. Both target-build attempts stopped
before compilation because the Xtensa compiler is unavailable.

Original builders, canonical digests, accepted dist/release bytes and source
inventories remain unchanged. Targeted workflow steps build the new identities;
historical MSP parity uses a separate immutable a7ae399 checkout and its
unchanged builders. Three route/identity tests and YAML parsing pass locally.
No target replay or remote CI was executed.

An inherited **whole-workflow gate remains**: `driver-parity.yml` still invokes
`build_usb_host_v2.py`, whose release manifest guard expects host 0.1.3 rather
than the prerequisite's local 0.1.6-checked-cleanup.1. The latter has its separate
`build_usb_host_checked_cleanup.py` producer. This MSP change does not rewrite
that host gate or claim the entire CI workflow is green. Full source parity is
also not claimed for these intentionally unpublished source candidates.

No push, PR, merge, tag, release, catalog edit, accepted-byte overwrite, product
pin change, hardware test request, device access, flashing or radio action was
performed.

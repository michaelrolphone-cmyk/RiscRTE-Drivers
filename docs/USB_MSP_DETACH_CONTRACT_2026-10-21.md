# MSP detached-session cleanup contract, 2026-10-21

## Outcome and stopping boundary

The existing selected host ABI does not let `usb_msp` prove that a detached
session's claims were released. No narrow production repair satisfying the
existing close contract was established. This follow-up therefore stops with a
composed regression/diagnostic, not a new lifecycle policy or a success shortcut.

The child branch `tasker/usb-msp-detach-contract-20261021` is based on clean local
checkpoint `1f86a08db75a98247bab47f0f1800c64a4e2075f`. It changes only test and
evidence files. Production source, public SDK, manifests, versioned artifacts,
product pins, and the earlier safety repair remain unchanged. No device or radio
was accessed and nothing was published.

## Selected source identity

Drivers base source is `a7ae39994603c4545a3c014f09051ecfc12084d2`; the following
blobs remain unchanged through the parent safety checkpoint:

- `Drivers/usb_msp/driver.c`: `a64cf0e2748e83897be96fcaabf2559e27c36463`.
- `Drivers/usb_host_v2/driver.c`: `5cdddf061b454d514f237ffcdc9a77341b6e238c`.
- `sdk/driver/RiscUsbProviderV1.h`: `7e6a7512c332fd2014ecf4b44fa0c56de18a1e07`.
- `sdk/driver/RiscUsbControllerV1.h`: `348b5b478f23f5893ff33dffcb4359f694712fd0`.

The programmer's earlier 0.1.1 correction remains separate: it safely retains
failed-open transport ownership, but cannot force the lower transport to
acknowledge a token that the lower transport has erased.

## Ownership trace

1. `usb_msp.open_probe` claims control/data interfaces and stores their
   generation-qualified host claim tokens. Open failure invokes void host
   `release` and discards local state. Successful acquisition returns the MSP
   session token.
2. `usb_msp.poll_probes` notices a detached device, invokes void host `release`
   for both claims, then zeroes the MSP session record.
3. `usb_msp.close_probe` requires the session record. Once erased, old close
   returns false forever; it cannot distinguish a retired valid token from an
   unknown token.
4. `usb_host_v2.release_claim` marks a claim closing. The controller's release
   returns a boolean: false explicitly means resources might remain live. Host
   retains that claim on false, but its public release function returns void.
5. Repeating public host release for a closing claim does not retry it. Host
   lifecycle `quiesce` can retry the physical release, but that lifecycle table
   is not part of the MSP dependency's `usb.host` capability API. Discovery
   `poll`/`devices` expose presence, not per-claim release completion.
6. The MSP close contract says failure retains the session and success releases
   claims. Neither device absence, local MSP quiescence, elapsed time nor a void
   release call proves those claims were released.

Returning success for erased tokens would hide unknown or still-live ownership.
Retaining detached tokens alone would fail closed indefinitely because their
protocol stop/reset calls cannot succeed after detach and release completion
remains unobservable. This diagnostic does not assume host quarantine itself
means successful release or permit direct access to another provider's state.

## Composed production fixture

`test/drivers/usb_msp_detach_contract_test.c` loads both real `usb_host_v2` and
`usb_msp` provider implementations as separate host shared objects. Only the
controller and TI probe framing are simulated; the latter reuses the existing
MSP fixture. No callbacks reach hardware.

Two independent processes exercise:

- Successful physical release on detach: the host has no outstanding claims,
  MSP is quiescent, but old MSP close is false.
- Failed physical release on detach: both physical claims remain live in host
  quarantine; MSP is nevertheless quiescent and old MSP close is identically
  false. Host quiescence is false until the fake controller later accepts
  release. After real host cleanup completes, old MSP close still returns false.

The fixture also checks that stale execution/interface calls do not perform new
protocol I/O. This demonstrates two different physical-ownership outcomes with
identical MSP-visible observations, not a repaired detach flow.

Both cases passed normally and with AddressSanitizer/UndefinedBehaviorSanitizer.
The prior programmer ownership regression also passed normally and sanitized.
LeakSanitizer remains disabled because ptrace prevents it in this environment.
No target ELF compilation, remote CI, real USB timing, physical detach, DMA,
claim release, target programming, or hardware success is claimed.

Run:

```sh
bash test/run_usb_msp_detach_contract_test.sh
SANITIZE=1 ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 \
  UBSAN_OPTIONS=halt_on_error=1 bash test/run_usb_msp_detach_contract_test.sh
```

Logs are under `docs/evidence/usb-msp-detach-contract-2026-10-21/`.

## Existing newer source does not authorize a blind swap

Historical Reader `34d8e694d89a1e72d8854403d8592c289fae3ddc` has host 0.1.5
with existing append-only `release_checked` and `control_claim` slots. Its
checked release preserves/retries the same closing claim token and returns the
physical release result. This is a candidate dependency contract to reconcile,
not a newly invented ABI.

It is not selected by the current Drivers host 0.1.3/SDK or native X4 product
composition. Moreover, the newer host restricts its legacy device-token control
entry to HID requests; current MSP CDC line-coding/control-state requests use
that legacy entry. Copying the newer host alone would therefore not establish a
coherent MSP chain. The MSP source itself is unchanged in inspected Reader
`9c8783d6f20659a514bfaeb3847a77c7213dc4f6` and the earlier pinned snapshots.

Next prerequisite: separately reconcile the existing checked-release and
claim-scoped-control host/SDK/provider composition, exact claim retirement and
caller cleanup semantics, and its bounded controller behavior. Only then can a
provider-side detached cleanup change be qualified without fabricating release
success. This branch makes no such SDK, ABI, product-policy or pin change.

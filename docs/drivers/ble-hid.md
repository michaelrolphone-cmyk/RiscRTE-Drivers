# ble-hid 0.1.2

`ble-hid` is an original ABI-v2 logical provider for `bluetooth.hid@1`.
It is board-neutral: no Watch, e-paper panel, GPIO, SDK controller or radio
register is named by the implementation. It requires the append-only exclusive
host lease in `bluetooth.hci@1`, `platform.clock@1` and an explicit
`storage.key-value.bound@1` policy. Watch currently supplies the lease through
`twatch-ble` 0.3.0. Other RiscRTE products can use the same ELF when they provide
that exact contract and the deployment grants. Their firmware/images are not
modified or qualified by this change.

## Architecture and provenance

GAP, GATT, ATT, L2CAP, SMP, HCI ACL credits, ECDH and AES-CMAC are Apache NimBLE
1.9.0, tag `nimble_1_9_0_tag`, commit
`da7e3256da3ba80b232df880f40d8359311cc62e`:
https://github.com/apache/mynewt-nimble/tree/da7e3256da3ba80b232df880f40d8359311cc62e

The selected upstream sources and licenses are vendored with per-file upstream
SHA-256 values in `vendor/nimble/SOURCE.json`. One upstream patch adds
`uint32_t` promotions before two AES 24-bit shifts in TinyCrypt, removing signed
integer undefined behavior found by UBSan. Its patched hash and reason are
recorded separately; cryptographic algorithms are otherwise unchanged. A second
pinned patch propagates bounded transport errors through ATT instead of
asserting. Its original hash, patched hash and reason are recorded too. Logs are
compiled to no-op functions: keys, peer identity material and reports are never
printed. The build verifies every recorded source hash before compiling.

The original code consists of the HID service/report API (`driver.c`), a scoped
bond store (`hid_store.c`), and a cooperative NimBLE portability/transport layer
(`port/`). No host code enters Runtime. No global HCI hooks, external tasks,
interrupt handlers, timers or application callback pointers are registered.
NimBLE callbacks point only to this provider and run during its serialized API.

The implementation follows the Bluetooth HID service / HOGP model and Core
Secure Connections procedures:
- https://www.bluetooth.com/specifications/specs/hid-over-gatt-profile-1-0/
- https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Core-54/out/en/host/security-manager-specification.html

This experimental package makes no Bluetooth qualification or host-OS
interoperability claim. The Device Information PnP value deliberately uses an
unassigned development VID/PID, not another vendor's identity; a shipping
qualified product must supply its legitimate identity and qualify its profile.

## Binding and deployment

This is a logical Global0 provider. Do not add a hardware device to obtain a
provider ID. Its boot driver selection has no `instance_id`:

```json
{
  "manifest": "ble-hid/manifest.json",
  "key_value": [
    {"key":"hid_ours", "namespace":10, "access":"read-write"},
    {"key":"hid_peer", "namespace":10, "access":"read-write"},
    {"key":"hid_ccc", "namespace":10, "access":"read-write"},
    {"key":"hid_identity", "namespace":10, "access":"read-write"}
  ]
}
```

Namespace 10 is the later Watch cohort's private reservation, not a number in the
generic driver. A different product chooses its own namespace. Runtime resolves
the unique selected `bluetooth.hci` provider. An ambiguous graph fails admission.
Applications receive `bluetooth.hid@1` at instance 0. They do not receive the bond
namespace. The existing controller lease excludes scanning/other host owners;
Runtime's native controller/IQ resource exclusion still applies unchanged.

Driver activation is read-only and never opens the controller or generates a
key. Only explicit `open` claims the HCI lease. Product radio preferences and
Airplane mode remain application/deployment policy, using existing controls.

## Service and report contract

The HID service exposes HID Information (1.11), Report Map, keyboard input/output
with Report ID 1, five-button relative mouse with Report ID 2, Report Reference
descriptors, HID Control Point, Protocol Mode, Boot Keyboard Input/Output and
Boot Mouse Input. Report mode keyboard values are 8 bytes: modifiers, reserved,
six key usages. Mouse values are 4 bytes: buttons, signed X, signed Y, signed
wheel. Boot mouse omits the wheel byte; nonzero wheel and buttons beyond its three-button mask are rejected rather than silently lost. Report IDs are in Report Reference and
Report Map, not repeated in characteristic values. LED output is validated and
stored without inventing physical LEDs. GAP, GATT and Device Information
services are present. Battery Service reads return an ATT error while the level
is unknown; `battery(...,255)` means unknown, never fabricated 100%.

`RiscBluetoothHidV1.h` is the complete copied-value client contract:
- `open` copies a printable 1..20-byte device name and returns a generation token.
- `poll` services 1..16 receive/event work items. Apps should poll every 20 ms.
- `status` reports OFF, STARTING, ADVERTISING, CONNECTED, PAIR_CONFIRM, READY or
  FAULT, with separate keyboard/mouse-ready, encrypted/authenticated/bonded flags.
- Keyboard/mouse calls reject before the relevant notification subscription,
  authentication and encryption. Duplicated keyboard keys, error usages,
  modifier usages in the key array, invalid button masks and -128 axes reject.
- Each accepted report is an explicit NimBLE notification, preserving FIFO
  press/release order; it is not a coalesced current-state update.
- `release_all` sends neutral reports for held inputs. It clears each local
  modifier/button state only after a neutral is accepted or disconnect/native
  closure is confirmed; rejected reports retain state for retry. Automatic
  release failure enters a terminal fault and requests connection termination
  independently of ACL credits. Later queued GAP events cannot revive it. Mouse motion is never retained for reads.
- Boot/report protocol transitions, suspend, subscription changes and disconnect
  neutralize held state. Independent cooperative 1,000 ms keyboard and mouse watchdogs also release
  held state; they cannot execute if the caller stops polling entirely.
- `close` attempts neutral reports and graceful host stop, then releases/closes
  the native lease. Disconnect releases the host's device even if a final
  neutral notification cannot be delivered. No delivery acknowledgment is
  fabricated. A safe prior-power restore failure is visible in OFF status.error.
- A failed open may leave a nonzero cleanup token. A failed close retains all
  tokens, provider code and dependencies; retry it before releasing the grant.
  Failed or uncertain host state that cannot be restarted cleanly is poisoned
  for that loaded lifetime. Quiescence cannot free an unreleased native lease.

## Pairing and persistence

One bonded peer is supported. New pairing is permitted only by explicit
`open(..., allow_pairing=true)` while no existing bond is present. A known peer
reconnects using stored keys. New/repeated pairing never silently replaces a
bond. Explicit OFF-only `forget_bond` writes versioned empty records; it is not
storage formatting or enumeration.

The profile requires authenticated 128-bit LE Secure Connections with numeric
comparison. The user compares the six-digit value on both devices and confirms
explicitly through `confirm_pairing`. A 30-second confirmation timeout or
rejection fails pairing, disconnects and disarms new pairing until an explicit
restart. The confirmation entry point checks the deadline itself, including
clock wrap; a caller that missed polling cannot accept an expired comparison. Legacy pairing and unauthenticated Just Works are not accepted. Hosts
without a compatible Secure Connections numeric-comparison association cannot
pair with this profile; the app must report failure rather than a fake success.

Four exact 64-byte little-endian CRC-checked KV records hold local/peer security
material, up to six CCC entries and the local identity key. The store excludes
signed-write CSRKs and supports a single peer. Runtime's bound store commits and
reads back each record. Unreadable, corrupt or uncertain writes fail closed;
there is no rollback claim. Explicit Forget can recover corrupt records only
when every tombstone commit succeeds. No guarantee survives erased storage or a
full destructive reflash. Cryptographic random bytes come from NimBLE's HCI LE
Rand path; deterministic entropy exists only in the host test fixture.

## Bounds and lifecycle

The port uses a 24 KiB provider-owned static allocator arena, fixed upstream pools, at
most 16 callouts, 64 entries per event queue, a 1,028-byte receive scratch buffer
and bounded HCI command waits. Memory allocation never falls back to unbounded
malloc. All provider calls serialize through one guard. HCI command waits pump
only raw receive packets; they cannot recursively dispatch host events.
Malformed transport packets, pool/queue exhaustion and failed native operations
fail closed. A provider assertion retains the invocation and attempts native
lease closure, then yields until reset; it never returns through corrupted
state. Normal close/reopen re-registers the same immutable GATT definitions,
without multiplying allocation limits. Open completes its initial host start
stages under the claimed lease, so immediate close before the caller's first
poll is safe. Refused native claims preserve the previous proven stopped state
and remain retryable, including failed claims carrying a cleanup token. No per-session memory is leaked into
Runtime. Static arena storage lives with the provider ELF mapping.

## Software evidence

Run:

```sh
python -m pip install cryptography==50.0.0
bash test/run_ble_hid_test.sh
SANITIZE=1 bash test/run_ble_hid_test.sh
python scripts/build_ble_hid.py
```

The production host ELF runs against an independent Python/OpenSSL central and
synthetic HCI controller. Tests verify full Secure Connections numeric
comparison, both ECDH/CMAC checks, persisted bond and LTK reconnect, encrypted
service/characteristic/descriptor discovery, long Report Map reads, FIFO HID
reports, modifier/mouse release, boot protocol, CCC changes, watchdog, invalid
reports, stale tokens, duplicate/missing dependencies, rejection/timeout,
legacy downgrade rejection, forged DHKey check rejection, malformed HCI,
retained close retry, corrupt persistence and explicit recovery. Withheld-ACL-credit
cases exercise explicit retry, protocol/CCC/control transitions, watchdogs,
queued encryption refresh, and disconnect cleanup; held state is retained until
a neutral or safe teardown. Claim failures and expired direct confirmation are
also covered. The success
path includes quiescence/restart and twelve additional open/close cycles.
A separate C fixture covers NPL event ordering, timer wrap/cancel, recursive
owner locks, bounded queues and allocator reuse/overflow. Normal and ASan/UBSan
builds run the same protocol paths. The target build verifies provenance,
Xtensa ELF32 shared ABI, only `t5_driver_get` exported, and a bounded C-runtime
import allowlist. CI uploads development evidence, not an installation release.

Physical advertising, PC/phone/macOS/Windows/Linux interoperability, controller
entropy behavior, RF coexistence, latency, current consumption and device
teardown remain untested. The Watch deployment must also pass the real Runtime
store/graph and full-cohort preservation gates before integration readiness.

## Mouse transport failure and reconnect regression (0.1.2)

A rejected native ACL send previously returned literal 1 after consuming its
packet. NimBLE interprets 1 as `BLE_HS_EAGAIN`, which promises a remaining packet;
the production L2CAP path dereferenced a null remainder. Returning the correct
controller error also exposed the upstream ATT unconditional success assertion.
Both error paths now propagate a controller fault to the caller without a crash.

Close now asks NimBLE to finish its bounded software teardown even when the
transport has faulted. Reopen is permitted only after the host stopped, the old
connection is gone, and native release proves controller callback custody ended.
An unproven host stop still poisons the loaded provider; failed native closure
still retains the token and dependencies for retry. No bond is erased or replaced.

The optional size-checked diagnostic suffix copies transport failure stage,
last disconnect/security/notification results, bounded counters, maximum caller
poll gap, and independent host-stop/native-close results. It excludes pairing
codes, keys, addresses and input content. The original API/status prefix is intact.

Regressions establish a real Secure Connections mouse session, send sustained
motion with a held button, inject immediate/queued send or receive failure, close,
reopen and authenticate using the saved LTK, then send mouse input again. They
also cover retained native close, remote disconnect, missing mouse subscription,
stale controller events, and byte-for-byte unchanged security/identity records.
Normal and sanitizer runs use the same production paths. This reproduces a
software defect; the physical Watch's initiating transport failure is not yet
identified. The copied diagnostics are intended to distinguish that trigger.

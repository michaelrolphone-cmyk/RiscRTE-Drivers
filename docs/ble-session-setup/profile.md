# BLE session setup, experimental 0.1.1

`ble-session-setup` provides `bluetooth.session-setup@1` over the ABI-v2 provider boundary. It requires only `bluetooth.hci@1` with the full exclusive-host extension and `platform.clock@1` with the size/tag/version-validated scheduler-only suffix in [RiscPlatformClockWaitV1.h](../../sdk/driver/RiscPlatformClockWaitV1.h). Prefix-only clocks are rejected before any HCI claim. It is a short-lived discovery channel for an already-ready WebDAV endpoint. It has no storage, Wi-Fi, TCP listener, export-grant, or file-read interface.

The driver reuses Apache NimBLE 1.9.0, pinned commit `da7e3256da3ba80b232df880f40d8359311cc62e`, and the existing cooperative NPL/HCI port from Drivers commit `58ad37140bd86bd2247bfa3244c4acc8942b2bd8`. It compiles that port without editing it. HID and telemetry services are not registered in this provider. Exclusive native HCI custody prevents concurrent HID/session-setup/other-host operation. No runtime graph limits or frozen products are changed.

## Owner API and lifecycle

The public C contract is [RiscBluetoothSessionSetupV1.h](../../sdk/driver/RiscBluetoothSessionSetupV1.h).

1. Obtain an authorized, ready WebDAV endpoint first. Sample its remaining duration immediately before `open`; the descriptor contains that `uint32_t` duration, transport, URL, temporary username/password, and session ID. `open` copies every string and retains no application pointers.
2. Choose a distinct BLE setup duration of 1–300000 ms. The provider caps it by WebDAV's copied remaining duration and establishes its own native monotonic deadlines. The app's 32-bit clock and native clock need not share an epoch.
3. Call `poll` at least every 20 ms. One invocation processes at most 16 queued events. Numeric comparison is displayed as six digits, including leading zeros. Confirm only an exact match by supplying the token, pairing generation and displayed number from the same status snapshot. The comparison times out after 30 seconds.
4. Read the copied status for advertising, connection, comparison, readiness, end or fault. Never log credentials or numeric-comparison values. A stale comparison returns INVALID and does not accept the current comparison.
5. Call checked `close` after success, cancellation, disconnect, expiry, or failure. A failed open can return a nonzero token that still requires close. Do not unload or release dependencies until close returns OK.

`OK` and `PENDING` indicate an accepted operation; `BUSY` indicates the serialized API is occupied. `CONTEXT` is terminal for an unknown/stale token and does not touch a newer session. A valid session returning `FAULT` or `EXPIRED` still requires checked close. A rejected valid scheduler-only delay returns terminal `RETAINED`: keep the module and dependencies pinned and make no further calls. Every subsequent API call rejects without host/clock work, and quiesce remains false. Generic BUSY or transport errors never imply owner loss.

Close immediately zeroes the descriptor and starts bounded host shutdown. If native release returns negative, close returns `CLEANUP_PENDING`: preserve the provider, dependencies and token, and retry only close. Each refusal performs one scheduler-only one-millisecond wait, which rounds up to at least one native tick. The initial host-stop loop uses the same safe wait. No NimBLE work resumes after the first native release attempt, even if the preceding host stop was incomplete. Ordinary poll, status and confirmation return `CLEANUP_PENDING` without touching NimBLE or transport. Quiesce may perform that same checked-close retry. A nonnegative native release proves controller custody ended; failed host shutdown can still fence reopening even after safe unload becomes possible. Native release result 0 means safely OFF because restoration failed; 1 means previous controller-enabled state restored.

One connection is permitted per setup session. A disconnect, rejection, protocol fault, transport fault, or expiry destroys the descriptor and never restarts advertising with its credentials. A fresh session needs a fresh explicit open. Closing or expiring BLE does not revoke or close WebDAV: the owning app must independently enforce WebDAV session expiry and any user-authorized cancellation.

## Pairing and confidentiality

Only LE Secure Connections is compiled; legacy pairing is disabled. The provider requires a 16-byte encryption key, authenticated encryption and explicit matching numeric comparison for this connection. ATT permissions require encryption/authentication, and the access callback independently checks the live connection state and local confirmation flag. Completed unauthenticated Secure Connections Just Works cannot read either value.

Bonding and key distribution are disabled. No persistent-storage dependency exists. Reads of saved security material always report absent; any attempt to persist it fails closed. A prior session LTK cannot resume a later session. The peer must pair again for each setup. OS pairing UX and whether the central retains a stale device entry are platform-dependent.

URLs must match the explicit HTTP/HTTPS transport and have a nonempty authority. Embedded userinfo, control bytes, whitespace, backslashes and fragments are rejected before native radio claim. Each credential and session ID is 1–64 printable ASCII bytes. BLE protects setup transport only: HTTP WebDAV still sends HTTP credentials/data without TLS, while HTTPS requires normal certificate validation by the WebDAV client.

ATT checks expiry at access time, including every long-read fragment, even when a separate owner poll has not yet run. Provider-owned descriptor bytes are explicitly zeroed at end/close, including native-retained cleanup. Data already sent while authorized cannot be recalled from a central or controller queue; the WebDAV listener must enforce its own real deadline.

## GATT wire profile

All multibyte fields are little-endian. UUIDs are project-specific experimental profile UUIDs, not Bluetooth SIG assignments.

| Item | UUID | Properties |
| --- | --- | --- |
| Primary service | `cc12f001-6b62-4c86-a72d-1b4864247521` | Advertised, no credentials in advertisements |
| Descriptor | `cc12f002-6b62-4c86-a72d-1b4864247521` | Authenticated encrypted read, including long reads |
| Validity | `cc12f003-6b62-4c86-a72d-1b4864247521` | Authenticated encrypted read |

The descriptor is immutable for its complete session, at most 464 bytes:

| Offset | Type | Meaning |
| --- | --- | --- |
| 0 | u8 | Schema 1 |
| 1 | u8 | HTTP=1, HTTPS=2 |
| 2 | u16 | Total descriptor bytes |
| 4 | u32 | WebDAV remaining lifetime copied at open, in milliseconds; informational, not a current countdown |
| 8 | 4 × u16 | URL, username, password, session-ID byte lengths |
| 16 | concatenated bytes | Four ASCII strings, without NUL terminators |

URL length is 1–256; all three other lengths are 1–64. The complete value fits ATT's 512-byte maximum. NimBLE implements ATT offsets, access controls and fragmentation; no ad hoc GATT/SMP implementation is used. Neither characteristic is writable or subscribable.

The 16-byte validity snapshot is `{u32 setup_remaining_ms, u32 connection_generation, u64 webdav_remaining_ms}`. These are decreasing durations, never boot-epoch timestamps. A central reads validity, reads the complete descriptor, then reads validity again. It rejects a changed connection generation, zero/invalid lifetime, incomplete descriptor, or a read sequence taking longer than the first setup remaining duration. Use a local monotonic clock and subtract the complete read interval from the first WebDAV remaining duration to form a conservative local deadline.

## Reference central and verification

[ble_session_setup_central.py](../../examples/ble_session_setup_central.py) uses Bleak's OS pairing and long-read support. Install Bleak in a user-controlled Python environment, then run `python examples/ble_session_setup_central.py --out temporary-session.json`, optionally selecting `--address`. The OS and watch must display matching six-digit numbers. The helper creates a new mode-0600 file exclusively, does not print secrets, never contacts WebDAV, and never disables HTTPS certificate checks. Remove the temporary credentials after use. This helper's decoder is tested without a Bluetooth adapter; OS/RF interoperability remains unverified.

Run `bash test/run_ble_session_setup_test.sh`, then `SANITIZE=1 bash test/run_ble_session_setup_test.sh`. These run the production NimBLE host against the existing independent deterministic HCI central and OpenSSL-backed ECDH/CMAC oracle. No device or RF operation is performed. Scenarios cover full numeric-comparison security, deliberate DHKey tampering, legacy/short-key/invalid-public-key rejection, completed unauthenticated SC, pre-auth ATT reads, maximum-length descriptor/long-read offsets, write denial, copied input lifetime, buffer erasure, stale token/generation/handle events, expiry, disconnect/re-pairing, retained native custody, failed claim recovery, and 20 immediate close/reopen cycles.

`python scripts/build_ble_session_setup.py` cross-builds the Xtensa ESP32-S3 ET_DYN artifact, validates pinned source hashes, allows only explicit libc imports, requires the sole `t5_driver_get` export, and records allocated section sizes and reused-port hashes. See [cleanup-cooperation-0.1.1.md](cleanup-cooperation-0.1.1.md) for this successor checkpoint. The original [0.1.0 verification receipt](verification.md) is preserved unchanged. Hardware performance, OS numeric-comparison UX, actual Wi-Fi/WebDAV/BLE coexistence and product deployment are separate, unperformed qualification steps.

# usb-xinput-gamepad

## Purpose and identity

`usb-xinput-gamepad` is the ABI-v2 `usb.xinput.gamepad@1` provider for Xbox-360-format wired devices and wireless/2.4 GHz receivers. It consumes `usb.host@1` plus `platform.clock@1`; physical USB enumeration, claims, and interrupt transfers remain owned by the lower host provider.

Verified current package identity:
- ID/version: `usb-xinput-gamepad` 0.1.3
- driver ABI / architecture: 2 / `xtensa-esp32s3`
- source: `Drivers/usb_xinput_gamepad/driver.c`
- upstream source-tree SHA: `b84a41ad08114f0a6e0aa744582056d1a13879c4`
- driver blob: `7f68505443ffafe3ac3f2cb6f24e3ca5315b98b3`
- manifest blob: `54566d90d5b4899601e1b32f267e42b0d30159d8`
- exact upstream fixture blob: `f2d02e3189754387a46ce840894b17de30571da5`
- requires: `usb.host@1`, `platform.clock@1`
- provides: `usb.xinput.gamepad@1`
- source status string: `experimental-unpublished`
- release tag: `driver-usb-xinput-gamepad-v0.1.3`
- canonical ELF: 11,088 bytes
- canonical ELF SHA-256: `e2cee3355937d7815fb5685598cd2411c7d508bb36424f00636ec7495f654c26`

Observed package files also establish `.package.json` 673 bytes / SHA-256 `d330f0114ee5d6023a74b994afb1a55af60f73c62a63fde61b3c13e6edd51ccc`, `provider-abi.v1` 47 bytes / `34a80c48f2d2cef60c5578a4a1f1ec14906876a54cda5196a81aea39838c237c`, and `privileged-imports.v1` 14 bytes / `a46fff766cb063bc348b3e7a6670e4a48c5c9201f0da92e73a67670229be7cca`.

## Source and shared interfaces

The implementation includes `RiscUsbInterruptV1.h`, `RiscUsbGamepadDiagnosticsV1.h`, `RiscPlatformClockV1.h`, and the shared `Drivers/usb_hid_gamepad/StateMailbox.h`. Those destination files already match current upstream exactly:
- interrupt-host extension: `ace65e02ed10daa9d452cbd6e789c6e0a6b01135`
- gamepad diagnostics ABI: `75e2f396069663d9d0be21440681a4810aca9e61`
- platform clock ABI: `33a5b27dcea7e30698d0177ad5db065166082194`
- state mailbox helper: `9b6dc1e4c8ecf8ddc2d64da4e862f86b3a530793`

The intended sole ELF export is `t5_driver_get`, which returns the static ABI-v2 driver descriptor only for ABI 2.

## Dependency binding and lifecycle

`start` requires exactly two dependencies, in either order: one `usb.host@1` interrupt-capable host API and one `platform.clock@1` API. The host must provide configuration discovery, claim/release, discovery poll/device enumeration, and interrupt reads. The clock must provide `monotonic_ms`. Missing, duplicate, unknown, structurally short, or incomplete dependencies fail startup, as does a second start.

The provider uses fixed static storage: four pad slots, up to `RISC_USB_HOST_MAX_DEVICES` inspection records, and `RISC_USB_INPUT_MAX_SUBSCRIBERS` subscriber mailboxes. There is no dynamic allocation or internal locking.

`quiesce` first requires every subscriber slot to be released, then disconnects/releases all pad claims. `stop` does nothing until quiescent; successful stop clears host/clock pointers and inspection state. Subscription serial generation is not reset, so stale subscription tokens do not become valid after restart.

## Discovery and matching

Discovery is descriptor-driven rather than VID/PID-gated. This is intentional: the exact upstream fixture verifies successful binding to a clone with VID/PID `1234:9876`.

A qualifying interface must be:
- USB vendor class `0xff`;
- subclass `0x5d`;
- protocol `0x01` for wired format or `0x81` for wireless-receiver format;
- equipped with one interrupt-IN endpoint;
- endpoint number nonzero with no reserved address bits;
- max packet size from 20 through 64 bytes;
- nonzero polling interval.

The parser accepts nonzero alternate settings and non-default endpoint numbers. Bulk endpoints are rejected. Structurally truncated or length-mismatched configurations fail closed. Once an unsupported descriptor is successfully read, it is cached as unsupported instead of being rescanned indefinitely.

The provider can hold four pad claims. If all pad slots are occupied, a qualifying inspected device enters a waiting-capacity state and resumes only after a slot is freed.

## Bounded startup and retry behavior

`poll` requires `max_reports` from 1 through 16. It gives the USB host discovery layer a fixed budget of 8 events, then reconciles currently attached generation-qualified device tokens against pad and inspection state.

Each attachment gets at most eight discovery attempts within ten seconds. Retry backoff is 100, 200, 400, 800, 1600, 2000, and 2000 ms. Configuration failures and interface-claim failures retry; successful descriptors are cached so claim retries do not reread configuration. A device removed during backoff loses its inspection record, preventing a stale claim. A replacement generation is inspected afresh.

A clock value of `UINT64_MAX` pauses discovery and surfaces `XINPUT DISCOVERY CLOCK FAILED`. A backwards clock observation does not trigger immediate repeated USB work.

## Interrupt input and connection state

For each claimed pad, `poll` performs bounded interrupt reads with a 64-byte stack buffer and 10 ms host timeout. It stops retrying a pad within the same call once the endpoint returns zero bytes. Negative or oversized interrupt-read results disconnect the logical gamepad state and return failure while retaining the physical claim, allowing a later valid report to reconnect.

Wired-format input requires at least 20 received bytes, report byte 0 equal to zero, and the packet's internal header length byte at least 14. Short, LED/status, and malformed reports are ignored without releasing held state.

Wireless receivers process the receiver prefix before the same 20-byte payload decoder. A presence message with bit 3 set in byte 0 and bit 7 set in byte 1 generates a connection event when needed. A corresponding message without bit 7 generates disconnect. Input message type 1 requires at least 24 bytes total; the four-byte wireless prefix is then removed before decoding.

## Gamepad normalization

The provider publishes `risc_usb_gamepad_state_v1` and event kinds used by the shared gamepad ABI: connect, disconnect, and changed-state events.

The Xbox semantic face-button mapping is preserved rather than swapping controls by physical position. The exact fixture verifies A/B/X/Y map to output values 2/1/8/4 respectively. Shoulder buttons, digitalized trigger thresholds, Back, Start, stick clicks, and Guide are also mapped into the 32-bit buttons field.

D-pad bits are normalized to the standard hat values 0-7 with 8 as neutral. Simultaneous cardinal combinations become diagonal hats.

Left X/Y and right X/Y are decoded as signed little-endian 16-bit axes. Y axes are inverted by bitwise complement. The two 8-bit trigger bytes are expanded across the signed 16-bit range using `value * 257 - 32768`, so 0 becomes -32768 and 255 becomes 32767.

Unchanged reports do not emit redundant state events.

## Subscribers, filtering, and mailbox behavior

`subscribe` allocates one of the fixed subscriber slots and returns a monotonically increasing token. A filter of zero receives all devices; a nonzero filter receives only that device token. `unsubscribe` clears the matching subscriber. `next` returns -1 for invalid/stale tokens, zero when no event is pending, and positive when delivering an event.

The shared state-mailbox implementation coalesces state churn instead of allowing an unbounded queue. The upstream fixture drives thousands of alternating reports and verifies that the subscriber still receives the current state while the provider's snapshot remains current.

`snapshot` reports all currently claimed pad slots, including claimed-but-not-yet-connected wireless/idle devices, using size-query semantics when the caller's output capacity is insufficient.

## Diagnostics and failure handling

The diagnostics extension writes the current textual status into a caller-provided bounded buffer. Established statuses include driver-not-started, waiting for discovery/report, connected, configuration retry/limit, interface-claim retry/limit, retry-deadline, waiting-for-pad-capacity, clock failure, USB discovery failure, and interrupt-read failure.

Disconnecting a physically removed claimed device emits a logical disconnect before releasing the claim. If event-sequence generation is exhausted, release/quiescence can fail rather than silently losing required lifecycle notification.

## Validation coverage

The exact upstream host fixture at blob `f2d02e3189754387a46ce840894b17de30571da5` exercises:
- dependency-order independence and structural API validation;
- pre-existing devices at startup;
- configuration-read recovery without busy retry;
- cached descriptors across claim retries;
- eight-attempt and ten-second retry limits;
- clock failure and stale-attachment removal;
- unsupported-descriptor caching;
- partial/status report rejection;
- face buttons, shoulders, Guide, triggers, stick axes, and all D-pad combinations;
- unchanged-report suppression;
- mailbox coalescing under thousands of updates;
- hot-unplug disconnect/release;
- clone VID/PID binding;
- nonzero alternate settings and non-default interrupt endpoint;
- wireless receiver presence/input/disconnect framing;
- bulk/truncated descriptor rejection;
- claim retry without unplug/replug;
- interrupt-read failure, logical disconnect, and subsequent reconnect;
- quiescence, still-plugged shutdown, restart, and stale-token rejection.

The expected terminal result is:
`XInput discovery, current state, bounded startup retries, hotplug and shutdown: PASS`.

This fixture is deterministic provider simulation, not physical-controller or RF validation.

## Build and release validation

`scripts/build_usb_xinput_gamepad.py` validates the exact manifest, builds a 32-bit little-endian Xtensa ET_DYN ELF, normalizes supported Xtensa relocations, requires `t5_driver_get` as the sole global function export, and requires exactly `memcpy` and `memset` as unresolved imports. It records generated size/SHA-256 and whether the result matches the canonical 11,088-byte artifact.

## Established limitations

The implementation supports the Xbox-360-format interface described above; it does not issue HID requests, USB resets, rumble/output effects, controller-specific power changes, or a general Xbox One/Series protocol. It exposes four simultaneous pad slots, uses fixed subscriber capacity, bounds discovery to eight attempts/ten seconds per attachment, limits one interrupt report to 64 bytes, and establishes no internal multi-thread synchronization.

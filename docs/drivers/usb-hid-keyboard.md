# usb-hid-keyboard

## Purpose and scope

`usb-hid-keyboard` is an ABI-v2 class provider that consumes `usb.hid@1` and publishes `usb.hid.keyboard@1`. It owns boot-keyboard session selection and interpretation, copied keyboard event queues, per-device state snapshots, and subscriber lifecycle. USB controller ownership, descriptor enumeration, interface claims, interrupt transfers, and physical HID session lifetime remain below this provider in `usb.hid`.

## Package identity

- Driver ID: `usb-hid-keyboard`
- Version: `0.1.1`
- Driver ABI: `2`
- Architecture: `xtensa-esp32s3`
- Source path: `Drivers/usb_hid_keyboard`
- Upstream source tree SHA: `64fe4961c973555eafb1a8ddd035455b934c7f1e`
- Upstream source blob: `859071a1b6e34ae5a72eac04b74aa507c80d3eb4`
- Manifest blob: `010b7f9bde0418dfa7ec51117a85391ebe8f4319`
- Requires: `usb.hid@1`
- Provides: `usb.hid.keyboard@1`
- Source manifest status: `experimental-unpublished`
- Published tag: `driver-usb-hid-keyboard-v0.1.1`
- Canonical ELF: 8,444 bytes
- Canonical ELF SHA-256: `4c9f55b63b89c7464d386401acd955df6ea3d96fb58933c13c7f1fdabe02691e`

The source manifest status is recorded separately from the observed release-index publication.

## ABI and dependency validation

The driver uses the already-migrated `RiscUsbHidV1.h` ABI. The destination header blob `47913ebeda94dcc4138173be45eafe9315191e91` exactly matches current upstream.

`t5_driver_get(uint32_t)` is the sole provider entry point and returns the static ABI-v2 descriptor only for ABI 2.

`start` accepts exactly one `usb.hid@1` dependency. It validates the dependency's API version and full structure size and requires `scan`, `interfaces`, `open`, `set_boot_protocol`, `read`, `present`, and `close`. Startup fails if the provider is already bound or the dependency is malformed.

## Fixed capacity and state

The implementation performs no dynamic allocation.

- Maximum simultaneously tracked boot keyboards: `KEYBOARDS = 4`.
- Maximum subscribers: `RISC_USB_INPUT_MAX_SUBSCRIBERS = 4`.
- Per-subscriber queue depth: `RISC_USB_INPUT_QUEUE_LENGTH = 32`.
- HID read buffer: `RISC_USB_HID_MAX_REPORT = 64`, though accepted boot reports are exactly 8 bytes.
- `poll` accepts a caller report budget from 1 through 16.

Each keyboard slot records the generation-qualified device identity, HID session token, interface/alternate, modifier bitmap, and six boot-keyboard usage slots. Each subscriber stores an opaque monotonic token, optional device filter, copied event ring, and a gap flag.

The code relies on the generic provider executor to serialize entry points; there are no internal locks.

## Boot-keyboard discovery and session ownership

Every `poll` first calls `usb.hid.scan(..., 16)` and snapshots the available HID interfaces. A candidate is accepted only when HID subclass is 1 and protocol is 1, corresponding to a boot keyboard. Existing sessions are matched by device, interface, and alternate.

For a new candidate, the provider calls `open(device, interface, alternate)` and then `set_boot_protocol(..., true)`. Failure to set boot protocol closes the just-opened HID session and leaves the device unbound. Successful binding emits a copied connected event and stores the session.

A tracked session is released when its interface disappears from the HID interface snapshot or `present` becomes false. Release emits key-up events for all held ordinary keys and modifiers, then a disconnected event, then closes the HID session. The slot is zeroed only after all required event emission and close operations succeed.

## Boot report validation and transition order

Only 8-byte reports with reserved byte 1 equal to zero are interpreted. HID rollover/error usages 1 through 3 cause that report to be ignored. Duplicate nonzero usages within the six-key array are also ignored.

The provider compares the new report against the prior state. Transition emission order is:

1. modifier key-up events,
2. ordinary key-up events,
3. modifier key-down events,
4. ordinary key-down events.

Modifier usages are represented as HID page-0x07 usages `0xe0` through `0xe7`. Events include the provider-global monotonically increasing sequence, device identity, kind, usage, and current modifier bitmap.

## Poll scheduling

After discovery, `poll` spends the caller's report budget across tracked keyboards in rounds. A quiet endpoint is skipped after its first zero-length read for the remainder of that call, allowing active keyboards to consume the remaining budget.

A negative read fails the poll when the HID session is still present. If `present` reports that the physical/session identity is gone, the provider releases that keyboard instead.

## Subscriber queues and overflow behavior

`subscribe` returns a monotonically increasing nonzero token until token exhaustion. A filter of zero accepts all keyboards; a nonzero filter receives only events from the matching device.

Each event is copied into every matching subscriber queue. If a subscriber queue reaches 32 events, that subscriber is marked `gap`, and its queue is cleared. No further events are queued for that subscriber until `next` reports the gap. `next` returns:

- `1` with one copied event,
- `0` when the queue is empty,
- `-1` for unknown/stale subscription or after an overflow gap.

After a gap return, queue state is reset so the consumer can recover by using `snapshot`.

`unsubscribe` zeroes the complete subscriber slot; stale/zero tokens fail.

## Snapshot behavior

`snapshot` counts currently open keyboard sessions. If capacity is too small, or no output buffer is supplied for a nonzero count, it writes the required count and returns false. On success it returns device identity, modifier bitmap, the six held usages, and `connected = 1` for each live session.

## Quiescence and stop

Quiescence is forbidden while any subscriber token remains live. Once all subscribers are gone, `quiesce` releases every keyboard HID session, including physically attached devices, so the parent HID/host providers and VBUS ownership are not pinned by an unused class provider.

`stop` clears the `usb.hid` dependency only after quiescence succeeds.

## Validation

The staged repository-local host fixture exercises boot-keyboard discovery, boot protocol selection, ordinary and modifier down/up transition ordering, subscription delivery, snapshot sizing/content, live-subscriber quiescence refusal, physical disappearance/disconnect cleanup, queue-overflow gap behavior, and final provider cleanup.

This is provider-level simulation. It does not validate a physical keyboard, host controller, HID descriptor parsing, or USB timing.

## Standalone Xtensa build and package metadata

The standalone builder validates the exact manifest, builds an ELF32 little-endian Xtensa ET_DYN image, normalizes supported relocations, requires `t5_driver_get` as the only global function export, and requires exactly `memcpy` and `memset` as unresolved runtime imports. The published `privileged-imports.v1` has the same 14-byte SHA-256 identity used by already-reproduced providers with that exact import pair.

Published package metadata:

- `.package.json`: 625 bytes, SHA-256 `92d2621ef96ac6c8637eda977f07ae546cf2b646dbaf338c810172adf2af885f`
- `driver.elf`: 8,444 bytes, SHA-256 `4c9f55b63b89c7464d386401acd955df6ea3d96fb58933c13c7f1fdabe02691e`
- `provider-abi.v1`: 45 bytes, SHA-256 `d9354efb9bae9d9d834a85ae18899184a348f02290dbe1286575db7cf1122080`
- `privileged-imports.v1`: 14 bytes, SHA-256 `a46fff766cb063bc348b3e7a6670e4a48c5c9201f0da92e73a67670229be7cca`

CI run `36312749073` passed the repository parity/documentation checks and the keyboard host fixture, and produced an 8,444-byte ELF with SHA-256 `4c9f55b63b89c7464d386401acd955df6ea3d96fb58933c13c7f1fdabe02691e`, exactly matching the published canonical artifact.

## Established limitations

This provider intentionally supports boot-protocol keyboards only. It does not parse arbitrary keyboard report descriptors itself, implement N-key rollover, or expose output reports such as keyboard LEDs. Six-key rollover follows the boot keyboard report format. Capacity is four keyboards and four subscribers. The class provider emits copied state/events only; all USB transport ownership remains in `usb.hid`.

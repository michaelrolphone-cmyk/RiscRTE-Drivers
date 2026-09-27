# usb-hid-gamepad

## Purpose and scope

`usb-hid-gamepad` is an ABI-v2 class provider that consumes `usb.hid@1` and publishes `usb.hid.gamepad@1`. It parses HID report descriptors for joystick/gamepad application collections, converts supported button/axis/hat fields into normalized gamepad state, exposes current-state snapshots, maintains a compatibility notification mailbox, and provides a bounded discovery-status diagnostic extension. USB enumeration, interface claims, descriptor transport, interrupt reads, and physical HID session ownership remain below it in `usb.hid`.

## Package identity

- Driver ID/version: `usb-hid-gamepad` 0.1.3
- Driver ABI/architecture: ABI 2, `xtensa-esp32s3`
- Source path/tree: `Drivers/usb_hid_gamepad`, tree `9baecb41299b4c9ba35a71df2c7fb47f93d43e03`
- Driver blob: `91fdbd1e9898ac021e2d476adaa447037c0359fa`
- Manifest blob: `4cf609038c823adab571b229520cee340040aef7`
- Mailbox helper blob: `9b6dc1e4c8ecf8ddc2d64da4e862f86b3a530793`
- Diagnostics ABI blob: `75e2f396069663d9d0be21440681a4810aca9e61`
- Requires: `usb.hid@1`
- Provides: `usb.hid.gamepad@1`
- Source status: `experimental-unpublished`
- Published tag: `driver-usb-hid-gamepad-v0.1.3`
- Canonical ELF: 13,980 bytes
- Canonical SHA-256: `16142ccae4a8cea40cc94f2ef38145d3fb27021a5f7314417b0098a520c1da0e`

## ABI and dependency binding

The provider uses `RiscUsbHidV1.h` plus the append-only `RiscUsbGamepadDiagnosticsV1.h` extension. The extension embeds `risc_usb_gamepad_api_v1` as its prefix and appends a bounded diagnostic callback. Its base `struct_size` is the size of the full extension table so consumers can detect the suffix while older consumers use the unchanged prefix.

`t5_driver_get` is the sole provider entry point. `start` accepts exactly one `usb.hid@1` dependency and requires `scan`, `interfaces`, `open`, `report_descriptor`, `read`, `present`, and `close`.

## Static capacities and descriptor parser

The implementation performs no dynamic allocation. It has four pad slots, at most 40 parsed fields per pad, a four-entry HID global-state stack, 32 pending local usages, four compatibility subscribers, descriptors bounded to 512 bytes, and reports bounded to 64 bytes.

The parser rejects malformed item lengths and report IDs, global push/pop imbalance, collection depth over 16, oversized report count/size/layouts, more than 40 supported fields, invalid axis logical ranges, and unfinished collections/stacks. Long HID items are bounds-checked and skipped.

It selects the first top-level Application Collection on Generic Desktop page 1 with Joystick usage 4 or Game Pad usage 5. Report bit positions are tracked independently for all 256 report IDs. The first report layout that yields supported input fields is selected; other layouts are ignored after selection.

Supported variable fields are Button page usages 1–32 with one-bit width, Generic Desktop X/Y/Z/Rx/Ry/Rz usages 0x30–0x35 through 16 bits, and Hat Switch 0x39 through 8 bits. Unsupported/constant fields still advance bit positions.

## Extraction and normalization

Supported fields are extracted at arbitrary bit offsets. Signed values are sign-extended when the descriptor logical minimum is negative. Axis values are clamped to the logical range and mapped to -32767 through +32767. The implementation uses a bounded 32-bit divider instead of a 64-bit compiler helper because the source records that the helper can introduce an unsupported Xtensa relocation.

Buttons map HID Button 1 to bit 0 through Button 32 to bit 31. Hat values within the declared range map to 0–7; invalid/out-of-range values become 8 (neutral).

## Discovery and session ownership

Each `poll` calls `usb.hid.scan(...,16)` and snapshots HID interfaces. Existing gamepad sessions are released if their generation-qualified device/interface/alternate disappears or `present` fails. Boot keyboard/mouse interfaces (subclass 1, protocol 1 or 2) are skipped.

A candidate is opened through `usb.hid.open`; its report descriptor must be read and parsed successfully. Failed claims are skipped, while unreadable/unsupported descriptors are closed before continuing. A successful candidate stores the selected layout and emits a connected state. Disconnect release emits a neutral disconnected state and closes the HID session before clearing the slot.

## Polling and current state

The caller work budget must be 1–16 reports. Reads use 10 ms deadlines and are distributed in rounds across active pads. A zero-length read marks that pad quiet for the rest of the call. A negative read releases a no-longer-present session; otherwise it fails the poll.

For report-ID layouts, nonmatching IDs are ignored. Reports shorter than the selected bit layout are ignored. The state is rebuilt with buttons cleared and hat default 8, then fields are applied. A state event is emitted only if buttons, six axes, or hat changed.

## Compatibility mailbox and snapshots

`StateMailbox.h` stores only the latest pending state per device for each subscriber; it is intentionally not a historical button FIFO. A state update can replace a pending connection while preserving connection kind. Four distinct attachment generations can be retained; another marks a gap, clears pending state, and makes the next read return -1. Pending devices are delivered by lowest global sequence.

Subscriptions have monotonically increasing nonzero tokens and optional device filters. `snapshot` returns current state for each live gamepad and reports required capacity when absent/too small.

## Diagnostics

The append-only diagnostic callback copies the current status string with NUL termination. Source statuses distinguish not-started/waiting, discovery poll failure, no-HID/no-gamepad, capacity exhaustion, claim failure, descriptor read/parse failure, interrupt-read failure, and `GAMEPAD INPUT LAYOUT CONNECTED`.

## Quiescence and unload

Any live subscriber prevents quiescence. With no subscribers, quiescence closes every remaining gamepad session even if the controller remains attached, so this class provider does not pin lower providers. `stop` clears the HID dependency only after quiescence succeeds.

## Validation

The repository host fixture uses a bounded Game Pad descriptor with four buttons plus X/Y axes. It validates descriptor-driven connection, endpoint axis normalization, snapshots, diagnostics, state-change notification, subscriber-gated quiescence, disappearance/disconnect cleanup, and final unload. It is deterministic provider simulation, not physical USB validation.

## Build and package metadata

The standalone Xtensa builder validates the exact manifest, ELF32 little-endian Xtensa ET_DYN format, sole global function export `t5_driver_get`, and exactly the `memcpy`/`memset` runtime import pair, then records canonical parity.

Published v0.1.3 files:
- `.package.json`: 625 bytes, SHA-256 `040ad8ea7d18bc95eee3ee9bad588c2693689288483e298cd5eaa7db12bc0796`
- `driver.elf`: 13,980 bytes, SHA-256 `16142ccae4a8cea40cc94f2ef38145d3fb27021a5f7314417b0098a520c1da0e`
- `provider-abi.v1`: 44 bytes, SHA-256 `6b5f0f4c7e97304a917e44e9dad87ee2b61419ccaef74217efd1fbbd918b85bf`
- `privileged-imports.v1`: 14 bytes, SHA-256 `a46fff766cb063bc348b3e7a6670e4a48c5c9201f0da92e73a67670229be7cca`

Byte-for-byte parity is recorded only after integrated CI reproduces the canonical ELF.

## Established limitations

Only the first usable joystick/gamepad application and first supported report layout are exposed per HID interface. The parser supports the bounded button/six-axis/hat subset above rather than arbitrary HID semantics. Compatibility `next` coalesces current state and is not an ordered input journal; consumers should prefer `poll` plus `snapshot`. Capacity is four active pads and four subscribers.

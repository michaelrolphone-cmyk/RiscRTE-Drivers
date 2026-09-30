# usb-hid-gamepad

## Purpose and scope

`usb-hid-gamepad` is an ABI-v2 class provider that consumes `usb.hid@1` and publishes `usb.hid.gamepad@1`. It parses HID report descriptors for joystick/gamepad application collections, converts supported button/axis/hat fields into normalized gamepad state, exposes current-state snapshots, maintains a compatibility notification mailbox, and provides a bounded discovery-status diagnostic extension. USB enumeration, interface claims, descriptor transport, interrupt reads, and physical HID session ownership remain below it in `usb.hid`.

## Package identity

- Driver ID/version: `usb-hid-gamepad` 0.1.4
- Driver ABI/architecture: ABI 2, `xtensa-esp32s3`
- Source path/tree: `Drivers/usb_hid_gamepad`, tree `ef229494933a41fb8215cae58cc7896d0d680630`
- Driver blob: `0e31b792cf9ff982ef4769afedae4de764333469`
- Manifest blob: `ed8ded3575458372f88fb5fd6a55d9bc28b49789`
- Mailbox helper blob: `9b6dc1e4c8ecf8ddc2d64da4e862f86b3a530793`
- Diagnostics ABI blob: `75e2f396069663d9d0be21440681a4810aca9e61`
- Requires: `usb.hid@1`, `platform.clock@1`
- Provides: `usb.hid.gamepad@1`
- Source status: `experimental-unpublished`
- Published tag: `driver-usb-hid-gamepad-v0.1.4`
- Canonical ELF: 14,444 bytes
- Canonical SHA-256: `2d543f6a04e5b94192f732c64f2f5a436861e2a569f05fb4c6617ae7267db86c`

## ABI and dependency binding

The provider uses `RiscUsbHidV1.h` plus the append-only `RiscUsbGamepadDiagnosticsV1.h` extension. The extension embeds `risc_usb_gamepad_api_v1` as its prefix and appends a bounded diagnostic callback. Its base `struct_size` is the size of the full extension table so consumers can detect the suffix while older consumers use the unchanged prefix.

`t5_driver_get` is the sole provider entry point. `start` accepts exactly two dependencies in either order: `usb.hid@1` and `platform.clock@1`. The HID table must provide `scan`, `interfaces`, `open`, `report_descriptor`, `read`, `present`, and `close`; the clock table must report API v1, a `struct_size` covering the declared table, and a non-null `monotonic_ms` callback. Unknown, duplicate, missing, or invalid dependencies fail start.

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

Non-boot interfaces are tracked in an attachment-scoped `inspected` cache keyed by generation-qualified device handle, interface number, and alternate setting. Discovery issues at most one claim/report-descriptor attempt per `poll`, then returns to the caller. Failed claims and descriptor-read failures retry after exponential delays beginning at 100 ms and capped at 2,000 ms, with at most eight attempts and a 10,000 ms lifetime from the first attempt. A successfully read but unsupported descriptor is marked done for that attachment. `UINT64_MAX` from the clock suppresses an attempt, and a clock value earlier than the last attempt also suppresses retry. Cache entries are cleared when the corresponding attachment/interface disappears.

A candidate is opened through `usb.hid.open`; its report descriptor must be read and parsed successfully. Failed descriptor sessions are closed. If that close fails, the handle remains in the cache and later polling/quiescence retries cleanup instead of losing ownership. A successful candidate stores the selected layout and emits a connected state. Disconnect release emits a neutral disconnected state and closes the HID session before clearing the slot. Gamepad-capacity exhaustion records the diagnostic and stops discovery for the current poll without converting the whole poll to a transport failure, allowing already connected pads to continue being serviced.

## Polling and current state

The caller work budget must be 1–16 reports. Reads use 10 ms deadlines and are distributed in rounds across active pads. A zero-length read marks that pad quiet for the rest of the call. A negative read releases a no-longer-present session; otherwise it fails the poll.

For report-ID layouts, nonmatching IDs are ignored. Reports shorter than the selected bit layout are ignored. The state is rebuilt with buttons cleared and hat default 8, then fields are applied. A state event is emitted only if buttons, six axes, or hat changed.

## Compatibility mailbox and snapshots

`StateMailbox.h` stores only the latest pending state per device for each subscriber; it is intentionally not a historical button FIFO. A state update can replace a pending connection while preserving connection kind. Four distinct attachment generations can be retained; another marks a gap, clears pending state, and makes the next read return -1. Pending devices are delivered by lowest global sequence.

Subscriptions have monotonically increasing nonzero tokens and optional device filters. `snapshot` returns current state for each live gamepad and reports required capacity when absent/too small.

## Diagnostics

The append-only diagnostic callback copies the current status string with NUL termination. Source statuses distinguish not-started/waiting, discovery poll failure, no-HID/no-gamepad, capacity exhaustion, claim failure, descriptor read/parse failure, interrupt-read failure, and `GAMEPAD INPUT LAYOUT CONNECTED`.

## Quiescence and unload

Any live subscriber prevents quiescence. With no subscribers, quiescence closes every remaining gamepad session and every retained failed-close discovery handle. A failed close makes quiescence fail and preserves ownership for a later cleanup attempt. `stop` clears both the HID and clock dependencies only after quiescence succeeds.

## Validation

The repository host fixture is synchronized from the current upstream descriptor/lifecycle test. In addition to descriptor parsing, normalized input, snapshots, diagnostics, disconnect/reconnect, and unload behavior, it exercises composite supported/unsupported interfaces over 500 steady-state polls, one-attempt-per-poll discovery, retry timing/deadline handling, clock faults/regression, bounded eight-attempt lifetime retries, reconnect cache reset, continued servicing of a valid pad beside an auxiliary interface, and failed-close ownership through quiescence. It is deterministic provider simulation, not physical USB validation.

## Build and package metadata

The standalone Xtensa builder validates the exact manifest, ELF32 little-endian Xtensa ET_DYN format, sole global function export `t5_driver_get`, and exactly the `memcpy`/`memset` runtime import pair, then records canonical parity.

Published v0.1.4 files:
- `.package.json`: 669 bytes, SHA-256 `1ce64645dbbc23d89ac87851d3eff8e6da55139cf45d2a1f4a35bf56de842b01`
- `driver.elf`: 14,444 bytes, SHA-256 `2d543f6a04e5b94192f732c64f2f5a436861e2a569f05fb4c6617ae7267db86c`
- `provider-abi.v1`: 44 bytes, SHA-256 `6b5f0f4c7e97304a917e44e9dad87ee2b61419ccaef74217efd1fbbd918b85bf`
- `privileged-imports.v1`: 14 bytes, SHA-256 `a46fff766cb063bc348b3e7a6670e4a48c5c9201f0da92e73a67670229be7cca`

Destination CI run `36673309318` completed the v0.1.4 migration gate. The synchronized host fixture passed (`Gamepad feature IDs, unrelated inputs, multiple collections, input delivery and detach: PASS`), and `scripts/build_usb_hid_gamepad.py` produced the published 14,444-byte ELF SHA-256 `2d543f6a04e5b94192f732c64f2f5a436861e2a569f05fb4c6617ae7267db86c` with `byte_parity=True`. The repository parity/documentation job in the same run also passed. This establishes destination source/build/test/documentation parity for the currently inspected upstream release.

## Established limitations

Only the first usable joystick/gamepad application and first supported report layout are exposed per HID interface. Initial discovery or a scheduled retry can still incur the lower HID provider's synchronous report-descriptor control-transfer latency; v0.1.4 bounds how often those attempts are issued rather than eliminating that single-attempt cost. The parser supports the bounded button/six-axis/hat subset above rather than arbitrary HID semantics. Compatibility `next` coalesces current state and is not an ordered input journal; consumers should prefer `poll` plus `snapshot`. Capacity is four active pads and four subscribers.

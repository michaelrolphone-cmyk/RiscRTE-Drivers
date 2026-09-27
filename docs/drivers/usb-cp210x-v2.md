# usb-cp210x-v2

## Purpose and scope

`usb-cp210x-v2` is an ABI-v2 `serial.port@1` provider for Silicon Labs CP210x USB-to-serial devices. It owns CP210x matching, vendor-control protocol, serial configuration, session state, and stale-token behavior. Physical USB enumeration, interface claims, control transfers, bulk transfers, and claim release remain owned by the separate `usb.host@1` provider.

## Package identity

- Driver ID: `usb-cp210x-v2`
- Version: `0.1.0`
- Driver ABI: `2`
- Architecture: `xtensa-esp32s3`
- Source path: `Drivers/usb_cp210x_v2`
- Upstream source tree SHA: `876bba87ab601fafbcd873ae3fd0e14f96151d3b`
- Manifest status: `experimental-unpublished`
- Requires: `usb.host@1`
- Provides: `serial.port@1`
- Published tag: `driver-usb-cp210x-v2-v0.1.0`
- Canonical ELF: 5,788 bytes
- Canonical ELF SHA-256: `96f22bbb6cc8cf00f138656a910d3078d491d6c117ffc4e23a2842c458481c61`

The source manifest status is preserved independently from the observed release-index publication.

## ABI and dependency binding

`t5_driver_get` returns the static ABI-v2 descriptor only for ABI 2. `start` requires exactly one `usb.host@1` dependency and validates the host table's API version, full structure size, configuration/claim/release/control callbacks, and bulk read/write callbacks. A second start while already bound fails.

## Static resources

The implementation allocates no dynamic memory. It keeps four `cp_session` slots, one 4,096-byte configuration buffer, and a 64-bit generation counter. Each session records its generation-safe token, physical device token, host claim, selected interface/alternate, and bulk IN/OUT endpoints. The source contains file-static mutable state and no internal locks, so concurrent thread-safe entry is not established.

## Device matching and descriptor parsing

`open` retrieves the configuration and VID/PID through `usb.host@1`. The current implementation requires VID `0x10c4`; PID is read but deliberately not constrained.

The configuration must begin with a type-2 descriptor. `wTotalLength` must be at least nine bytes and no greater than the returned buffer length. Nested descriptors must stay within that total and have length at least two.

Only vendor-class interfaces (`0xff`) are candidates. Endpoint descriptors are considered when the endpoint transfer type is bulk. Nonzero max packet size must not exceed 512. A duplicate bulk IN or duplicate bulk OUT endpoint on the same interface is rejected. Exactly one vendor-class interface/alternate with both a bulk IN and bulk OUT endpoint must exist; zero or multiple candidates fail closed.

## Open and enable

After parsing, the provider claims the selected interface/alternate through `usb.host@1`. A failed or zero claim aborts. It then enables the CP210x interface using vendor request type `0x41`, request `0x00`, value `1`, selected interface in `wIndex`, zero payload, and 1,000 ms timeout. The control operation must return zero. If enable fails, the host claim is released.

Only after enable succeeds does the provider advance its generation counter, skipping zero on wrap, and publish the session.

## Serial configuration

`configure` accepts baud 300 through 3,000,000, data bits 5 through 8, parity 0 through 4, and one or two stop bits.

Baud is encoded as four little-endian bytes and sent with request `0x1e`, value 0, the session interface index, and 1,000 ms timeout. Success requires four bytes transferred.

Line control is encoded as `(data_bits << 8)`, with nonzero parity shifted left four bits and two stop bits represented by adding value 2. That value is sent with request `0x03`, zero payload, and must return zero. The upstream fixture verifies 115200 baud, 8 data bits, parity value 2, one stop bit, bytes `00 c2 01 00`, and line value `0x0820`.

## DTR and RTS

`control_lines` sends request `0x07` with value `0x0300 | DTR | (RTS << 1)`. The host fixture verifies DTR=true/RTS=false produces `0x0301`.

## Bulk I/O

Reads use the selected bulk IN endpoint and writes use bulk OUT through the same host claim. Both reject invalid sessions, null buffers, zero sizes, and operations larger than 4,096 bytes. Caller timeouts pass through unchanged. A nonnegative host result larger than the requested size is converted to `-1`.

## Close, stop, and quiescence

`close` first disables the physical CP210x interface with request `0x00`, value `0`. If that disable fails, close fails without releasing the claim or clearing the session, so the provider remains non-quiescent. This is an explicit source-level safety property.

After successful disable, the claim is released and token/device/claim are cleared. Stale tokens fail. `quiesce` returns false while any session is live. `stop` clears the host binding only after quiescence.

## Host validation

The exact upstream fixture dynamically loads a host-built shared copy of the driver. It covers ABI gating, dependency binding, pre-open quiescence, non-Silicon-Labs VID rejection, enable-failure unwind, open, invalid line parameters, baud and line-control bytes, DTR/RTS, bulk read/write, failed physical disable preserving the session, successful close, stale-token rejection, generation-safe reopen, final quiescence, and stop.

This validates provider logic against a deterministic host stub; it does not establish physical device/controller behavior.

## Standalone Xtensa validation and release metadata

The staged build script validates the exact manifest, ELF32 little-endian Xtensa ET_DYN output, sole global function export `t5_driver_get`, and no unresolved imports outside `memcpy`/`memset`. It records canonical byte parity.

Published package metadata:
- `.package.json`: 623 bytes, SHA-256 `a2e08a17e3362a481c636af885be2d2a5b9311cf43129925d7378492d56f18e3`
- `driver.elf`: 5,788 bytes, SHA-256 `96f22bbb6cc8cf00f138656a910d3078d491d6c117ffc4e23a2842c458481c61`
- `provider-abi.v1`: 40 bytes, SHA-256 `892780d027085bbba39fce0d795850388a019cb47ec5a4497a5249a01e1a2f60`
- `privileged-imports.v1`: 14 bytes, SHA-256 `a46fff766cb063bc348b3e7a6670e4a48c5c9201f0da92e73a67670229be7cca`

## Established limitations

The implementation constrains VID but not PID, binds exactly one usable vendor-class serial interface, and exposes serial configuration, DTR/RTS, bulk byte streams, and close. It does not implement modem-status reads, flow-control configuration, break signaling, CP210x GPIO features, event notifications, or multiport selection.

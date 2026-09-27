# usb-ftdi

## Purpose and scope

`usb-ftdi` is an ABI-v2 `serial.port@1` provider for supported single-port FTDI USB UART devices. It depends on `usb.host@1`: the host provider retains enumeration, physical interface claims, control/bulk transfers, and USB ownership, while this ELF implements FTDI device identification, vendor control requests, baud encoding, line state, and removal of FTDI's two-byte receive status prefix.

The current `serial.port@1` API opens a USB device rather than selecting one port of a multi-port adapter. The implementation therefore fails closed unless the configuration exposes exactly one usable FTDI vendor-specific bulk interface; it does not choose an arbitrary FT2232/FT4232 channel.

## Package identity

- Driver/package ID: `usb-ftdi`
- Version: `0.1.0`
- Driver ABI: `2`
- Architecture: `xtensa-esp32s3`
- Source path: `Drivers/usb_ftdi`
- Source tree SHA: `e181559f4024d4db1da8f128562f7805a5c21e2c`
- `driver.c` blob: `80eda12b8209f042418a63421a3e6c6b896353c7`
- `manifest.json` blob: `9b34e227debbf4cab125edf57688b996ce4cce6b`
- Host fixture blob: `e09f3252fb83d52426f74d3584c3d80bc9e60e95`
- Requires: `usb.host@1`
- Provides: `serial.port@1`
- Manifest status string: `experimental-unpublished`
- Published tag: `driver-usb-ftdi-v0.1.0`
- Canonical ELF: 8,048 bytes
- Canonical ELF SHA-256: `cf5c2f2777df96df1bf44ec719291ded008b4a3b833c8668a203e14e1a56516e`

The manifest status string and observed release-index publication are separate facts: release-index currently publishes version 0.1.0 despite the literal source status.

## Source, ABI, build, and test files

- `Drivers/usb_ftdi/driver.c`
- `Drivers/usb_ftdi/manifest.json`
- `sdk/driver/RiscProviderV2.h`
- `sdk/driver/RiscUsbProviderV1.h`
- `scripts/build_usb_ftdi.py`
- `test/drivers/usb_ftdi_test.c`

The provider uses the existing shared USB host/serial ABI; no FTDI-specific public ABI header is introduced.

## Exported root and dependency binding

The only intended exported function is `t5_driver_get(uint32_t abi)`. It returns the static `risc_driver_v2` only for ABI 2. The descriptor identifies driver `usb-ftdi`, capability `serial.port`, API 1.

`start` accepts exactly one dependency named `usb.host` at API 1. The dependency table itself must report API 1, be at least the compiled structure size, and provide configuration, claim, release, control, bulk-read, and bulk-write callbacks. Startup fails if already bound or if any required dependency contract is missing.

## Supported hardware identifiers and generation classification

The implementation accepts vendor ID `0x0403` and these product IDs:

- `0x6001` — FT232 family
- `0x6014` — FT232H
- `0x6015` — FT-X family
- `0xFBFA` — accepted FT232RL product ID in the source

After configuration parsing, it reads the 18-byte USB device descriptor with a standard `GET_DESCRIPTOR` control request and classifies by `bcdDevice`:

- `0x0400` / `0x0600` -> BM/R family, only with PID `0x6001` or `0xFBFA`, channel index 0
- `0x0900` -> FT232H, only with PID `0x6014`, channel = interface + 1
- `0x1000` -> FT-X, only with PID `0x6015`, channel = interface + 1

Other generations or PID/generation mismatches are rejected before an interface claim is retained.

## Configuration/interface discovery

The parser requires a complete USB configuration descriptor with type 2 and exact `wTotalLength`. Every nested descriptor must have a valid in-bounds length. A usable interface must be vendor class `0xFF` and expose exactly one valid bulk IN plus one valid bulk OUT endpoint. IN packet size must not exceed 512 bytes; OUT packet size must be nonzero and no more than 512. Endpoint zero and reserved endpoint-address bits are rejected.

Exactly one usable FTDI interface must exist. More than one candidate, duplicate bulk direction, malformed descriptor, or invalid packet size fails binding.

## Static resources and sessions

The provider allocates no heap memory. It uses the shared `RISC_USB_CDC_MAX_SESSIONS` limit of four static sessions and the shared 4,096-byte configuration buffer. A separate 512-byte receive packet buffer is static.

Each session holds a generation token, physical device and host claim tokens, interface/alternate and endpoints, channel number, receive packet size, `bcdDevice`, chip classification, and up to 510 bytes of pending payload retained after stripping the two FTDI status bytes.

Generation tokens are monotonically incremented and open fails when the 64-bit generation counter has reached `UINT64_MAX`.

## Open/reset flow and ownership

`open` obtains configuration metadata from the host, checks FTDI VID/PID, parses the single interface, reads/classifies the device descriptor, and claims that interface/alternate. After a successful claim it issues bounded 1,000 ms vendor OUT requests in this order:

1. reset SIO (`request 0`, value 0)
2. purge RX (`request 0`, value 1)
3. purge TX (`request 0`, value 2)
4. disable flow control (`request 2`, value 0)

Any vendor-control failure releases the newly acquired host claim and returns no session. A session token is published only after all initialization operations succeed.

## Baud and framing configuration

`configure` accepts 5 through 8 data bits, parity values 0 through 4, and one or two stop bits. The source rejects baud below 300. BM/R and FT-X encoding is capped at 3,000,000 baud; FT232H uses the high-speed encoding for rates at or above 1,200 and is capped at 12,000,000 baud.

The BM divisor derives from 48 MHz using the source's 1/8-fraction mapping. The high-speed path derives from the source's 120 MHz expression and marks the encoded divisor with `0x00020000`. Channel-aware devices pack the divisor upper bits and channel into `wIndex`.

The provider first sends FTDI `SET_DATA` (`request 4`) with data-bit/parity/stop-bit fields, then `SET_BAUD` (`request 3`). Either failing control transfer makes configuration fail.

## DTR/RTS control

`control_lines` sends vendor request 1 with DTR/RTS mask bits always present and the low state bits set according to requested DTR and RTS. The current session's channel is used as `wIndex`. The operation uses the fixed 1,000 ms control timeout.

## Read behavior and FTDI status stripping

Each FTDI bulk IN packet begins with two modem/line-status bytes. `read` therefore asks the host for exactly one USB packet using the interface's recorded IN maximum packet size, strips the first two bytes, and returns only payload to the caller.

If the caller buffer cannot hold that packet payload, the remainder is copied into the session's 510-byte pending buffer. Later calls drain pending payload before issuing another host read. A status-only two-byte packet returns zero payload. Shorter-than-two-byte or oversized host results fail unless payload had already been drained during the same call.

Read requires a valid session, non-null/nonzero caller buffer, capacity at most 4,096, valid packet size from 2 through 512, and nonzero timeout.

## Write behavior

`write` forwards data unchanged to the session's bulk OUT endpoint through the host claim. It requires a valid session, non-null source, nonzero length at most 4,096, and nonzero timeout. Negative host results or a host result larger than the requested length become failure `-1`; otherwise the host byte count is returned.

## Close/quiesce/stop

`close` first requests DTR and RTS both low using vendor request 1. If that teardown control operation fails, the source intentionally retains the host claim and session so cleanup uncertainty is visible. On success it releases the host claim and zeroes the session.

`quiesce` is true only when no static session token is live. `stop` clears the host dependency only when quiescent; it does not force-close active sessions.

## Concurrency and lifecycle assumptions

All mutable state is file-static and there are no locks. The source therefore relies on the provider/runtime serialized execution model; it does not establish independently thread-safe concurrent calls.

## Host validation

The exact upstream fixture builds the provider as a host shared library and validates:

- non-FTDI rejection and fail-closed multi-port rejection
- unsupported legacy `bcdDevice` rejection before claim
- FT232R reset/purge/flow-control initialization
- 115200 baud BM divisor and line framing
- DTR/RTS vendor request encoding
- stripping the two receive status bytes and retaining overflow payload without an extra USB read
- status-only packet behavior and bulk write routing
- session close, stale token rejection, and quiescence
- FT-X channel-index packing and framing
- FT232H 12 MBaud high-speed divisor packing and upper baud rejection
- failed line teardown retaining claim/quiescence state until a successful retry

This is provider-level simulation, not physical FTDI hardware validation.

## Standalone Xtensa validation

The build script validates the exact manifest, emits a 32-bit little-endian Xtensa ET_DYN shared ELF, normalizes relocations, requires `t5_driver_get` as the only defined global function export, and requires exactly `memcpy` and `memset` as unresolved runtime imports. CI run `36309926391` produced 8,048 bytes with SHA-256 `cf5c2f2777df96df1bf44ec719291ded008b4a3b833c8668a203e14e1a56516e`, exactly matching the published canonical ELF.

## Published package metadata

- `.package.json`: 618 bytes, SHA-256 `e76a9c3b5ba8a1f546fec536b48287eac238a2483410f91113ec2d1446a15665`
- `driver.elf`: 8,048 bytes, SHA-256 `cf5c2f2777df96df1bf44ec719291ded008b4a3b833c8668a203e14e1a56516e`
- `provider-abi.v1`: 40 bytes, SHA-256 `892780d027085bbba39fce0d795850388a019cb47ec5a4497a5249a01e1a2f60`
- `privileged-imports.v1`: 14 bytes, SHA-256 `a46fff766cb063bc348b3e7a6670e4a48c5c9201f0da92e73a67670229be7cca`

## Established limitations

The current API/source deliberately does not bind multi-port FTDI adapters because no port selector exists in `serial.port@1`. Hardware support is limited to the VID/PID and `bcdDevice` combinations coded above. Flow control is disabled on open and no flow-control configuration API is exposed. Receive modem/line status is stripped but not surfaced to callers. Physical USB and device lifetime remain owned by `usb.host@1`.

# usb-ch34x-v2

## Purpose and scope

`usb-ch34x-v2` is an ABI-v2 `serial.port@1` provider for supported WCH-compatible CH34x USB-to-serial devices. The driver contains CH34x-specific VID/PID matching, vendor initialization and line-control requests, baud-rate divisor calculation, framing configuration, and bulk byte-stream routing. It depends on the separately installed `usb.host@1` provider for configuration-descriptor access, physical interface claims, control transfers, bulk transfers, and release.

It does not own the USB controller, enumerate devices independently, or expose host-controller objects to consumers.

## Package identity and release state

- Driver ID: `usb-ch34x-v2`
- Version: `0.1.0`
- Driver ABI: `2`
- Architecture: `xtensa-esp32s3`
- Source path: `Drivers/usb_ch34x_v2`
- Upstream source-tree SHA: `6b2dc3c8e1f685d6fdaba58d04a763378f524b1b`
- Upstream `driver.c` blob: `286651a6f2dfdd78c7816f31e58ea54d121d78e1`
- Upstream manifest blob: `54e57872d412ba9db4318efda59d1567351dd8aa`
- Manifest status string: `experimental-unpublished`
- Requires: `usb.host@1`
- Provides: `serial.port@1`
- Published tag: `driver-usb-ch34x-v2-v0.1.0`
- Canonical ELF: 6,340 bytes
- Canonical ELF SHA-256: `8d88d227ac61116116b0ffb7c0e4052b7b547a0a19edc13a13af08792b95d621`

The source manifest's literal status string is preserved as implementation metadata. Separately, the inspected upstream release index publishes version 0.1.0 and its canonical package artifacts.

## Source, ABI, build, and validation files

- `Drivers/usb_ch34x_v2/driver.c` — exact upstream implementation.
- `Drivers/usb_ch34x_v2/manifest.json` — exact upstream package manifest.
- `sdk/driver/RiscUsbProviderV1.h` — already-shared `usb.host@1` and `serial.port@1` ABI used by the driver.
- `scripts/build_usb_ch34x_v2.py` — independent Xtensa ELF/export/import and release-metadata validator.
- `test/drivers/usb_ch34x_v2_test.c` — host fixture for matching, descriptor binding, vendor control requests, bulk routing, and lifecycle.

## Exported provider and dependency binding

The intended public ELF function is `t5_driver_get(uint32_t abi)`. It returns the static provider descriptor only for `RISC_PROVIDER_DRIVER_ABI_V2`.

The driver descriptor advertises:

- driver ID `usb-ch34x-v2`;
- capability ID `serial.port`;
- capability API `RISC_USB_CDC_API_V1` (`1`);
- `start`, `stop`, and `quiesce` lifecycle callbacks.

`start` accepts exactly one dependency with capability ID `usb.host`, API version 1, and a non-null API pointer. It validates the dependency table's own API version and structure size plus all six required operations: `configuration`, `claim`, `release`, `control`, `bulk_read`, and `bulk_write`. Startup also refuses a second bind while a host is already installed.

## Supported USB identities

The source accepts exactly these VID/PID pairs:

| VID | PID |
| --- | --- |
| `0x1a86` | `0x5523` |
| `0x1a86` | `0x7522` |
| `0x1a86` | `0x7523` |
| `0x4348` | `0x5523` |
| `0x2184` | `0x0057` |
| `0x9986` | `0x7523` |

Any other identity is rejected before an interface claim is acquired.

## Static state, capacity, and token lifecycle

The provider allocates no dynamic memory. It keeps the bound `usb.host@1` API pointer, four `ch_session` slots because `RISC_USB_CDC_MAX_SESSIONS` is 4, one 4,096-byte configuration buffer, and a monotonically increasing 64-bit sequence token.

Each session stores the provider token, physical device token, host claim token, interface number, selected alternate, bulk IN endpoint, bulk OUT endpoint, and one-byte CH34x version value. `open` refuses to allocate a new session after `sequence == UINT64_MAX`; token generation does not wrap.

The source uses file-static mutable state and contains no internal locking. It does not establish concurrent/thread-safe entry semantics.

## Configuration parsing and interface matching

`open(device)` first requests the complete USB configuration plus VID/PID from `usb.host@1`. The parser requires a valid configuration descriptor, total length no greater than the 4,096-byte buffer, configuration descriptor type 2, `wTotalLength` equal to the supplied byte count, and every nested descriptor to remain in bounds with `bLength >= 2`.

Only class `0xff` interfaces are candidates. A candidate interface/alternate requires exactly one valid bulk IN and one valid bulk OUT endpoint. Accepted endpoints must have descriptor length at least 7, nonzero maximum packet size no greater than 512, nonzero endpoint number, and no reserved endpoint-address bits. Duplicate IN or OUT endpoints fail immediately. The entire configuration must expose exactly one matching vendor-class interface/alternate, so ambiguous multiport configurations fail closed.

## Physical claim and initialization sequence

Once a single candidate is found, `open` claims that interface and alternate through `usb.host@1`. After a successful claim the driver performs two 1,000 ms control transfers:

1. vendor IN `0xc0/0x5f`, zero value/index, two-byte payload;
2. vendor OUT `0x40/0xa1`, zero value/index and zero payload.

The first must return exactly two bytes and the second zero. Failure releases the acquired claim. The first version byte is retained in the session. The provider publishes a token only after initialization succeeds.

## Baud-rate configuration

`configure` accepts baud 300 through 3,000,000, 5 through 8 data bits, parity 0 through 4, and one or two stop bits.

The divisor implementation uses a 48 MHz clock, searches prescale values, rounds adjacent divisors, folds even divisors where factor 1 can be removed, and packs the encoded divisor into a 16-bit value. Device versions greater than `0x27` add bit `0x0080`. The source deliberately remains in 32-bit arithmetic to avoid unsupported libgcc 64-bit divide relocations.

The encoded baud value is sent by vendor OUT request `0x9a` with `wValue=0x1312` and the divisor in `wIndex`.

## Framing configuration

For CH34x version values below `0x30`, only 8-N-1 is accepted. Version `0x30` or newer constructs an LCR byte and sends `0x9a` with `wValue=0x2518` and the LCR in `wIndex`.

The LCR starts at `0xc0 + (data_bits - 5)`; odd/even/mark/space parity add `0x08/0x18/0x28/0x38`, and two stop bits add `0x04`.

## DTR and RTS

`control_lines` uses vendor OUT request `0xa4`. RTS contributes `0x40`, DTR contributes `0x20`, and the combined 16-bit value is inverted before transmission as `wValue`. For DTR and RTS both asserted, the encoded value is `0xff9f`.

## Bulk I/O

`read` uses the session host claim and bulk IN endpoint; `write` uses the bulk OUT endpoint. Both reject invalid sessions, null buffers, zero transfers, and transfers larger than 4,096 bytes. Caller timeout passes through unchanged. A nonnegative host return greater than requested capacity is converted to `-1`.

## Close, stop, and quiescence

`close` releases the host-owned physical claim and zeroes the entire session. `quiesce` is false while any session token remains live. `stop` clears the host pointer only when already quiescent and does not force-close sessions.

## Host validation

The exact upstream fixture validates provider identity/ABI, dependency binding, supported-device matching, rejection of unsupported VID before claim, one vendor-class interface claim, `0xc0/0x5f` version read, `0x40/0xa1` initialization, quiescence while open, 115200-8-N-1 and 9600-7-E-2 programming, active-low DTR/RTS request `0xa4`, bulk routing, invalid null I/O, release, stale-close rejection, and final quiescence.

This is deterministic provider-level simulation, not physical CH34x/controller validation.

## Standalone Xtensa build and release metadata

The standalone builder validates the exact package manifest, builds ELF32 little-endian Xtensa ET_DYN output, normalizes supported relocations, requires `t5_driver_get` as the only global function export, and rejects unresolved imports outside `memcpy`/`memset`. CI run `36310220488` passed the exact host protocol/lifecycle fixture and produced 6,340 bytes with SHA-256 `8d88d227ac61116116b0ffb7c0e4052b7b547a0a19edc13a13af08792b95d621`, exactly matching the published canonical ELF.

Observed upstream package metadata:

- `.package.json`: 622 bytes, SHA-256 `e96f413b514dbdb3407e9374e33149499d1178de4ee60ecb2b39a5698662ba8e`
- `driver.elf`: 6,340 bytes, SHA-256 `8d88d227ac61116116b0ffb7c0e4052b7b547a0a19edc13a13af08792b95d621`
- `provider-abi.v1`: 40 bytes, SHA-256 `892780d027085bbba39fce0d795850388a019cb47ec5a4497a5249a01e1a2f60`
- `privileged-imports.v1`: 14 bytes, SHA-256 `a46fff766cb063bc348b3e7a6670e4a48c5c9201f0da92e73a67670229be7cca`

## Established implementation limitations

The driver intentionally binds only a configuration with exactly one usable vendor-class serial interface/alternate and cannot select among multiple CH34x-style ports. The API exposes serial line configuration, DTR/RTS, and bulk byte streams. It does not expose modem-status polling, break signaling, flow-control configuration, interrupt notifications, or CH34x-specific diagnostics. Physical USB and device lifetime remain owned by `usb.host@1`.

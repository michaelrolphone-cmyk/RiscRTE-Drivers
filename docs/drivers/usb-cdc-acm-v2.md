# usb-cdc-acm-v2

## Purpose and scope

`usb-cdc-acm-v2` is an ABI-v2 `serial.port@1` provider that implements USB CDC ACM above the separate `usb.host@1` provider. It parses a device's complete USB configuration, identifies one unambiguous ACM control/data function, claims the required interfaces through the host capability, issues CDC class requests, and forwards bulk reads and writes through host-owned interface claims. It does not own the USB controller or perform physical USB transfers directly.

## Package identity

- Driver ID: `usb-cdc-acm-v2`
- Version: `0.1.0`
- Driver ABI: 2
- Architecture: `xtensa-esp32s3`
- Source path: `Drivers/usb_cdc_v2`
- Upstream source tree SHA: `d283ed6c4993244c5e49a4c58037784b3c12f08a`
- Manifest status string: `experimental-unpublished`
- Requires: `usb.host@1`
- Provides: `serial.port@1`
- Release tag: `driver-usb-cdc-acm-v2-v0.1.0`
- Canonical ELF: 6,584 bytes
- Canonical ELF SHA-256: `34b3eeea0cca3927517e849298a1f088cbd1a14651bf81ff2b74f655a78af56f`

The source manifest's status string is recorded separately from observed release state: the inspected release index publishes version 0.1.0 despite the literal `experimental-unpublished` field.

## Source, ABI, build, and test files

- `Drivers/usb_cdc_v2/driver.c` — exact current upstream implementation
- `Drivers/usb_cdc_v2/manifest.json` — exact current upstream manifest
- `sdk/driver/RiscUsbProviderV1.h` — shared `usb.host@1` and `serial.port@1` ABI
- `scripts/build_usb_cdc_acm_v2.py` — standalone Xtensa build and release-parity validator
- `test/drivers/usb_cdc_v2_test.c` — exact current upstream host-capability lifecycle/I/O fixture

## Exported provider and dependency binding

The only intended public ELF function is `t5_driver_get(uint32_t abi)`. It returns the static `risc_driver_v2` descriptor only for ABI 2.

`start` accepts exactly one dependency whose capability ID is `usb.host`, API version is 1, and API pointer is non-null. It validates the dependency table's own API version, structure size, and the `configuration`, `claim`, `release`, `control`, `bulk_read`, and `bulk_write` callbacks. Startup fails if the provider is already bound, the dependency set is not exactly one matching host, or any required callback is absent.

## Static resources and ownership

The provider allocates no memory dynamically. `RISC_USB_CDC_MAX_SESSIONS` is 4 and the shared configuration-descriptor buffer is `RISC_USB_CONFIG_LIMIT` = 4,096 bytes. Each session stores a generated token, physical device token, control/data claim tokens, control/data interface numbers, selected data alternate, and one bulk IN plus one bulk OUT endpoint.

A live physical interface remains owned by this ELF after generic grants disappear; `quiesce` therefore refuses unload while any session token exists. The source uses file-static mutable state and no internal locks, so it does not establish thread-safe concurrent entry.

## Configuration parsing

`open(device)` rejects an unbound host, zero device, or lack of a free session slot. It obtains the full configuration descriptor into the 4,096-byte buffer. Parsing requires a standard 9-byte configuration descriptor of type 2, a self-consistent `wTotalLength`, and every contained descriptor to remain inside that length with `bLength >= 2`.

Exactly one alternate-0 communications interface of class 2/subclass 2 is accepted as the ACM control interface. CDC data interfaces are class 10. The parser supports a CDC Union Functional Descriptor identifying exactly one slave data interface. Multiple union descriptors or a union with more than one slave fail closed because `open(device)` has no function selector. Without a union, exactly one alternate-0 CDC data interface must exist.

If an Interface Association Descriptor covers the control interface, the selected data interface must also lie inside that IAD's range. A union target must actually exist as an alternate-0 class-10 interface.

## Data alternate and endpoint matching

The parser scans every alternate of the selected data interface and accepts an alternate only when it has one valid bulk IN endpoint and one valid bulk OUT endpoint. A valid endpoint descriptor has length at least 7, bulk transfer type, nonzero max packet size no greater than 512, nonzero endpoint number, and no reserved endpoint-address bits set. Duplicate IN or OUT endpoints reject the alternate. More than one equally valid alternate causes the whole bind to fail rather than selecting arbitrarily.

## Interface claims and unwind

After parsing, the provider claims the control interface at alternate 0, then the selected data interface/alternate. If the control claim fails or yields zero, open fails. If the data claim fails, the already-acquired control claim is released before returning failure. Only after both claims succeed does the provider increment its generation counter, force a nonzero session token, and install the candidate in the session table.

`close(session)` releases the data claim first when distinct, then the control claim, and clears the session. Unknown or stale tokens fail.

## Serial line coding and modem control

`configure` accepts baud 300 through 3,000,000, data bits 5 through 8, parity values 0 through 4, and stop bits exactly 1 or 2. It emits CDC ACM `SET_LINE_CODING` (`bmRequestType=0x21`, `bRequest=0x20`) to the control interface with a 7-byte payload and 1,000 ms timeout. Baud is little-endian; one stop bit encodes as 0 and two as 2. Success requires the host control transfer to return exactly 7.

`control_lines` emits `SET_CONTROL_LINE_STATE` (`0x21/0x22`) with DTR in bit 0 and RTS in bit 1 of `wValue`, zero payload, the control-interface index, and 1,000 ms timeout. Success requires return 0.

## Bulk I/O

`read` routes through the data claim and selected bulk IN endpoint. `write` uses the same claim and bulk OUT endpoint. Both require a valid session, non-null buffer, nonzero size, and no more than 4,096 bytes. The caller timeout is passed through unchanged to the host provider. Negative host returns remain failures; zero is not rewritten.

## Lifecycle

`quiesce` returns false while any session remains open. The generic loader is expected to check quiescence before unmapping.

`stop` is also capable of recovery/test teardown: when the host is bound it releases every remaining session claim and clears the host pointer. That behavior is explicitly exercised by the current upstream fixture, but normal lifecycle must still respect the quiescence contract before unloading the ELF.

## Current upstream host validation

The exact current upstream fixture dynamically loads a host-built copy of the driver with `dlopen` and resolves only `t5_driver_get`. Its synthetic configuration contains ACM control interface 0 and CDC data interface 1 with bulk IN endpoint `0x81` and bulk OUT endpoint `0x02`; it exercises the parser's no-Union fallback path.

The fixture verifies ABI rejection/acceptance, dependency binding, configuration-fetch failure, data-interface claim failure with control-claim unwind, session creation, line-coding validation and 115200-8-N-1 payload bytes, DTR+RTS control state, bulk read/write routing, explicit close and stale-token rejection, generation-safe reopen, quiescence behavior, direct stop releasing an active session, restart, and rejection of the stale pre-stop handle.

This fixture validates provider logic only; it does not establish behavior on a physical CDC device or USB controller.

## Standalone Xtensa build and release parity

The standalone builder validates the exact manifest, compiles a 32-bit little-endian Xtensa shared ELF, normalizes relocations, requires `t5_driver_get` as the sole exported function, and requires only `memcpy` and `memset` as unresolved runtime imports.

Published package metadata:

- `.package.json`: 624 bytes, SHA-256 `37bef861997b4e1e028ff1e98a1764f99c87df99dcacaf83f529c9382803481`
- `driver.elf`: 6,584 bytes, SHA-256 `34b3eeea0cca3927517e849298a1f088cbd1a14651bf81ff2b74f655a78af56f`
- `provider-abi.v1`: 40 bytes, SHA-256 `892780d027085bbba39fce0d795850388a019cb47ec5a4497a5249a01e1a2f60`
- `privileged-imports.v1`: 14 bytes, SHA-256 `a46fff766cb063bc348b3e7a6670e4a48c5c9201f0da92e73a67670229be7cca`

The builder records whether independently produced bytes exactly equal the canonical release.

## Established limitations

The provider supports one unambiguous ACM function per `open(device)` because the API has no function-selector argument. Multi-slave unions, multiple ACM control functions, and multiple equally valid data alternates fail closed. The implementation exposes line coding, DTR/RTS, and bulk byte streams; it does not expose CDC notifications, break signaling, line-coding reads, or other CDC subclasses. USB controller ownership and physical transfers remain the responsibility of `usb.host@1`.

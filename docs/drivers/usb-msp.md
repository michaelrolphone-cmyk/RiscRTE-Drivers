# usb-msp

## Identity and package

`usb-msp` is the RiscRTE USB transport provider for TI MSP-FET and eZ-FET probes. The migrated package is version **0.1.1**, driver ABI **2**, architecture **xtensa-esp32s3**, and executable `driver.elf`. The driver root advertises `debug.vendor.msp@1` and requires `usb.host@1` plus `platform.clock@1`.

The upstream manifest still labels the source package `experimental-unpublished`, while the upstream release index contains the published `driver-usb-msp-v0.1.1` package. Its canonical ELF is **14,116 bytes**, SHA-256 `d0f48fdd2a4da3afd41d74960017bd10a0e49cb7396cf7ce4a9ca3a5bb397ede`. Observed package files are `.package.json` (662 bytes, SHA-256 `768afcdf3e7f084a50dd4771b8e23c81f576b058e0598162b81a32d013591732`), `driver.elf` (14,116 bytes, hash above), `provider-abi.v1` (45 bytes, SHA-256 `04b1bf96e25a247b60764fd47a614d17bb8b5a681c46a59a85d19321f4b11789`), and `privileged-imports.v1` (22 bytes, SHA-256 `1a06e116d0a87beb043f45441b7671526c56c678fa69cbb7823caecb1dc09393`).

## Source and build inputs

The migration is derived from `Drivers/usb_msp/driver.c`, `Drivers/usb_msp/manifest.json`, `sdk/driver/RiscMspFetV1.h`, `sdk/driver/RiscPlatformClockV1.h`, `sdk/driver/RiscUsbControllerV1.h`, and `test/drivers/usb_msp_test.c`. The three required ABI headers already present in this repository match upstream byte-for-byte.

`scripts/build_usb_msp.py` performs the standalone Xtensa build and validates ELF32 little-endian Xtensa `ET_DYN`, the sole public function symbol `t5_driver_get`, and unresolved imports `memcpy`, `memmove`, and `memset`. It also checks the canonical size and SHA-256 above.

## Exported root and capability interface

The only public root symbol is `const risc_driver_v2 *t5_driver_get(uint32_t abi)`. It returns the driver only for `RISC_PROVIDER_DRIVER_ABI_V2`. The static root provides driver ID `usb-msp`, capability `debug.vendor.msp`, capability API 1, and `start`, `stop`, and `quiesce` lifecycle callbacks.

The `risc_msp_fet_api_v1` interface exposes bounded discovery polling, probe snapshots, open, interface selection, TI HAL execution, and close. The ABI limits the implementation to four probes, request payloads of at most 250 bytes, and response payloads of at most 4096 bytes. Probe snapshots report device token, VID/PID, CDC control/data interface numbers, bulk endpoints, probe variant, JTAG/SBW support, and whether a second CDC function is present.

## Matching and discovery

The implementation accepts TI VID `0x2047` with application-mode PIDs `0x0013` (eZ-FET lite) and `0x0014` (MSP-FET). Other identities are rejected. The upstream fixture specifically confirms that recovery PID `0x0203` is not surfaced as a probe.

Discovery is performed through `usb.host@1`. Configuration descriptors are parsed within `RISC_USB_CONFIG_LIMIT`. The provider identifies the first CDC ACM control function, follows a CDC union descriptor when present, and selects the corresponding CDC data interface. A usable data interface needs one bulk-IN and one bulk-OUT endpoint with a nonzero packet size no greater than 512 bytes. Duplicate endpoints or malformed endpoint encodings are rejected.

Discovery state is statically bounded by `RISC_USB_HOST_MAX_DEVICES` and four probe slots. `poll` accepts 1 through 16 events per call. A device is attempted at most eight times within a 10,000 ms discovery window. Retry delay starts at 100 ms, doubles, and is capped at 2,000 ms. A valid probe waits for a free probe slot when all four slots are occupied. Detach clears discovery state and releases claims held by an active session for that device.

## USB transport

The provider does not own the USB controller. Descriptor reads, interface claims, control transfers, and bulk I/O are all routed through the bound `usb.host@1` interface.

Opening requires a discovered device, an unused session slot, and either JTAG or SBW mode. A second session for the same device is rejected. The selected CDC function is configured for **460800 baud, 8 data bits, no parity, one stop bit** using `SET_LINE_CODING`, followed by `SET_CONTROL_LINE_STATE` with DTR/RTS clear. Before protocol synchronization the implementation drains at most four 64-byte reads with 10 ms read timeouts.

The control and data interfaces are independently claimed when they differ. Any open-time claim or CDC setup failure releases claims already obtained.

## TI HAL framing and time bounds

Implemented frame types are execute `0x81`, ACK `0x91`, exception/reset `0x92`, and data `0x93`. Implemented function IDs include version `0x00`, start-JTAG `0x04`, stop-JTAG `0x06`, and reset-static-globals `0x52`. Frames include length, type, reference, reserved byte, payload, padding when required, and the two-byte XOR integrity trailer checked by the receive path.

Each session owns a 512-byte receive buffer. The execution API rejects requests above 250 bytes, output capacities above 4096 bytes, zero timeouts, and timeouts above **5000 ms**. Bulk operations use waits of at most **100 ms**, read/write loops are bounded to **64** steps, and one command accepts at most **32** response frames. Exception replies return `-2`; transport, framing, stale-reference, size, and timeout failures return a negative result.

The implementation parses both supported version layouts and adjusts later function IDs for protocol revisions below 3.0.

## Session and provider lifecycle

Open creates a nonzero session token, resets the communications channel, reads the probe protocol version, resets static HAL state, and starts the requested target interface. JTAG uses protocol byte zero and SBW uses byte one. Changing modes stops the current target interface before starting the requested one.

Close is fail-closed: target stop and communication reset must succeed before USB claims and session state are released. If either operation fails, the session remains active and `quiesce` remains false. This prevents the provider from being considered unloadable while an active session still owns USB state.

`start` requires exactly one `usb.host@1` and one `platform.clock@1` dependency. The host object must expose configuration, claim/release, control, bulk read/write, poll, and device snapshot callbacks; the clock must expose `monotonic_ms`. `stop` only clears provider state when no session is active. All provider state is statically allocated. The source contains no lock, mutex, or atomic coordination, so concurrent-call safety is **not established** by the implementation.

## Failure behavior and established limitations

Malformed descriptors, dependency mismatch, invalid mode or token, clock failure/overflow, claim/control failure, bulk I/O failure, malformed or stale protocol frames, response overflow, and failed shutdown all fail closed. Unknown hardware identities are not treated as compatible.

The implementation supplies transport and target-interface selection only. It does not establish MSP430 device-family algorithms, target-memory semantics, flash/FRAM programming policy, register operations, breakpoint policy, or recovery-mode support. Those behaviors are not claimed here.

## Tests

The exact upstream `test/drivers/usb_msp_test.c` fixture validates ABI identity, dependency admission, recovery-PID rejection, both supported probe PIDs, selection of the first CDC function while leaving the backchannel unclaimed, 460800-8N1 setup, protocol reset/version exchange, SBW and JTAG transitions, HAL request/response framing, non-quiescence during an active session, close-time claim release, stale-session rejection, and detach cleanup.

Its success string is `MSP-FET/eZ-FET discovery, HAL framing, JTAG, SBW, detach and quiescence: PASS`.

Canonical ELF parity remains a CI gate until the integrated standalone Xtensa build reproduces the 14,116-byte published artifact.

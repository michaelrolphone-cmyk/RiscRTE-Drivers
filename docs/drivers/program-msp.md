# program-msp

## Purpose and scope

`program-msp` is the ABI-v2 target-programming provider layered above the vendor-neutral runtime dependency `debug.vendor.msp@1`. It implements the first `program.msp@1` API for MSP430FR/XV2 devices reached through an MSP-FET or eZ-FET transport provider. Its source explicitly limits programming to addressed FRAM writes; classic MSP flash programming is not implemented.

## Package identity and publication

- Driver/package ID: `program-msp`
- Version: `0.1.0`
- Driver ABI: `2`
- Architecture: `xtensa-esp32s3`
- Executable: `driver.elf`
- Requires: `debug.vendor.msp@1`
- Provides: `program.msp@1`
- Source manifest status string: `experimental-unpublished`
- Source path: `Drivers/program_msp`
- Upstream source-tree SHA: `fe046f4c538efbd003ddd176b4d4b44b5d3de281`
- Upstream release tag: `driver-program-msp-v0.1.0`
- Canonical released ELF: 9,128 bytes
- Canonical released ELF SHA-256: `19b0999f41e45522e877097addf3cfd55651b2fd00ae925fa6084b769b66527f`

The upstream release index also declares a 629-byte `.package.json`, a 40-byte `provider-abi.v1`, and a 21-byte `privileged-imports.v1`. Publication state and the source manifest's `experimental-unpublished` status string are recorded separately rather than rewriting upstream metadata.

## Migrated implementation and interfaces

The migration carries:

- `Drivers/program_msp/driver.c`
- `Drivers/program_msp/manifest.json`
- `sdk/driver/RiscProgramMspV1.h`
- `sdk/driver/RiscMspFetV1.h`
- the already-shared `RiscProviderV2.h`, `RiscUsbControllerV1.h`, and USB provider types required by the dependency ABI
- `test/drivers/program_msp_test.c`
- `scripts/build_program_msp.py`

The source, manifest, ABI headers, and host behavioral fixture are exact upstream content. The destination build script reproduces the upstream standalone Xtensa compile/link command and verifies the published ELF bytes.

## Exported provider descriptor and dependency binding

The ELF exports only `t5_driver_get`. It returns its static `risc_driver_v2` descriptor only for provider ABI 2.

`start` accepts exactly one dependency. It requires:

- capability ID `debug.vendor.msp`;
- API version 1 in the dependency record;
- a non-null API pointer;
- an MSP API whose own `api_version` is 1;
- `struct_size >= sizeof(risc_msp_fet_api_v1)`;
- non-null `poll`, `snapshot`, `open`, `execute`, and `close` callbacks.

The MSP transport ABI also contains `set_interface`, but this provider neither calls that callback nor requires it to be non-null.

A second `start` while the dependency pointer is already bound is rejected.

## MSP-FET transport contract used by this provider

The dependency exposes incremental probe discovery and MSP-FET HAL execution. Constants in `RiscMspFetV1.h` establish:

- maximum probe snapshot: 4;
- maximum transport request: 250 bytes;
- maximum response: 4,096 bytes;
- interface modes: none, JTAG, or Spy-Bi-Wire;
- probe variants: unknown, eZ-FET Lite, or MSP-FET.

This provider asks `poll` to process at most eight events before every target-open attempt, then calls `snapshot` into a fixed four-entry local array.

## Probe selection and interface selection

`open` accepts a device token and an interface mode from the `program.msp@1` API.

A device value of zero means “select the only discovered probe.” If more than one probe matches that implicit selection, opening fails with an ambiguity error. A nonzero device value selects the matching probe token explicitly.

The public programming API accepts AUTO, JTAG, and SBW. AUTO tries SBW first and then JTAG. An explicit JTAG or SBW request tries only that mode. Values above SBW are rejected.

The implementation has two fixed `program_session` slots; no dynamic allocation is used. If both slots have nonzero tokens, opening another target fails.

## MSP-FET protocol negotiation

After opening the transport, the provider executes HAL function ID `0x00` (Version) with a one-byte zero request.

`parse_protocol` recognizes two response layouts:

- responses of at least 40 bytes interpret the low 16 bits of the first little-endian 32-bit software-version word, deriving major as `(sw >> 14) + 1` and minor as `(sw >> 8) & 0x3f`;
- responses from 8 through 39 bytes derive major from the top two bits of `version[1]` and minor from its low six bits.

Shorter or missing replies are rejected.

For negotiated protocols below 3.0, HAL function IDs greater than `0x11` are mapped down by one before being passed to the transport. Protocol 3.x and newer use the source function IDs unchanged.

## Target identification and supported XV2 devices

The provider obtains the target JTAG ID with HAL function `0x0c`. Only IDs `0x91`, `0x95`, and `0x99` are accepted. Any other ID closes the MSP transport and reports that the target is not an XV2 device supported by this FRAM programmer.

The returned `risc_program_msp_target_v1` describes the selected probe device, probe variant, opened interface mode, JTAG ID, and negotiated protocol major/minor.

## Target configuration

Before programming, the provider issues MSP-FET Configure function `0x07` for these parameters:

- clock-control type `0x0a = 0`;
- SFLLDEH `0x0c = 0`;
- default clock control `0x03 = 0x040f`;
- enhanced PSA `0x01 = 0`;
- PSA TCKL high `0x02 = 0`;
- power test-register mask `0x04 = 0`;
- power test-register 3V mask `0x07 = 0`;
- alternate ROM address for CPU read `0x0e = 0`.

Those eight configuration writes are required. The `NO_BSL` parameter `0x0d = 0` is attempted but treated as optional: a failure of that one request does not fail target configuration.

Configuration requests use a 1,000 ms timeout.

## JTAG fuse check

HAL function `0x4c` checks whether the target JTAG fuse is blown. Transport failure rejects the target. A reply of at least two bytes beginning `0x55, 0x55` is treated as a blown fuse and rejected.

## POR synchronization and saved target context

`sync_target` supports only the three accepted XV2 JTAG IDs. It sends HAL function `0x3a` with a 21-byte request containing:

- WDTCTL address `0x015c`;
- WDTHOLD `0x80`;
- WDTPW `0x5a`;
- target JTAG ID;
- the source's fixed control fields including `request[15] = 40`.

A response shorter than eight bytes fails synchronization. On success, the provider saves the returned watchdog byte, 32-bit PC, and 16-bit SR into the session and marks it synchronized.

This operation uses a 1,500 ms timeout.

## FRAM write API and bounds

`RISC_PROGRAM_MSP_MAX_CHUNK` is 128 bytes. Public write requests must have:

- a valid session;
- non-null data;
- nonzero length;
- length at most 128;
- an address no greater than `0x000fffff`;
- an address-plus-length that remains within the first 1 MiB address space.

The MSP-FET word operations require even addresses and lengths. For an odd first or last byte, the provider expands the operation to an even-aligned range and performs a read-modify-write so bytes outside the caller's requested range are preserved. The temporary stack buffer is 130 bytes, allowing at most the 128-byte public chunk plus two edge bytes.

Aligned writes avoid the preliminary read and copy the supplied data directly into the word buffer.

## MSP-FET memory operations

Reads use HAL function `0x3d` with an eight-byte little-endian request containing the 32-bit byte address and a 32-bit word count. The response length must equal the requested byte count exactly. Timeout: 1,500 ms.

Writes use HAL function `0x4e` (Write FRAM Quick XV2). Its request is the same eight-byte address/word-count prefix followed by the even-length data. Timeout: 2,500 ms. Any negative transport result fails the write and reports that classic MSP flash is not supported.

The provider's private helpers admit up to 130 bytes after alignment. They reject odd word-operation addresses, odd word-operation lengths, null buffers, or zero lengths.

## Verification behavior

`verify` validates the same public address/chunk bounds as `write`, expands to the same even-aligned range, reads those words, then compares only the originally requested byte range.

Success returns the requested byte count. A transport/read failure returns `-1`. A readback mismatch returns `-2` and records the error string “MSP FRAM readback differs from firmware image”.

## Session close and target release

Closing a programming session intentionally re-runs target synchronization, then executes HAL function `0x3c` to restore/release target context. That request includes the saved watchdog value, PC, SR, and the fixed release control bytes used by the source.

Only after target release succeeds does the provider call the dependency's `close` callback to stop JTAG/SBW and release the probe transport. If all three steps succeed, the whole session structure is zeroed.

A failure before that point retains the session token, so `quiesce` continues to fail and the runtime cannot safely unload the provider.

## Lifecycle and quiescence

`quiesce` returns false whenever either of the two session slots has a nonzero token. It returns true only when no programming sessions remain.

`stop` does nothing while quiescence fails. When there are no sessions, it clears the MSP dependency pointer and the shared error buffer.

The source maintains global session slots, a global monotonically increasing serial counter, and a global error buffer without an internal lock. No source-level guarantee of concurrent calls from multiple tasks is established; documentation therefore does not claim thread safety.

## Error reporting

`last_error` copies from a provider-global 160-byte bounded error buffer and always NUL-terminates the caller buffer when capacity is nonzero. It returns false for a null/zero-capacity destination or when no current error text exists.

Public `open`, `write`, and `verify` clear the error buffer at their start. Source-defined messages distinguish discovery, ambiguity, session exhaustion, transport/version/JTAG/configuration/fuse/synchronization failures, invalid FRAM requests, read/write failures, verification mismatch, and close/quiescence failure.

## Host behavioral test

The migrated upstream fixture builds the provider as a host shared object and supplies an in-memory MSP-FET mock. It verifies:

- ABI 2 lookup and ABI 1 rejection;
- driver/capability identity;
- correct binding to `debug.vendor.msp@1`;
- automatic SBW-first interface selection;
- protocol 3.8 parsing;
- accepted JTAG ID `0x99`;
- required target configuration and POR synchronization;
- quiescence refusal with an open programming session;
- odd-address three-byte FRAM write with preservation of adjacent bytes;
- successful readback verification;
- mismatch return value `-2` and diagnostic text;
- synchronization/release on close and transport closure;
- ambiguous default probe rejection before hardware open;
- explicit JTAG open followed by rejection/closure of an unsupported `0x89` target;
- sole dynamic export `t5_driver_get`.

The fixture models the MSP-FET dependency and a 256-byte memory window. It does not prove behavior against a physical MSP-FET/eZ-FET or an actual MSP430FR target.

## Standalone Xtensa build and canonical parity

`scripts/build_program_msp.py` reproduces the upstream standalone compile/link command with the ESP32-S3 Xtensa toolchain. It checks the exact manifest, requires `t5_driver_get` as the only exported function, requires the only unresolved runtime imports to be `memcmp`, `memcpy`, and `memset`, verifies a 32-bit little-endian Xtensa shared ELF, and requires exact equality with the published 9,128-byte canonical ELF SHA-256 `19b0999f41e45522e877097addf3cfd55651b2fd00ae925fa6084b769b66527f`.

Canonical byte parity is therefore a build failure condition for this migrated driver rather than a documentation-only observation.

## Established limitations

The implementation is an MSP430FR/XV2 FRAM programmer, not a general MSP430 flash algorithm. Only JTAG IDs 0x91, 0x95, and 0x99 are accepted. Public write/verify calls are bounded to 128 bytes and the first 1 MiB address space. Physical probe/target validation is not established by the host fixture; the repository records only the behavior demonstrated by source, ABI, tests, and canonical package metadata.

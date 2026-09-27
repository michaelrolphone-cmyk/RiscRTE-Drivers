# usb-stlink

## Purpose and package identity

`usb-stlink` is the ABI-v2 provider for `debug.vendor.stlink@1`, implementing the ST-LINK V2/V2.1/V3 raw USB debug-probe transport above `usb.host@1` and `platform.clock@1`. It does not implement STM32/STM8 target algorithms; those belong above this capability.

Verified package metadata from current upstream:
- ID/version: `usb-stlink` 0.1.0
- architecture: `xtensa-esp32s3`; driver ABI 2
- source: `Drivers/usb_stlink/driver.c`
- source tree: `dc392f6d1125f8a55de6660ae19e281e4f27098c`
- driver blob: `21458e04097b89d11c8b60aec1025b8d61352191`
- manifest blob: `1cc2df051ea3ff32d8b25be1627dd9bb6123c5ee`
- host fixture blob: `340e0b6dd362fd42c39a0b0ccb531d80b7d0e1d0`
- `RiscStlinkV1.h` blob: `6540af993431b2bf39a088dd96029e8659a8279e`
- manifest requires: `usb.host@1`, `platform.clock@1`
- manifest provides: `debug.vendor.stlink@1`
- source status: `experimental-unpublished`

Observed release `driver-usb-stlink-v0.1.0` publishes a 9,304-byte `driver.elf` with SHA-256 `21500939b991239598ae57a97667c5004e8bccf988a1052d350eca427b5af3be`. Package metadata also establishes `.package.json` 664 bytes / `d9ac2e4e5d0e106825e55a32b8a72a030a551079cd6e50e3b90df157b5636ce6`, `provider-abi.v1` 48 bytes / `f63530c5bc8135503bf1871e91449522cd35fed8c039f5869430914b7dd0a9bc`, and `privileged-imports.v1` 14 bytes / `a46fff766cb063bc348b3e7a6670e4a48c5c9201f0da92e73a67670229be7cca`.

## Files, export, and ABI

Migrated files are the exact upstream `driver.c`, exact manifest, exact host fixture `test/drivers/usb_stlink_test.c`, and exact `sdk/driver/RiscStlinkV1.h`. The existing `RiscPlatformClockV1.h` already matches upstream blob `33a5b27dcea7e30698d0177ad5db065166082194`. `scripts/build_usb_stlink.py` independently builds and validates the ELF.

The intended sole exported function is `t5_driver_get(uint32_t abi)`; it returns the static `risc_driver_v2` only for ABI 2. The provided `risc_stlink_api_v1` exposes:
- `poll` for bounded discovery;
- `snapshot` for probe metadata;
- `open` for an exclusive probe session and transport entry;
- `set_transport` for SWD/SWIM switching;
- `command` for raw 16-byte ST-LINK command framing plus one optional data phase;
- `close` for mode exit and claim release.

The ABI fixes four maximum probes, four exclusive sessions, 16-byte command packets, and raw data phases of at most 6,144 bytes.

## Dependency binding and lifecycle

`start` requires exactly one `usb.host@1` and one `platform.clock@1` dependency, in either order. It rejects missing, duplicate, unknown, undersized, or incomplete dependencies and rejects a second start. The host API must provide configuration, claim/release, bulk read/write, discovery poll, and device snapshot callbacks. The clock API must provide `monotonic_ms`.

No dynamic allocation or internal locking is used. State is kept in file-static probe, inspection, and session arrays. The implementation therefore depends on the serialized provider-executor model rather than providing independent multi-thread synchronization.

`quiesce` is false while any session is open. `stop` refuses to clear dependencies while non-quiescent. After quiescence it clears host/clock pointers and discovery state but intentionally does not reset session-token generation, avoiding stale-token reuse after restart.

## USB identification and descriptor matching

Only STMicroelectronics VID `0x0483` is accepted. Supported PIDs are:
- `0x3748`: ST-LINK V2;
- `0x374b`, `0x3752`: V2.1;
- `0x374d`, `0x374e`, `0x374f`, `0x3753`, `0x3754`, `0x3755`, `0x3757`: V3 family.

ST-LINK/V1 PID `0x3744` is intentionally rejected because the implementation identifies it as SCSI transport, not the V2/V2.1/V3 raw-bulk protocol.

The configuration parser requires a structurally valid complete configuration descriptor whose reported total length exactly matches the buffer and is within `RISC_USB_CONFIG_LIMIT`. It selects exactly one vendor-class interface with bulk endpoint 1 IN and bulk endpoint 1 or 2 OUT, with packet sizes from 1 through 512 bytes. Multiple qualifying interfaces fail closed. V2.1/V3 SWO endpoint 2 IN is intentionally ignored; endpoint 1 IN remains the command-response path.

## Discovery, retry, and snapshot behavior

`poll` accepts work bounds from 1 through 16 and delegates bounded work to the host provider. It snapshots generation-qualified USB device tokens, removes state for detached generations, and tracks an inspection record per candidate.

Configuration acquisition is retried no more than eight times. Backoff starts at 100 ms, doubles per attempt, and is capped at 2,000 ms. Inspection ends after eight attempts or 10,000 ms. A backwards monotonic-clock observation ends inspection rather than performing unsafe time subtraction. Detach removes the inspection record, allowing a later USB generation to be considered again.

At most four probes are retained. `snapshot` uses size-query semantics: inadequate output capacity reports the required count and returns false. A successful record includes device token, VID/PID, interface/alternate, RX/TX endpoints, maximum endpoint packet size, V2/V2.1/V3 variant, and SWD/SWIM support flags.

## Sessions and resource ownership

`open` requires a discovered device, SWD or SWIM transport, a free session slot, no existing session for that device, a non-exhausted token generator, and a successful host interface claim. The resulting session owns that claim and records device, endpoints, transport, and generation-safe session token.

If initial transport entry fails, the newly acquired claim is released and the session slot is cleared. Only one session may own a probe at a time.

`close` first re-reads probe mode. If mode discovery fails, the session and host claim remain live. If the probe is in debug or SWIM mode, leaving that mode must succeed before claim release. This deliberately retains ownership when physical probe state is uncertain.

## Raw command and transport behavior

Every command is zero-filled to exactly 16 bytes and written completely to the selected bulk OUT endpoint. Command length must be 1-16 bytes. A request may have either one data-out phase or one data-in phase, never both. Data phases are limited to 6,144 bytes and require a non-null buffer. Timeout must be 1-5,000 ms. Negative, oversized, or short mandatory transfers fail.

Mode discovery sends command `0xf5`. Recognized modes are DFU `0x00`, mass-storage `0x01`, debug `0x02`, SWIM `0x03`, and bootloader `0x04`. DFU and bootloader fail closed because leaving them may re-enumerate the device.

Debug exit uses `0xf2 0x21`; SWIM exit uses `0xf4 0x01`. SWD entry first attempts API-v2 `0xf2 0x30 0xa3` and requires status `0x80`; on failure it retries the API-v1 form using `0x20`. SWIM entry uses `0xf4 0x00`. Provider-generated mode exchanges use 1,000 ms timeouts.

## Build and validation

The exact upstream host fixture passed under `-std=c11 -Wall -Wextra -Werror` with:
`ST-LINK V2/V2.1/V3 discovery, SWD, SWIM, framing and quiescence: PASS`.

That fixture verifies V1 rejection, generation reconsideration after detach/reconnect, V2/V2.1/V3 endpoint matching, SWD and SWIM entry/exit, raw command framing, invalid simultaneous TX/RX rejection, claim cleanup, DFU fail-closed behavior, and quiescence. It is deterministic provider simulation, not physical-probe validation.

The standalone Xtensa builder validates the exact manifest, ELF32 little-endian Xtensa ET_DYN output, sole export `t5_driver_get`, and exactly `memcpy` and `memset` as unresolved imports. It records produced size/SHA-256 and whether the artifact matches the canonical release bytes.

## Established limitations

Verified limitations are: no ST-LINK/V1/SCSI support; no JTAG selector; SWO endpoint 2 IN is ignored; no STM32/STM8 target-specific algorithm is implemented here; four-probe/four-session limits; 6,144-byte/5,000-ms raw data bounds; eight-attempt/10-second discovery bound; and no internal thread synchronization.

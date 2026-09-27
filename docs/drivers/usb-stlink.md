# usb-stlink

## Purpose and scope

`usb-stlink` is the ABI-v2 `debug.vendor.stlink@1` provider for STMicroelectronics ST-LINK V2, V2.1, and V3 USB debug probes. It sits above `usb.host@1` and `platform.clock@1`. The USB host provider retains enumeration, physical interface claims, and bulk-transfer ownership; this ELF owns ST-LINK identity matching, interface/endpoint selection, discovery retry state, 16-byte command framing, exclusive probe sessions, and transitions into or out of SWD/SWIM transport modes.

Target-specific STM32 or STM8 programming/debug algorithms are explicitly outside this provider and belong above `debug.vendor.stlink@1`.

## Package identity and observed release state

- Driver ID: `usb-stlink`
- Version: `0.1.0`
- Driver ABI: `2`
- Architecture: `xtensa-esp32s3`
- Source path: `Drivers/usb_stlink`
- Upstream source tree SHA: `dc392f6d1125f8a55de6660ae19e281e4f27098c`
- `driver.c` blob: `21458e04097b89d11c8b60aec1025b8d61352191`
- `manifest.json` blob: `1cc2df051ea3ff32d8b25be1627dd9bb6123c5ee`
- Host fixture blob: `340e0b6dd362fd42c39a0b0ccb531d80b7d0e1d0`
- `RiscStlinkV1.h` blob: `6540af993431b2bf39a088dd96029e8659a8279e`
- Requires: `usb.host@1`, `platform.clock@1`
- Provides: `debug.vendor.stlink@1`
- Source manifest status: `experimental-unpublished`
- Published tag: `driver-usb-stlink-v0.1.0`
- Canonical `driver.elf`: 9,304 bytes
- Canonical ELF SHA-256: `21500939b991239598ae57a97667c5004e8bccf988a1052d350eca427b5af3be`

The literal source status and observed release-index publication are separate facts: the source still says `experimental-unpublished`, while the inspected release index publishes v0.1.0.

## Source, ABI, build, and validation files

- `Drivers/usb_stlink/driver.c` — exact current upstream implementation.
- `Drivers/usb_stlink/manifest.json` — exact current upstream package manifest.
- `sdk/driver/RiscStlinkV1.h` — ST-LINK capability ABI migrated with this driver.
- `sdk/driver/RiscPlatformClockV1.h` — existing `platform.clock@1` ABI; the destination blob is byte-identical to upstream blob `33a5b27dcea7e30698d0177ad5db065166082194`.
- `sdk/driver/RiscUsbControllerV1.h` / `RiscUsbProviderV1.h` — existing USB host/discovery ABI dependencies.
- `test/drivers/usb_stlink_test.c` — exact current upstream host fixture.
- `scripts/build_usb_stlink.py` — standalone Xtensa build/export/import/release-parity validator.

## Exported root and capability API

The intended public ELF entry point is `t5_driver_get(uint32_t abi)`. It returns the static `risc_driver_v2` only for provider ABI 2. The descriptor advertises `debug.vendor.stlink@1`.

`risc_stlink_api_v1` contains:
- `poll(context, max_events)` for bounded discovery work;
- `snapshot(context, out, inout_count)` for current probe metadata;
- `open(context, device, transport)` for an exclusive probe claim plus SWD/SWIM entry;
- `set_transport(context, session, transport)` to re-enter SWD or SWIM on an existing exclusive session;
- `command(...)` for a bounded raw ST-LINK command with optional data-out or data-in phase;
- `close(context, session)` to leave the active transport before releasing the USB claim.

The ABI fixes `RISC_STLINK_MAX_PROBES` at 4, command packets at 16 bytes, and raw data phases at no more than 6,144 bytes.

## Dependency binding

`start` requires exactly two dependencies. They may appear in either order, but each capability may appear only once:

- `usb.host` with API version 1, represented by `risc_usb_host_discovery_v1`;
- `platform.clock` with API version 1, represented by `risc_platform_clock_api_v1`.

The host table must provide the v1 prefix plus `configuration`, `claim`, `release`, `bulk_read`, `bulk_write`, discovery `poll`, and `devices`. The driver does not use the host control-transfer callback. The clock table must provide `monotonic_ms`.

Startup fails for missing, duplicate, unknown, or structurally undersized dependencies, missing required callbacks, or a second start while already bound.

## Supported ST-LINK USB identities

The implementation requires STMicroelectronics VID `0x0483`.

Supported PID mapping is exactly:

- `0x3748` → ST-LINK V2.
- `0x374b`, `0x3752` → ST-LINK V2.1.
- `0x374d`, `0x374e`, `0x374f`, `0x3753`, `0x3754`, `0x3755`, `0x3757` → ST-LINK V3 family.

ST-LINK/V1 PID `0x3744` is deliberately not supported because the source identifies it as using SCSI transport rather than the V2/V2.1/V3 raw-bulk 16-byte command transport.

No other VID/PID is accepted.

## USB configuration and endpoint matching

The configuration parser accepts a complete USB configuration descriptor only when:
- total length is at least 9 and no more than `RISC_USB_CONFIG_LIMIT`;
- descriptor type is configuration (`2`);
- `wTotalLength` exactly equals the returned buffer length;
- every descriptor is structurally in bounds and has `bLength >= 2`.

The selected ST-LINK transport must be a vendor-class interface (`0xff`) with a valid bulk command-response IN endpoint and bulk command/data OUT endpoint. Response IN must be endpoint number 1. OUT may be endpoint number 1 or 2. Packet sizes must be nonzero and no greater than 512 bytes. Endpoint number zero and reserved endpoint-address bits are rejected.

V2.1/V3 probes may expose endpoint 2 IN for SWO trace; the parser intentionally prefers/retains endpoint 1 IN for command responses and ignores SWO endpoint 2 IN.

The whole device must yield exactly one qualifying vendor interface. Ambiguous multiple candidates fail closed.

## Discovery state, retry bounds, and clock use

`poll` requires a work bound from 1 through 16. It first delegates bounded host discovery, snapshots at most `RISC_USB_HOST_MAX_DEVICES` currently present device tokens, removes probe/inspection records whose generation-qualified device tokens disappeared, and obtains current monotonic milliseconds from `platform.clock`.

For a device not already represented as a probe, the driver stores inspection state containing device token, first-attempt time, last-attempt time, attempt count, and completion flag.

Configuration acquisition can retry at most 8 times. Retry delay begins at 100 ms, doubles by attempt, and is capped at 2,000 ms. A device is abandoned after 8 attempts or, once at least one attempt has occurred, after 10,000 ms from the beginning of inspection. A monotonic clock reversal marks that inspection done rather than attempting subtraction through wrap/reversal.

Once configuration acquisition succeeds, the inspection is marked done regardless of whether VID/PID or descriptors qualify. Detach clears inspection/probe state, allowing a later USB generation to be reconsidered.

## Probe snapshots

The provider stores at most four current probes. `snapshot` uses size-query semantics: insufficient capacity, or a null output for a nonempty snapshot, stores the required count and returns false. A successful snapshot reports the generation-qualified device token, VID/PID, interface/alternate, chosen RX/TX endpoints, maximum of the two endpoint packet sizes, V2/V2.1/V3 variant, and both `supports_swd` and `supports_swim` as true.

The provider does not dynamically allocate probe/session state.

## Session ownership and tokens

`open` requires:
- the provider to be started;
- a nonzero discovered device token;
- SWD or SWIM as the requested transport;
- a free one-of-four session slot;
- no existing session for that same device;
- token generation not already at `UINT64_MAX`;
- a successful nonzero USB interface claim.

The session stores its own monotonically increasing token, device token, physical host claim, selected transport, and RX/TX endpoints. Session serial state is not reset by `stop`, so stale handles are not intentionally recycled across clean restart.

If initial transport entry fails, the newly acquired USB claim is released and the session slot is zeroed.

## ST-LINK command framing

Every raw command is copied into a zero-filled 16-byte packet and written in full to the selected bulk OUT endpoint. Command length must be 1 through 16 bytes.

A command may have either:
- a data-out phase up to 6,144 bytes, or
- a data-in phase up to 6,144 bytes,
but not both.

A requested data phase requires a non-null buffer. Timeout must be from 1 through 5,000 ms. The provider rejects a short 16-byte command-packet write. For the optional data phase it accepts only nonnegative host returns no larger than the requested length; larger or negative results become failure.

`command` exposes the raw result length for valid data phases, or zero for a command with no data phase.

## Mode discovery and SWD/SWIM transitions

The provider queries current probe mode by sending single-byte ST-LINK command `0xf5` and reading up to two response bytes. The first response byte is interpreted as current mode:

- `0x00` DFU;
- `0x01` mass-storage;
- `0x02` debug;
- `0x03` SWIM;
- `0x04` bootloader.

DFU and bootloader modes fail closed: the provider does not silently leave them because the source comments identify those modes as re-enumerating.

When a mode must be left:
- debug exit uses command `0xf2 0x21`;
- SWIM exit uses command `0xf4 0x01`;
- mass-storage mode requires no explicit exit command in this implementation.

SWD entry first attempts debug API v2 with `0xf2 0x30 0xa3` and requires status byte `0x80`; if that does not succeed, it retries API v1 by substituting `0x20`. SWIM entry sends `0xf4 0x00` and expects no reply. A session already in SWIM can be accepted without a mode cycle when SWIM is requested again.

All provider-generated mode-management exchanges use a 1,000 ms timeout.

## Close, failure retention, and quiescence

`close` first re-queries the current mode. If mode discovery fails, the session and claim remain live. If the probe is in debug or SWIM, leaving that mode must succeed before the USB claim is released. This deliberately retains the claim when physical probe state is uncertain.

After successful mode handling, `close` releases the host claim and zeros the session slot.

`quiesce` returns false while any session token is live. `stop` therefore refuses to clear dependencies/state while a session exists. Once quiescent, `stop` clears host/clock pointers plus discovered-probe and inspection arrays. Session serial generation is not cleared.

## Concurrency and execution assumptions

The source contains no locks or atomic operations. Mutable provider state, probe tables, and session tables are ordinary file-static data. Therefore the implementation itself establishes no multi-threaded safety guarantee beyond the serialized provider-executor assumption used by the surrounding RiscRTE provider model.

## Exact host validation performed for migration

The exact upstream fixture was compiled with `-std=c11 -Wall -Wextra -Werror` against a host shared-object build of the exact driver and passed:

`ST-LINK V2/V2.1/V3 discovery, SWD, SWIM, framing and quiescence: PASS`

The fixture establishes:
- V1 PID rejection;
- detach/reconnect generation reconsideration;
- V2 discovery with EP1 IN / EP2 OUT;
- exclusive claim and non-quiescence while open;
- transition from debug mode back into SWD;
- raw command framing and response reads;
- rejection of simultaneous data-out and data-in phases;
- successful close/release and quiescence;
- V2.1 discovery with EP1 IN / EP1 OUT while ignoring SWO EP2 IN;
- SWIM entry, repeated SWIM selection, and SWIM exit before close;
- V3-family recognition;
- fail-closed handling when current mode is DFU, including release of the claim acquired during the failed `open`.

The fixture is deterministic provider/transport simulation. It does not establish behavior against a physical ST-LINK probe, STM32 target, STM8 target, or actual USB controller timing.

## Standalone Xtensa build validation

`scripts/build_usb_stlink.py` validates the exact manifest, builds a 32-bit little-endian Xtensa `ET_DYN` ELF, normalizes supported Xtensa relocations, requires `t5_driver_get` as the sole global function export, and requires exactly `memcpy` and `memset` as unresolved runtime imports.

It records produced size/SHA-256 and whether the result exactly matches the canonical release artifact.

## Published package metadata

Observed v0.1.0 package files:

- `.package.json`: 664 bytes, SHA-256 `d9ac2e4e5d0e106825e55a32b8a72a030a551079cd6e50e3b90df157b5636ce6`
- `driver.elf`: 9,304 bytes, SHA-256 `21500939b991239598ae57a97667c5004e8bccf988a1052d350eca427b5af3be`
- `provider-abi.v1`: 48 bytes, SHA-256 `f63530c5bc8135503bf1871e91449522cd35fed8c039f5869430914b7dd0a9bc`
- `privileged-imports.v1`: 14 bytes, SHA-256 `a46fff766cb063bc348b3e7a6670e4a48c5c9201f0da92e73a67670229be7cca`

The 14-byte privileged-import file corresponds to the build validator's expected `memcpy` and `memset` imports.

## Established limitations

- ST-LINK/V1/SCSI transport is deliberately unsupported.
- The provider exposes SWD and SWIM only; it does not expose a JTAG transport selector.
- SWO trace endpoint 2 IN is intentionally ignored.
- There is no target-specific STM32/STM8 flash/debug algorithm in this provider.
- Maximum concurrent discovered probes and exclusive sessions are each four.
- Raw command data phases are capped at 6,144 bytes and 5,000 ms.
- Discovery retries are bounded to 8 attempts / 10 seconds.
- The implementation does not establish internal multi-thread synchronization.

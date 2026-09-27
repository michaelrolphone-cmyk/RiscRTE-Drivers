# gt911-touch

## Purpose

`gt911-touch` is the provider-v2 driver for a Goodix GT911 touch controller. It owns GT911 register-level behavior while delegating physical I2C transactions to the `i2c.bus@1` provider and timestamps to `platform.clock@1`. It provides the transport-neutral `input.touch.raw@1` capability.

## Package identity

- Driver ID: `gt911-touch`
- Version: `0.1.0`
- Driver ABI: `2`
- Architecture: `xtensa-esp32s3`
- Provides: `input.touch.raw@1`
- Requires: `i2c.bus@1`, `platform.clock@1`
- Manifest status string: `experimental-unpublished` (still present in the upstream source manifest)
- Upstream release status: published as `driver-gt911-touch-v0.1.0`; canonical ELF size 8,736 bytes, SHA-256 `9b934b1056fc8a99f4973dccc311d991f4c4ff1e46b826d27fc2919e6555ec6d`

## Source and build files

- `Drivers/gt911_touch/driver.c`
- `Drivers/gt911_touch/manifest.json`
- `sdk/driver/RiscTouchV1.h`
- `sdk/driver/RiscI2cBusV1.h`
- `sdk/driver/RiscPlatformClockV1.h`
- `sdk/driver/RiscProviderV2.h`
- `scripts/build_gt911_touch.py`

The standalone build uses the Xtensa ESP32-S3 GCC toolchain, PIC/shared-object flags, SysV ELF hashing, relocation normalization, exported-symbol validation, and ELF32/Xtensa validation.

## Provider ABI

The only public driver symbol is `t5_driver_get(uint32_t abi)`. It returns a static `risc_driver_v2` only for ABI 2.

The descriptor identifies `gt911-touch`, advertises `input.touch.raw` API 1, and supplies `start`, `stop`, and `quiesce`.

## Touch capability

`risc_touch_api_v1` provides:

- `subscribe`
- `unsubscribe`
- `poll`
- `next`
- `snapshot`

The interface supports at most 5 simultaneous contacts, 4 subscribers, and 32 queued events per subscriber.

Event kinds are DOWN, MOVE, UP, BUTTON_DOWN, and BUTTON_UP. Each event carries a monotonically increasing sequence number and timestamp, contact/button identifier, and coordinates where applicable.

A snapshot contains the current sequence/timestamp, reported surface dimensions, active contact count, button bitmask, and up to five current contacts.

## GT911 hardware behavior

The driver tries two 7-bit I2C addresses in order:

1. `0x5d`
2. `0x14`

It reads 11 bytes beginning at GT911 product-information register `0x8140`, extracts surface width/height from bytes 6-9, and rejects zero dimensions or dimensions above 4096.

The status register is `0x814e`; first-point data begins at `0x814f`. The ready bit is `0x80`, touch-count mask is `0x0f`, and key/button indication is `0x10`.

All register transactions use a 20 ms timeout.

## Polling and report parsing

`poll` requires `max_reports` from 1 through 16. It services reports until the requested limit is reached or the controller reports no ready data.

For each ready report:

- touch count above 5 is rejected;
- each contact consumes 8 raw bytes;
- duplicate contact IDs are rejected;
- coordinates outside the probed surface dimensions are rejected;
- the driver acknowledges the report by writing zero to the GT911 status register before publishing events.

If acknowledgement fails, the report is not published, preventing the same unacknowledged hardware report from being emitted twice.

## Event state and subscriber queues

The driver compares each new contact set with the prior snapshot:

- missing previous contacts emit UP;
- new IDs emit DOWN;
- coordinate changes on retained IDs emit MOVE;
- GT911 key state maps to the primary touch button and emits BUTTON_DOWN/BUTTON_UP on transitions.

Every active subscriber receives its own copied event queue.

If a subscriber queue reaches 32 events, the queue is cleared and marked with a gap. The next `next` call returns `-1`; the interface contract requires consumers to discard derived state and call `snapshot` to re-establish authoritative state.

`next` returns 1 for an event, 0 when empty, and -1 for a stale subscription or queue gap in the current implementation.

## Lifecycle and ownership

`start` requires exactly two valid dependencies: an `i2c.bus@1` API with claim/transact/release operations and a `platform.clock@1` API with monotonic time.

The driver claims one GT911 I2C address and retains that claim while active. It fails startup if no clock is available or neither GT911 address probes successfully.

`quiesce` refuses to unload while any subscriber token remains active. When no subscribers remain, it releases the I2C device claim and clears provider state. `stop` calls `quiesce`.

This establishes the driver as the GT911 register/session owner while leaving bus-controller serialization and physical I2C implementation to the `i2c.bus` provider.

## Failure behavior and limits

Confirmed failure conditions include invalid/missing dependencies, failure to claim either I2C address, bad surface dimensions, I2C transaction failure, invalid touch count, duplicate IDs, out-of-range coordinates, acknowledgement failure, invalid poll count, and inability to release the I2C claim during quiescence.

If the platform clock fails during report service, event timestamping falls back to the most recent snapshot timestamp rather than inventing a new time value.

The source contains no IRQ/interrupt path; this implementation is explicitly serviced through `poll`.

## Publication state

T5S3-Reader now publishes `gt911-touch` version 0.1.0 in the release index as `driver-gt911-touch-v0.1.0`. The canonical `driver.elf` is 8,736 bytes with SHA-256 `9b934b1056fc8a99f4973dccc311d991f4c4ff1e46b826d27fc2919e6555ec6d`. RiscRTE-Drivers CI run 36277642683 independently built the migrated source to the same size and SHA-256, establishing byte-for-byte published ELF parity for this version. The upstream source manifest still carries the literal status field `experimental-unpublished`; this documentation records that source fact separately from the observed release-index publication state.

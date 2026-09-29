# gt911-touch

## Purpose and scope

`gt911-touch` is the provider-v2 driver for the Goodix GT911 touch controller. It owns GT911 register/report interpretation while delegating physical bus transfers to `i2c.bus@1` and timestamps to `platform.clock@1`. It provides the transport-neutral `input.touch.raw@1` capability.

This page documents the upstream 0.1.1 source currently mirrored into RiscRTE-Drivers. Behavior below is derived from `Drivers/gt911_touch/driver.c`, its manifest, `RiscTouchV1.h`, the current upstream host fixture, the upstream build script, and published package metadata.

## Package identity

- Driver/package ID: `gt911-touch`
- Version: `0.1.1`
- Driver ABI: 2
- Architecture: `xtensa-esp32s3`
- Source path: `Drivers/gt911_touch`
- Upstream source tree SHA: `917adb535504b77d831f6dfc09445fc1070164df`
- Provides: `input.touch.raw@1`
- Requires: `i2c.bus@1`, `platform.clock@1`
- Source-manifest status: `experimental-unpublished`
- Published release tag: `driver-gt911-touch-v0.1.1`
- Published `driver.elf`: 42,976 bytes
- Published `driver.elf` SHA-256: `44d753b736a2a433549ab500a3cae52f1e2844f79332fd119fc8df8d57cd11f4`

The source manifest still contains `experimental-unpublished`; the release-index independently establishes that 0.1.1 is published. Those are separate observed facts.

## Source, ABI, build, and test files

- `Drivers/gt911_touch/driver.c`
- `Drivers/gt911_touch/manifest.json`
- `sdk/driver/RiscTouchV1.h`
- `sdk/driver/RiscI2cBusV1.h`
- `sdk/driver/RiscPlatformClockV1.h`
- `sdk/driver/RiscProviderV2.h`
- `test/drivers/gt911_touch_test.c`
- `test/drivers/stub_idf_i2c/freertos/FreeRTOS.h`
- `test/drivers/stub_idf_i2c/freertos/semphr.h`
- `test/run_gt911_touch_test.sh`
- `scripts/build_gt911_touch.py`
- `scripts/build_gt911_release_parity.py`

The current upstream release build uses the ESP32-S3 PlatformIO compilation database so the provider is compiled with the firmware's actual FreeRTOS configuration. The RiscRTE-Drivers migration tooling replays that release environment for canonical byte comparison rather than replacing the upstream build inputs with an inferred configuration.

## Exported root and provider descriptor

The only intended public function export is `t5_driver_get(uint32_t abi)`. ABI values other than provider ABI 2 return no descriptor. The ABI-2 descriptor identifies `gt911-touch`, advertises `input.touch.raw` API 1, and supplies `start`, `stop`, and `quiesce`.

## `input.touch.raw@1` interface

`risc_touch_api_v1` exposes:

- `subscribe(context)`
- `unsubscribe(context, subscription)`
- `poll(context, max_reports)`
- `next(context, subscription, out)`
- `snapshot(context, out)`

Implementation and ABI constants establish:

- maximum contacts: 5
- maximum subscribers: 4
- copied event queue length per subscriber: 32
- event kinds: DOWN, MOVE, UP, BUTTON_DOWN, BUTTON_UP
- primary button mask: bit 0

Each event contains a sequence number, monotonic timestamp, contact or button ID, and coordinates. A snapshot contains the authoritative current sequence/timestamp, surface dimensions, active contacts, and button bitmask.

A new subscriber receives no historical queue entries. The ABI requires the consumer to call `snapshot()` after subscribing when it needs authoritative current state.

## Concurrency and state ownership in 0.1.1

Version 0.1.1 adds a provider-local FreeRTOS mutex around the entire public touch API state machine. The source explicitly allows public calls to originate from a capture task and an app while lifecycle start/stop remains serialized by the grant-owning loader.

`lock_state()` waits for the provider mutex using the same 20 ms bound used by GT911 register I/O. If the millisecond-to-tick conversion would be zero, it uses one tick.

The lock protects complete report processing and state mutation rather than relying on the `i2c.bus` provider's per-transfer serialization. This prevents a second caller from interleaving status read, point read, acknowledgement, subscriber queues, or snapshot state with an active report.

When the state mutex cannot be acquired:

- boolean operations fail;
- `subscribe` returns zero;
- `next` returns `-2`, which the ABI documents as provider fault/busy rather than queue GAP.

## Hardware identification and register interaction

The provider probes these 7-bit I2C addresses in order:

1. `0x5d`
2. `0x14`

It reads 11 bytes beginning at product-information register `0x8140`. Width and height are taken from bytes 6-9. Startup rejects zero dimensions and dimensions above the source's 4096 bound.

Report registers and masks established in source:

- status register: `0x814e`
- first point: `0x814f`
- READY: `0x80`
- touch-count mask: `0x0f`
- key/button indication: `0x10`
- per-contact record: 8 bytes
- transaction timeout: 20 ms

The driver never directly configures ESP32-S3 I2C hardware. Register reads and writes go through the retained `i2c.bus@1` device claim.

## Report processing and acknowledgement semantics

`poll` validates `max_reports` in the range 1 through 16. In 0.1.1, one invocation intentionally services at most one complete hardware report even when a larger value is supplied. The source comment identifies the reason: one report can require up to three 20 ms bus operations, so a single caller must not monopolize the provider/bus while READY is continuously asserted.

For a READY report:

- contact counts above 5 are rejected;
- contact data is read only when count is nonzero;
- duplicate contact IDs are rejected;
- coordinates outside the probed surface are rejected;
- GT911 key state maps to `RISC_TOUCH_BUTTON_PRIMARY`.

A failed point-data read does **not** acknowledge the GT911 status register. READY is deliberately left latched so the unread report can be retried on a later bounded poll.

Malformed reports are discarded and every active subscriber is marked GAP. Their pending event queues are cleared so a later state transition cannot silently fabricate a continuous gesture after invalid hardware data.

For a valid report, 0.1.1 applies the validated contact/button state and enqueues derived events **before** attempting the status acknowledgement. This handles the ambiguous failure case in which the ACK write may have reached the controller even though the bus call returned failure:

- if ACK actually cleared READY, the validated edge has already been retained;
- if ACK did not clear READY, the next poll sees the same state and `apply_state` emits no duplicate edge;
- the provider does not blindly retry the ACK because that could clear a newer controller report.

Consequently `poll` can return false after making partial observable progress. The ABI comment explicitly tells consumers to drain `next()` even after a failed `poll`.

## Event derivation and queue semantics

Compared with the previous snapshot:

- a missing old contact emits UP;
- a new contact ID emits DOWN;
- a retained ID whose coordinates changed emits MOVE;
- primary key transitions emit BUTTON_DOWN/BUTTON_UP.

Every active subscriber gets a copied queue.

When a subscriber queue overflows, that subscriber is marked GAP and its buffered entries are discarded. `next` then returns `-1`; the ABI requires the consumer to discard derived state and use `snapshot()` to recover authoritative current state. `-1` also represents a stale subscription handle. `-2` is reserved for provider fault/state-lock failure.

## Lifecycle and stale-handle behavior

`start` requires exactly two valid provider dependencies: `i2c.bus@1` with claim/transact/release and `platform.clock@1` with monotonic time. It creates the state mutex before probing the controller and deletes the mutex if startup fails.

The selected GT911 I2C claim is retained while the provider is active.

Version 0.1.1 keeps the subscription serial monotonically increasing across clean stop/start cycles rather than resetting it. This prevents an old subscription token from becoming valid again after restart.

`quiesce` participates in the same state lock and refuses to complete while any subscriber remains. Once subscribers are gone, it releases the I2C claim and clears active provider state. `stop` attempts quiescence and deletes the state mutex only after successful quiescence.

## Failure behavior verified by the host fixture

The current upstream fixture exercises:

- ABI rejection/acceptance;
- dependency/start validation;
- primary and fallback address probing;
- DOWN/MOVE/UP delivery to multiple subscribers;
- snapshots;
- unread point-data retry with READY left asserted;
- failed ACK where the controller did and did not actually clear READY;
- no duplicated edge after ambiguous ACK;
- malformed/out-of-range reports becoming subscriber GAP;
- provider-wide serialization when another thread is inside status I/O;
- `next == -2` during state-lock contention;
- primary button transitions;
- subscriber queue overflow/GAP recovery;
- quiesce refusal with live subscribers;
- release on successful quiesce;
- restart with monotonic subscription generations.

These host tests use mocked I2C and clock providers. They do not establish real GT911 electrical behavior, interrupt timing, or actual FreeRTOS scheduling on hardware.

## Published package metadata

Observed in the current upstream release index for v0.1.1:

- `.package.json`: 666 bytes, SHA-256 `a286fbd5f56b54d88a3291b6bdca5594bdfdc985837ca790948a443beb95fc08`
- `driver.elf`: 42,976 bytes, SHA-256 `44d753b736a2a433549ab500a3cae52f1e2844f79332fd119fc8df8d57cd11f4`
- `provider-abi.v1`: 44 bytes, SHA-256 `b2e84b614f7f76e8dd0a971a4be0a7eea4374d22f8b79ce3062d041bfa1630ed`
- `privileged-imports.v1`: 118 bytes, SHA-256 `7afc3a0dd435251841149aa30a2041f60292b15abd20cecb1e80fbdd25ba3cba`

The release-index metadata establishes the privileged-imports file's bytes/hash, but not its text contents. This document therefore does not invent the exact published import list. Source inspection establishes use of the FreeRTOS semaphore/mutex API; the upstream release builder validates imports against the firmware's permitted export set.

## Migration validation status

The v0.1.1 source, manifest, touch/I2C ABI headers, and host fixture are synchronized into RiscRTE-Drivers. The prepared canonical replay targets the observed 42,976-byte SHA-256 above and is intended to fail closed under `--require-byte-parity`.

Until that strict replay has passed on the destination CI, v0.1.1 must not be represented as parity-complete.

## Established limitations

The implementation is polled; no GT911 IRQ handling is present in the driver source. It depends on the external `i2c.bus@1` and `platform.clock@1` providers and does not own physical I2C hardware. Host validation cannot establish board electrical behavior or device timing.

# i2c-esp32s3-v2

## Purpose and scope

`i2c-esp32s3-v2` is the ABI-v2 `i2c.bus@1` provider for the T5S3 Pro. Version 0.1.6 remains a firmware-backed transitional provider: the ELF owns logical claims, provider-local serialization, timeout admission, and lifecycle state, while physical I2C0 transfer execution is delegated to the firmware-private `risc_fw_i2c_transact_v1` bridge.

This page documents the current 0.1.6 source, ABI header, host fixture, upstream probe, and published package metadata. It does not infer direct hardware ownership that is absent from source.

## Package identity

- Driver/package ID: `i2c-esp32s3-v2`
- Version: `0.1.6`
- Driver ABI: 2
- Architecture: `xtensa-esp32s3`
- Board: `t5s3-pro`
- Source path: `Drivers/i2c_esp32s3_v2`
- Upstream source tree SHA: `788ca9625e27f0fd8d07bed86cc90d2bf3c32bc7`
- Requires: none
- Provides: `i2c.bus@1`
- Source-manifest status: `experimental-unpublished`
- Published tag: `driver-i2c-esp32s3-v2-v0.1.6`
- Published `driver.elf`: 24,960 bytes
- Published `driver.elf` SHA-256: `0230f71ca21340165c59cba89e18e30ba169671c714dd1098ed5b4994f90fc34`

The source manifest's status string and the release-index publication state are recorded separately.

## Source, ABI, build, and test files

- `Drivers/i2c_esp32s3_v2/driver.c`
- `Drivers/i2c_esp32s3_v2/manifest.json`
- `Drivers/i2c_esp32s3_v2/exports.map`
- `Drivers/i2c_esp32s3_v2/loader_sections.ld`
- `sdk/driver/RiscProviderV2.h`
- `sdk/driver/RiscI2cBusV1.h`
- `sdk/driver/RiscFirmwareI2cCompatV1.h`
- `test/drivers/i2c_esp32s3_v2_test.c`
- `test/drivers/stub_idf_i2c/freertos/FreeRTOS.h`
- `test/drivers/stub_idf_i2c/freertos/semphr.h`
- `test/drivers/stub_idf_i2c/freertos/task.h`
- `scripts/build_i2c_esp32s3_v2.py`

The current upstream release build is produced through `scripts/probe_i2c_esp32s3_v2.py` using the full `t5s3-pro` PlatformIO compilation database. The destination canonical-replay builder is designed to use the exact release commit and original Actions workspace path rather than approximate the release compiler environment.

## Exported root and capability

The only intended public function export is `t5_driver_get(uint32_t abi)`. It returns the static provider descriptor only for provider ABI 2.

The descriptor advertises `i2c.bus@1` and provides:

- `claim_device`
- `transact`
- `release_device`

The driver declares no provider dependency because its physical transport is a privileged firmware bridge rather than another public provider capability.

## Fixed resources and claim model

The source contains 12 static claim slots. Each active slot records a 64-bit token and one 7-bit I2C address.

Claim rules established in source:

- valid addresses are `0x08` through `0x77`;
- the output-token pointer must be non-null;
- the provider must be started;
- a device address cannot be claimed twice;
- a claim fails when all 12 slots are occupied;
- token generation fails rather than wrapping at `UINT64_MAX`.

Token generation is kept monotonic across clean stop/start cycles so stale claims cannot become valid after restart.

## Provider serialization

One FreeRTOS mutex protects provider-local state and complete synchronous transfers. Claim, release, quiesce, and transaction admission therefore cannot race the claim table or each other.

Physical bus serialization remains the firmware's responsibility after the provider enters `risc_fw_i2c_transact_v1`.

## Transaction limits

The source rejects a transaction when:

- the claim token is zero or stale;
- both write and read phases are empty;
- either phase exceeds 128 bytes;
- a nonzero phase has a null buffer;
- `timeout_ms` is zero;
- `timeout_ms` exceeds 3,000 ms.

Combined write/read is one synchronous operation through the firmware bridge.

## Version 0.1.6 deadline semantics

Version 0.1.6 changes timeout handling so `timeout_ms` is a **total admission plus transfer budget**, not a fresh timeout for each nested synchronization layer.

`RiscI2cBusV1.h` now states that only the remaining budget should be forwarded to the physical transport and notes that tick rounding/scheduler latency can add one scheduler tick.

Implementation behavior:

1. capture the starting FreeRTOS tick using `xTaskGetTickCount`;
2. convert the caller's millisecond budget to mutex-wait ticks (raising zero conversion to one tick);
3. wait for the provider mutex within that budget;
4. after the mutex is obtained, measure elapsed ticks;
5. fail without entering the firmware backend when the caller's total budget has already been consumed;
6. otherwise pass only the remaining millisecond budget to `risc_fw_i2c_transact_v1`.

A queued caller therefore does not receive a fresh full transfer timeout after waiting for another transaction.

## Lifecycle

`start` accepts no dependencies. It requires no active start state, no existing state mutex, and no residual claims. It creates the FreeRTOS mutex and then marks the provider started.

`quiesce` waits for provider serialization, checks all claim slots, and refuses to clear the active state while any claim remains.

`stop` attempts quiescence. If claims remain, the provider and mutex are retained. After clean quiescence it deletes the mutex.

`release_device` is serialized by the same mutex, so it cannot remove a claim while a transfer holding that mutex is still executing.

## Privileged/runtime imports

The current upstream 0.1.6 probe requires exactly these unresolved runtime symbols in the linked provider:

- `risc_fw_i2c_transact_v1`
- `vQueueDelete`
- `xQueueCreateMutex`
- `xQueueGenericSend`
- `xQueueSemaphoreTake`
- `xTaskGetTickCount`

The probe rejects direct I2C/GPIO/RTC-GPIO/peripheral/T5/USB implementation imports outside the dedicated firmware bridge. It also rejects `__atomic*` and `__sync*` helpers.

The additional `xTaskGetTickCount` import is required by 0.1.6's total-deadline accounting.

## Host validation

The current upstream pthread-backed fixture covers:

- provider ABI identity and start rules;
- address bounds and duplicate claims;
- write, read, and combined transfers;
- buffer, phase-length, and timeout limits;
- firmware bridge failure propagation;
- provider serialization with multiple transaction threads;
- a short-deadline caller timing out while another transaction holds the provider;
- proof that a timed-out queued caller never enters the mocked backend;
- proof that a queued long-budget caller receives a reduced remaining timeout instead of its original timeout;
- `release_device` waiting for an in-flight transfer;
- stale-token rejection;
- quiescence and restart;
- monotonically increasing token generations.

The migrated destination host semaphore fixture models finite mutex acquisition with C11 `timespec_get(..., TIME_UTC)` plus `pthread_mutex_trylock`/`thrd_yield`, and its `xTaskGetTickCount` shim uses the same C11 time source. This is host-test scaffolding only; the target driver imports the real FreeRTOS `xTaskGetTickCount`.

These tests establish provider semantics only. They do not establish real FreeRTOS scheduling, actual I2C electrical timing, or board hardware behavior.

## Published package metadata

Observed in the current upstream release index for v0.1.6:

- `.package.json`: 589 bytes, SHA-256 `d15633932ef9c9e2c5997e21a2671b1124b2d1bd22e20a0825db019ac1b0ee13`
- `driver.elf`: 24,960 bytes, SHA-256 `0230f71ca21340165c59cba89e18e30ba169671c714dd1098ed5b4994f90fc34`
- `provider-abi.v1`: 36 bytes, SHA-256 `5b40fe49054c4e3e69ffab68aa4ae41be769e5a77928a8cdea5a666a37447f14`
- `privileged-imports.v1`: 111 bytes, SHA-256 `ab1023d92c25c71f838d5c824fcfa2365ce439af298cd28bcae28a559b3ff390`

## Migration validation status

The 0.1.6 source/manifest, `RiscI2cBusV1.h`, total-deadline host fixture, and historical release replay are synchronized into RiscRTE-Drivers. The replay audits the exact six-symbol runtime import set above, including the new `xTaskGetTickCount` dependency.

GitHub Actions run `36545152857` on destination commit `4cb37724100aa427a04909b27c5f22a11b73c3ba` first passed the updated I2C host fixture and then passed `python scripts/build_i2c_esp32s3_v2.py --require-byte-parity`. Because that builder fails closed unless it is running at the canonical GitHub Actions workspace path and reproduces both the published size and SHA-256, this establishes canonical v0.1.6 parity for the 24,960-byte ELF SHA-256 `0230f71ca21340165c59cba89e18e30ba169671c714dd1098ed5b4994f90fc34`. The overall workflow later failed at the malformed GT911 standalone builder, after the I2C validation steps had already succeeded; that later failure does not invalidate the completed I2C gate.

## Established limitations

The provider remains dependent on `risc_fw_i2c_transact_v1`; it is not an independent owner of ESP32-S3 I2C0. Host tests validate synchronization and deadline semantics, not physical bus behavior or actual scheduler timing.

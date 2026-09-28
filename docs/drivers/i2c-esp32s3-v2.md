# i2c-esp32s3-v2

## Purpose and scope

`i2c-esp32s3-v2` is the ABI-v2 `i2c.bus@1` provider for the T5S3 Pro. Version 0.1.5 remains transitional: the ELF does not configure or own ESP32-S3 I2C0 directly. Physical transfers are delegated to the firmware-private compatibility entry point `risc_fw_i2c_transact_v1`, while the ELF owns logical device claims, provider lifecycle state, and provider-local serialization.

## Package identity

- Driver ID: `i2c-esp32s3-v2`
- Version: `0.1.5`
- Driver ABI: 2
- Architecture: `xtensa-esp32s3`
- Board: `t5s3-pro`
- Source path: `Drivers/i2c_esp32s3_v2`
- Upstream source tree SHA: `7d22b9d723d055008fbc99da214a5da3e8fb5a0f`
- Manifest status string: `experimental-unpublished`
- Requires: none
- Provides: `i2c.bus@1`
- Release tag: `driver-i2c-esp32s3-v2-v0.1.5`
- Canonical ELF: 24,496 bytes
- Canonical ELF SHA-256: `7b8f51f62da71e99949b093b6cdc531435a0cec01f87740921f9436544f8bd9c`

The source manifest still says `experimental-unpublished`, while the inspected release index publishes version 0.1.5. These are recorded as separate source facts.

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
- `scripts/xtensa_stubs/freertos/FreeRTOS.h`
- `scripts/xtensa_stubs/freertos/semphr.h`
- `scripts/build_i2c_esp32s3_v2.py`

The implementation, manifest, host behavior fixture, and host semaphore fixture are copied from the current upstream 0.1.5 source. The `scripts/xtensa_stubs` files are build-only declarations: they model the ESP-IDF semaphore macro-to-queue symbol ABI so a standalone Xtensa build can leave the same runtime imports unresolved without embedding another FreeRTOS implementation.

## Provider ABI and capability

The only intended public ELF function is `t5_driver_get(uint32_t abi)`. It returns the static `risc_driver_v2` descriptor only for ABI 2. The provider advertises `i2c.bus@1` and exposes `claim_device`, `transact`, and `release_device`. No provider dependency is declared.

## Version 0.1.5 synchronization model

Versions 0.1.3 and 0.1.4 used relocatable atomic coordination state. Version 0.1.5 removes that design. One ordinary FreeRTOS mutex (`state_lock`) protects all provider-local state and is held across complete synchronous calls to `risc_fw_i2c_transact_v1`.

Concurrent callers therefore queue at the provider instead of receiving a synthetic busy failure. Claim, release, and quiesce operations cannot mutate the claim table while a transaction owns the mutex. `release_device` waits for that mutex, which is the implementation's drain guarantee for an in-flight transaction.

Firmware still owns the board-level recursive I2C/Wire lock and remains the physical I2C0 owner.

## Fixed resources and claims

The provider has 12 static claim slots, each containing a 64-bit token and an address. Claimable addresses are 0x08 through 0x77. Duplicate address claims fail. A claim fails if the table is full, the output pointer is null, the provider is not started, or token generation has reached `UINT64_MAX`.

`next_token` is not reset on a clean stop/start, so tokens remain monotonically increasing and stale handles do not become valid again after restart.

## Lifecycle

`start` requires zero dependencies, no active start state, no existing mutex, and no residual claim. It creates the FreeRTOS mutex and sets `started`.

`quiesce` waits indefinitely for the mutex, checks all 12 claim slots, and only clears `started` when none remain. If any claim exists, quiesce returns false and leaves the provider active.

`stop` first attempts quiescence. If live claims prevent it, stop returns without deleting the mutex. After successful quiescence, it clears `state_lock` and calls `vSemaphoreDelete`.

## Transactions

A transaction is rejected when its token is zero, both phases are empty, either phase exceeds 128 bytes, a required buffer is null, timeout is zero, or timeout exceeds 3,000 ms.

The timeout is converted with `pdMS_TO_TICKS`; a zero-tick conversion is raised to one tick. The same provider mutex is acquired with that bounded wait. While holding it, the implementation verifies the provider is started, resolves the token to an address, and calls `risc_fw_i2c_transact_v1`. The bridge receives the original timeout in milliseconds. Its false return is propagated without fabricated success or read data.

Because the mutex remains held across the bridge call, provider-level transaction concurrency is exactly one operation at a time.

## Privileged/runtime imports

The canonical 0.1.5 package records these unresolved symbols:

- `risc_fw_i2c_transact_v1`
- `vQueueDelete`
- `xQueueCreateMutex`
- `xQueueGenericSend`
- `xQueueSemaphoreTake`

The standalone validator rejects direct I2C/GPIO/RTC-GPIO/peripheral/T5/USB imports, unexpected imports, and any `__atomic*` or `__sync*` helper.

## Host validation

The current upstream host fixture uses pthreads and a host-only semaphore shim. The mocked firmware bridge intentionally permits overlap so the fixture can prove the provider itself serializes calls.

It covers ABI identity, start rules, address bounds, duplicate claims, size/buffer/timeout bounds, write/read/combined requests, bridge failures, two transaction threads queueing without backend overlap, `release_device` waiting for a blocked transaction, stale release rejection, clean quiescence, stop/restart, and monotonically increasing tokens.

This does not prove physical bus timing, actual FreeRTOS scheduling, board mutex behavior, or real-device operation.

## Standalone Xtensa validation

`scripts/build_i2c_esp32s3_v2.py` validates the exact 0.1.5 manifest and requires the migrated driver source files to be byte-identical to the historical v0.1.5 release source at commit `74d2417a0e9c88e6e0a8b71c8fc337d4d6d1da4e`. It replays the original `scripts/probe_i2c_esp32s3_v2.py` build through that commit's full `t5s3-pro` PlatformIO compilation database at the historical GitHub Actions workspace path, copies the resulting shared ELF into the local distribution directory, and removes the temporary historical checkout before later build steps.

The replay validates a 32-bit little-endian Xtensa ET_DYN image, sole function export `t5_driver_get`, the exact five-symbol runtime import set, and records produced size/SHA-256 plus build provenance against the canonical 24,496-byte release target. The `--require-byte-parity` option fails closed when canonical bytes are required. Until this replay runs successfully in repository CI, published-byte parity remains pending.

## Published package metadata

- `.package.json`: 588 bytes, SHA-256 `a0c7faf635a1b833fb66a658977efb88ab733cd13aae943c10067dabaaed0ca7`
- `driver.elf`: 24,496 bytes, SHA-256 `7b8f51f62da71e99949b093b6cdc531435a0cec01f87740921f9436544f8bd9c`
- `provider-abi.v1`: 36 bytes, SHA-256 `5b40fe49054c4e3e69ffab68aa4ae41be769e5a77928a8cdea5a666a37447f14`
- `privileged-imports.v1`: 93 bytes, SHA-256 `570d72eb68371d6105885edefdbb4b058f71b24a71adff81dac88820ffbbd564`

## Established limitations

This remains a firmware-backed transitional provider, not an independent I2C0 hardware owner. Host validation establishes provider semantics only. Canonical byte parity remains a separate reproducibility result until the independent build produces the published 24,496-byte ELF and SHA-256.

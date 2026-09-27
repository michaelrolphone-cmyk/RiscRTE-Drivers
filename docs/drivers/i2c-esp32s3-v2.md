# i2c-esp32s3-v2

## Purpose and scope

`i2c-esp32s3-v2` is the ABI-v2 provider for `i2c.bus@1` on the T5S3 Pro. The current implementation is explicitly transitional: it does not own or configure ESP32-S3 I2C0 itself. Instead, the verified driver ELF delegates each physical transaction to the firmware-owned compatibility entry point `risc_fw_i2c_transact_v1`, while exposing the stable provider-to-provider I2C contract to dependent drivers.

## Package identity

- Driver ID: `i2c-esp32s3-v2`
- Version: `0.1.3`
- Driver ABI: `2`
- Architecture: `xtensa-esp32s3`
- Board field: `t5s3-pro`
- Manifest status string: `experimental-unpublished`
- Provides: `i2c.bus@1`
- Declared provider dependencies: none
- Source path: `Drivers/i2c_esp32s3_v2`
- Current upstream source tree SHA: `9fa0ea80c39935a5c0cf8abb11595ebb695dab1b`
- Upstream release tag: `driver-i2c-esp32s3-v2-v0.1.3`
- Canonical released ELF: 12,376 bytes
- Canonical released ELF SHA-256: `ea7deb38c08ec154ba169cd1d01661031c6418d549d46b406ceb5abf155ddbca`

The manifest status string and the observed release-index publication state are recorded separately because the current source manifest still says `experimental-unpublished` while the release index publishes version 0.1.3.

## Migrated source, ABI, test, and linker files

- `Drivers/i2c_esp32s3_v2/driver.c`
- `Drivers/i2c_esp32s3_v2/manifest.json`
- `Drivers/i2c_esp32s3_v2/exports.map`
- `Drivers/i2c_esp32s3_v2/loader_sections.ld`
- `sdk/driver/RiscProviderV2.h`
- `sdk/driver/RiscI2cBusV1.h`
- `sdk/driver/RiscFirmwareI2cCompatV1.h`
- `test/drivers/i2c_esp32s3_v2_test.c`
- `scripts/build_i2c_esp32s3_v2.py`

The source, manifest, public I2C ABI, private firmware-compatibility ABI, provider ABI, linker controls, and host behavioral test are copied from the current upstream implementation. The standalone build script is repository-local tooling for independent compilation and validation.

## Provider ABI and exported entry point

The driver exports only `t5_driver_get(uint32_t abi)`. It returns the static `risc_driver_v2` descriptor only for `RISC_PROVIDER_DRIVER_ABI_V2` (2).

The descriptor identifies the driver as `i2c-esp32s3-v2`, advertises `i2c.bus` API 1, exposes one `risc_i2c_bus_api_v1` capability object, and supplies `start`, `stop`, and `quiesce` lifecycle callbacks.

The migrated `RiscProviderV2.h` also contains the optional append-only diagnostics extension used by newer ABI-v2 providers. This driver itself uses the base `risc_driver_v2` structure.

## I2C capability interface

`risc_i2c_bus_api_v1` contains `api_version`, `struct_size`, opaque `context`, and the `claim_device`, `transact`, and `release_device` callbacks.

The public ABI requires a single owner for each 7-bit address and treats a write-plus-read request as one serialized bus transaction. When a read follows a write, the public contract requires a repeated START without an intervening STOP. Buffers are borrowed only for the duration of the synchronous call.

## Device claims and tokens

The implementation has a fixed table of 12 claims and performs no allocation. Addresses below 0x08 or above 0x77 are rejected. A second claim of an already-owned address is rejected. If no claim slot is available, the request fails.

Successful claims receive a monotonically increasing nonzero 64-bit token. `next_token == UINT64_MAX` prevents creation of another token rather than wrapping and reusing one. Releasing a claim clears its table entry but does not decrement or recycle the token counter.

## Transaction validation and limits

A transaction is rejected before entering the firmware bridge when the provider is not started; the token is zero or stale; both phase lengths are zero; either phase exceeds 128 bytes; a nonzero phase has a null buffer; the timeout is zero; or the timeout exceeds 3,000 ms.

Those byte and timeout limits come from `RiscFirmwareI2cCompatV1.h`.

## Firmware delegation and physical ownership

The driver imports `risc_fw_i2c_transact_v1` as a private privileged compatibility symbol. Firmware remains the owner of the physical I2C0 controller, pins, Wire instance, and board I2C mutex.

The driver intentionally does not call `Wire.begin()`, install an IDF I2C driver, reconfigure GPIO, or expose a second hardware owner. The standalone validation requires the private bridge import and rejects direct unresolved imports whose names begin with I2C/GPIO/RTC-GPIO/peripheral-module/T5/USB prefixes.

A false result from the firmware bridge is returned directly as a failed transaction. The provider does not fabricate read data or claim that a write succeeded after a NACK or timeout.

## Concurrency and in-flight operations

Version 0.1.3 tracks active transactions with an atomic `in_flight` counter. This permits independent provider consumers on different tasks to overlap at the provider boundary while leaving physical serialization to the firmware-owned recursive board I2C mutex.

The provider increments `in_flight` before consulting the claim table, rechecks the started state, performs claim lookup and the private bridge call, then decrements the counter. Normal contention is therefore not converted into an immediate provider-level busy failure.

The upstream behavioral test deliberately re-enters `transact` from the mocked firmware bridge and proves that the nested call succeeds. During that in-flight period, both `quiesce` and `release_device` must fail.

## Lifecycle

`start` accepts no provider dependencies. It fails if a dependency count is supplied, if the provider is already started, if a transaction is still in flight, or if any claim remains in the table. It does not initialize physical I2C hardware; successful private symbol resolution establishes access to the firmware compatibility transport.

`release_device` refuses to mutate claim lifetime state while any transaction is in flight.

`quiesce` returns false while a transaction is active or any claim remains. When both are clear, it atomically marks the provider stopped. Because firmware retains physical ownership, quiescing the ELF does not shut down firmware I2C service used by touch, battery, or expander code.

`stop` calls `quiesce` and ignores its return value, so the runtime must honor the ABI-v2 quiescence contract and avoid unmapping a provider when `quiesce` reports false.

## Host behavioral validation

The migrated upstream test verifies ABI selection, descriptor/capability identity, dependency-free startup, duplicate-start rejection, valid and invalid 7-bit claims, duplicate-address rejection, unique claim tokens, stale-token rejection, 128-byte phase limits, required buffers, nonempty transactions, 1..3000 ms timeout bounds, read-only/write-only/combined transfers, bridge failure propagation, nested contention, release/quiesce refusal during an active transaction, restart behavior, and monotonically advancing tokens.

The host fixture mocks only the private firmware transaction ABI. It does not establish physical bus timing, pin ownership behavior, electrical operation, or real-device behavior.

## Standalone Xtensa build validation

`scripts/build_i2c_esp32s3_v2.py` builds an Xtensa shared ELF using the migrated version script and runtime-loader section layout. It validates exact source manifest identity/version, sole global export `t5_driver_get`, required private bridge import, absence of direct physical I2C/GPIO-class imports, ELF32 little-endian shared-object form for Xtensa machine 94, output size/SHA-256, and whether produced bytes match the canonical upstream release.

The release index also declares `.package.json`, `provider-abi.v1`, and `privileged-imports.v1` companions. The canonical `driver.elf` is 12,376 bytes with SHA-256 `ea7deb38c08ec154ba169cd1d01661031c6418d549d46b406ceb5abf155ddbca`.

## Established limitations

The source explicitly describes this as a transitional firmware-backed provider. It does not yet transfer physical I2C0 ownership out of firmware. The host test and standalone ELF validation therefore do not establish board-level timing or hardware operation. Destination CI run 36286792775 successfully built and tested this migration, but its minimal standalone build produced a 4,364-byte ELF with SHA-256 `ef767d28e438ad014352095da72dc9021887d213ad6bf6f301a7710c2342b736`, not the canonical 12,376-byte ELF. The upstream canonical path derives its C compilation command from the T5S3-Pro PlatformIO firmware compilation database before linking the adapter; the repository-local standalone builder currently uses a smaller self-contained compile configuration. Source/test/ABI/version parity is established, but canonical byte parity is not.

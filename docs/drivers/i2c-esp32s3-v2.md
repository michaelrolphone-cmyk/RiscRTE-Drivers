# i2c-esp32s3-v2

## Purpose and scope

`i2c-esp32s3-v2` is the ABI-v2 provider for `i2c.bus@1` on the T5S3 Pro. The implementation is explicitly transitional: the ELF does not own or configure ESP32-S3 I2C0 itself. It delegates physical transactions to the firmware-owned private compatibility function `risc_fw_i2c_transact_v1`, while exposing the stable provider-to-provider I2C capability to dependent drivers.

## Package identity

- Driver ID: `i2c-esp32s3-v2`
- Version: `0.1.4`
- Driver ABI: `2`
- Architecture: `xtensa-esp32s3`
- Board: `t5s3-pro`
- Manifest status string: `experimental-unpublished`
- Provides: `i2c.bus@1`
- Declared provider dependencies: none
- Source path: `Drivers/i2c_esp32s3_v2`
- Current upstream source tree SHA: `168aca8b76d177dd5be6c6b1299f50292f9a95f3`
- Upstream release tag: `driver-i2c-esp32s3-v2-v0.1.4`
- Canonical released ELF: 13,864 bytes
- Canonical released ELF SHA-256: `c8548cc7e72c32af3bb20e05fd17eda33d2b01b0072933b82edb8a78dca8b993`

The source manifest still carries the literal `experimental-unpublished` status while the release index publishes version 0.1.4. Those two source facts are recorded separately rather than treating the status string as authoritative release state.

## Source, ABI, linker, build, and test files

- `Drivers/i2c_esp32s3_v2/driver.c`
- `Drivers/i2c_esp32s3_v2/manifest.json`
- `Drivers/i2c_esp32s3_v2/exports.map`
- `Drivers/i2c_esp32s3_v2/loader_sections.ld`
- `sdk/driver/RiscProviderV2.h`
- `sdk/driver/RiscI2cBusV1.h`
- `sdk/driver/RiscFirmwareI2cCompatV1.h`
- `test/drivers/i2c_esp32s3_v2_test.c`
- `scripts/build_i2c_esp32s3_v2.py`

The implementation source and manifest are exact copies of upstream version 0.1.4. The ABI/linker files and host behavior fixture remain compatible with the current source. The repository-local build script independently compiles and validates the ELF and now also enforces the mutable-state section-placement invariant introduced by 0.1.4.

## Provider ABI and exported symbol

The only intended public ELF function is `t5_driver_get(uint32_t abi)`. It returns the static `risc_driver_v2` descriptor only when `abi == RISC_PROVIDER_DRIVER_ABI_V2` (2), otherwise null.

The descriptor identifies `i2c-esp32s3-v2`, advertises `i2c.bus` API 1, points at a static `risc_i2c_bus_api_v1`, and supplies `start`, `stop`, and `quiesce` lifecycle callbacks.

## I2C capability interface

`risc_i2c_bus_api_v1` contains `api_version`, `struct_size`, an opaque `context`, and three operations:

- `claim_device(context, address, out_token)`
- `transact(context, token, write_bytes, write_length, read_bytes, read_length, timeout_ms)`
- `release_device(context, token)`

The public contract uses one logical owner per 7-bit address. Combined write/read requests are submitted to the firmware bridge as a single synchronous transaction so the firmware transport controls repeated-START and bus serialization behavior.

## Fixed resources and claim model

The driver allocates no memory dynamically. It contains exactly 12 claim slots. Each slot stores a 64-bit token and 7-bit address.

Addresses below `0x08` or above `0x77` are rejected. Duplicate claims for an already-owned address fail. A claim also fails when all 12 slots are occupied, the output-token pointer is null, lifecycle state is not mutation-safe, or token generation has reached `UINT64_MAX`.

The token generator is monotonically increasing across stop/start cycles. Version 0.1.4 uses `UINT64_MAX` only as a link-time nonzero initializer to force `next_token` into `.data`; the first successful start converts that sentinel to zero. Subsequent restarts preserve the current token generation so stale handles are never made valid by resetting the counter.

## Version 0.1.4 alignment correction

Version 0.1.3 used separately zero-initialized atomic lifecycle/transaction variables in `.bss`. The upstream source records that the runtime image layout could place the atomic word at an unaligned address, producing an Xtensa `LoadStoreAlignment` fault when the first I2C transaction reached that state.

Version 0.1.4 replaces those variables with one 32-bit coordination word and explicitly places all mutable coordination/claim data in `.data`:

- `claims[12]` — `.data`
- `next_token` — `.data`
- `state` — `.data`, explicitly `aligned(4)`

The nonzero initializers are deliberate placement sentinels. Upstream's probe script verifies the final linked symbols `state`, `claims`, and `next_token` are data symbols; the migrated standalone build performs the same invariant check.

## Lifecycle/transaction state machine

The 32-bit `state` word uses these masks:

- `STATE_STOPPED = 0x40000000`
- `STATE_STARTED = 0x80000000`
- `STATE_MUTATING = 0x20000000`
- `STATE_COUNT_MASK = 0x0000ffff`

The low 16 bits count concurrent `transact` calls. `STATE_MUTATING` excludes claim-table/lifecycle mutations while transactions are active.

`begin_transaction` accepts only a state with STARTED set and MUTATING clear, then atomically increments the transaction count. It rejects a count of 65,535 rather than overflowing. Multiple or nested transactions can therefore be active at the provider boundary; the firmware-owned board I2C mutex remains responsible for physical Wire serialization.

`begin_mutation` succeeds only by changing the exact idle `STATE_STARTED` value to `STATE_STARTED | STATE_MUTATING`. Claims, releases, and quiescence therefore cannot mutate claim/lifecycle state while any transaction is active or another mutation is underway.

## Start, quiesce, stop, and restart

`start` requires a zero dependency count and performs a compare/exchange from `STATE_STOPPED` to `STATE_MUTATING`. It then clears all claim entries, initializes `next_token` from its link-time sentinel on the first start only, and publishes `STATE_STARTED`.

A second start without a successful quiesce fails because the state is no longer STOPPED.

`quiesce` first acquires mutation ownership. If any claim remains, it restores idle STARTED state and returns false. With no claims, it compare/exchanges `STATE_STARTED | STATE_MUTATING` to `STATE_STOPPED`. Because transactions prevent mutation ownership, quiescence also cannot succeed while a transaction is active.

`stop` calls `quiesce` and ignores its return value. The runtime must therefore honor the ABI-v2 quiescence contract before unloading the provider.

## Transaction validation and bounds

A transaction fails before the firmware bridge when:

- the token is zero;
- both write and read lengths are zero;
- either phase exceeds `RISC_FW_I2C_COMPAT_V1_MAX_BYTES` (128 bytes);
- a nonzero phase has a null buffer;
- timeout is zero;
- timeout exceeds `RISC_FW_I2C_COMPAT_V1_MAX_TIMEOUT_MS` (3,000 ms);
- the provider cannot enter transaction state;
- the token is not present in the claim table.

Once admitted, the implementation finds the claimed address, calls `risc_fw_i2c_transact_v1`, then decrements the active transaction count. A NACK, timeout, or other false return from the firmware bridge is propagated as failure; the provider does not fabricate success or read data.

## Firmware transport and physical ownership

The only privileged hardware-facing import permitted by this driver is `risc_fw_i2c_transact_v1`. Firmware remains the owner of the physical I2C0 controller, GPIO/pin configuration, Wire instance, and recursive board I2C mutex.

The source does not call `Wire.begin()`, install a separate IDF I2C driver, or configure GPIO. The standalone build rejects direct unresolved imports with I2C/GPIO/RTC-GPIO/peripheral/T5/USB prefixes and rejects any unexpected unresolved symbol besides the private bridge.

## Host behavioral validation

The migrated host fixture mocks only `risc_fw_i2c_transact_v1`. It verifies ABI selection and identity, dependency-free start, duplicate-start rejection, valid and invalid addresses, duplicate claims, unique/stale tokens, write/read bounds, required buffers, timeout bounds, write-only/read-only/combined operations, bridge failure propagation, nested transaction admission, release/quiesce refusal while a transaction is active, release semantics, stop/restart behavior, and monotonic token generation.

The fixture does not establish electrical timing, hardware pin ownership, real-device behavior, or firmware mutex correctness.

## Standalone Xtensa validation

`scripts/build_i2c_esp32s3_v2.py` builds an Xtensa ESP32-S3 shared ELF using the migrated version script and loader section layout. It validates:

- exact manifest identity/version 0.1.4;
- sole public function export `t5_driver_get`;
- presence of the required private firmware bridge import;
- absence of other unresolved imports;
- absence of direct physical I2C/GPIO-class imports;
- `state`, `claims`, and `next_token` as linked `.data` symbols;
- ELF32 little-endian shared-object format for Xtensa machine type 94;
- produced size and SHA-256;
- comparison with the canonical 0.1.4 release bytes.

The canonical release is 13,864 bytes with SHA-256 `c8548cc7e72c32af3bb20e05fd17eda33d2b01b0072933b82edb8a78dca8b993`. CI run 36288732077 passes this validator and produces an independent 4,636-byte ELF with SHA-256 `4fe8a1c3d7f6dc5c34802d8842f49b05c89372ab75ec847cf0f5994ae7e75a7f`, with `byte_parity=False`. The `.data` placement check passes. The repository's minimal standalone compile configuration therefore validates the 0.1.4 ABI/source/state-layout invariants but still does not reproduce the firmware-compilation-database-derived canonical bytes.

## Published package metadata

The inspected 0.1.4 release-index package declares:

- `.package.json`: 588 bytes, SHA-256 `e7d8d415350b5fea9b4a81acee6e3a0ea689aed361b65bb99403242e5900a46e`
- `driver.elf`: 13,864 bytes, SHA-256 `c8548cc7e72c32af3bb20e05fd17eda33d2b01b0072933b82edb8a78dca8b993`
- `provider-abi.v1`: 36 bytes, SHA-256 `5b40fe49054c4e3e69ffab68aa4ae41be769e5a77928a8cdea5a666a37447f14`
- `privileged-imports.v1`: 24 bytes, SHA-256 `8b61e76d03eee30103179a37f061fcab06604372d7598b78e53b875fba516814`

## Established limitations

This remains a transitional firmware-backed I2C provider rather than an independent owner of I2C0. Source/test parity and release metadata establish the provider contract and the 0.1.4 alignment correction, but host validation does not prove physical bus behavior. CI run 36288732077 confirms parity metadata, documentation, host behavior, Xtensa linkage, privileged-import restrictions, and the 0.1.4 aligned mutable-state invariant. Independent reproduction of the canonical release bytes remains a separate build-reproducibility question because the upstream canonical path derives its C compilation command from the T5S3-Pro PlatformIO firmware compilation database.

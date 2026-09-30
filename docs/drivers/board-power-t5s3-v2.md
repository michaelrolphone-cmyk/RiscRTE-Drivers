# board-power-t5s3-v2

## Identity and scope

`board-power-t5s3-v2` is the reusable BQ25896-backed `board.power.vbus@1` provider. Current upstream source and release metadata are version **0.1.6**, driver ABI **2**, architecture **xtensa-esp32s3**, executable `driver.elf`. The package requires `i2c.bus@1`, `platform.clock@1`, and `board.power.bq25896.profile@1`.

The implementation lives in `Drivers/bq25896/driver.c`. It owns BQ25896 register access and source/external-power qualification; board electrical policy is supplied by a separate immutable profile provider. The retained package ID is historical and does not make the chip implementation T5S3-specific.

Current upstream source tree: `17c99241a5606380a3ce5755e35f2f4a28b1ab1a`.

## Release package

Upstream release tag: `driver-board-power-t5s3-v2-v0.1.6`.

| File | Size | SHA-256 |
| --- | ---: | --- |
| `.package.json` | 730 | `5c2ce33fd2ec3b162f2cf24a9990824b013334fb5f88a3e2f36abfb93f03ec82` |
| `driver.elf` | 12,660 | `5f50b5eb048085e3128939b6bed3693a9a44ebe27c9e2cae3b40dd725aa6a4c4` |
| `provider-abi.v1` | 45 | `6c45893d4c9b14d7dfbc8f6b1008fc6e1758f78a45e8a53e7c2bbc8f4c58a67c` |
| `privileged-imports.v1` | 19 | `fe6240d8e0702eb2fb282f9dc78519ccef9f86bf78f01fb02978b3c392796a33` |

The release index establishes the privileged-import sidecar size/hash; this page does not infer a symbol list from that hash.

## Source, ABI, and build inputs

Relevant migrated files are:

- `Drivers/bq25896/driver.c`
- `Drivers/bq25896/manifest.json`
- `sdk/driver/RiscUsbVbusV1.h`
- `sdk/driver/RiscBq25896ProfileV1.h`
- `sdk/driver/RiscI2cBusV1.h`
- `sdk/driver/RiscPlatformClockV1.h`
- `sdk/driver/RiscProviderV2.h`
- `test/drivers/board_power_t5s3_v2_test.c`
- `scripts/build_board_power_t5s3_v2.py`

The standalone builder uses the Xtensa ESP32-S3 GCC toolchain, emits a freestanding PIC shared ELF, verifies ELF32 little-endian Xtensa `ET_DYN`, requires the sole public function export `t5_driver_get`, and fails if the produced bytes do not match the canonical v0.1.6 size/hash above.

## Root and provided interface

The exported root is:

`const risc_driver_v2 *t5_driver_get(uint32_t abi)`

It returns the static driver only for `RISC_PROVIDER_DRIVER_ABI_V2`. The driver root exposes start, stop, and driver-level quiesce callbacks.

The capability object is `risc_usb_vbus_external_api_v1`. It is prefix-compatible with:

1. `risc_usb_vbus_api_v1`: `acquire_host`, `release_host`, `quiesce`.
2. `risc_usb_vbus_monitor_api_v1`: `input_status` and flags.
3. The optional external-host suffix: `acquire_external_host` and `external_host_valid`.

`RISC_USB_POWER_IDLE_PROBE_REQUIRED` is always advertised. `RISC_USB_POWER_EXTERNAL_HOST_SUPPORTED` is advertised only when the installed profile is large enough for `risc_bq25896_external_profile_v1` and sets `RISC_BQ25896_EXTERNAL_HOST`. Older profiles retain source-only behavior.

## Profile validation

The provider copies and validates the profile before claiming hardware. Verified checks include:

- profile API version 1 and base structure size;
- nonzero `max_host_milliamps` not exceeding the boost current limit;
- boost voltage 4550–5510 mV on an exact 64 mV chip step;
- boost current limit exactly one of 500, 750, 1200, 1400, 1650, 1875, or 2150 mA;
- boost settle time 1–500 ms;
- input settle time 220–5000 ms;
- transient recovery window at most 500 ms;
- with recovery enabled, stable interval 200–500 ms; with recovery disabled, stable interval zero.

The external-host flag is additive and size checked. A legacy base profile is accepted but cannot authorize externally powered host operation.

## Hardware matching and transport

The BQ25896 address is fixed at **0x6B**. All register traffic uses the admitted `i2c.bus@1` provider with a **100 ms** transaction timeout. The driver claims the device once during start and retains the claim until quiescence.

After claiming, it reads register `0x14` and accepts only the part-number field expected by the implementation; an ACK at 0x6B alone is not treated as identity.

Registers explicitly used by the source include ADC control `0x02`, power control `0x03`, boost configuration `0x0A`, status `0x0B`, fault `0x0C`, battery ADC `0x0E`, VBUS ADC `0x11`, and identification `0x14`.

## Start and dependency ownership

`start` requires exactly three dependencies and rejects duplicate, unknown, missing, wrong-version, or undersized dependencies. Required callbacks are the I2C claim/transact/release operations and platform-clock monotonic/sleep operations. A chip-probe failure occurs after the I2C claim; cleanup remains the provider's responsibility and is completed through quiescence.

The implementation uses static state. Its source explicitly requires calls to be serialized by the generic provider executor; arbitrary concurrent-call safety is not established.

## Battery-sourced host lease

`acquire_host` rejects invalid state, an existing lease, zero or over-budget current, token exhaustion, clock failure, external-input evidence, unowned OTG state, or a live boost fault. False clears the caller's output token.

Before writes, the driver snapshots power, boost, and ADC-control registers. It creates the lease before potentially state-changing writes so uncertain partial writes pin ownership. It programs the profile's boost settings, enables continuous ADC conversion, disables charging for the source transition, and enables OTG.

Source verification is bounded by **1500 ms**. It requires OTG still enabled, VBUS status reporting OTG, no live boost fault, and an adequate completed ADC sample. A profile may permit one cleared latched startup fault inside its configured transient window; that path still requires a clean stable interval and fresh adequate ADC result. Live or repeated faults fail.

Rollback and source-off verification are fail-closed. Source-off verification is bounded by **400 ms**. If rollback cannot prove a safe restored state, the lease and faulted ownership remain pinned for explicit retry.

## Externally powered host lease

Version 0.1.6 adds an optional externally powered host path. It never treats external VBUS as permission to enable battery boost.

`acquire_external_host` is available only when the profile explicitly opts in. It rejects active/faulted/leased state, unsupported profile policy, invalid current, active OTG, missing power-good evidence, or clock/read failures. It snapshots power, boost, and ADC state, enables continuous ADC conversion, and waits for fresh conversion time using twenty 50 ms scheduler sleeps while still bounded by the normal startup deadline.

The external voltage check requires all of the following from the source implementation:

- OTG enable is clear;
- status is not OTG;
- `POWER_GOOD` is set;
- the VBUS-good ADC bit is set;
- the 7-bit VBUS ADC code is 22 through 26 inclusive, documented by the source as a conservative **4.8–5.2 V** admission range at 100 mV steps;
- no live fault bit in mask `0x78`.

On success the provider returns a normal lease token and records it as an external lease. It does not enable boost or disable charging. `external_host_valid` rechecks the active external lease and the same electrical qualification.

A failed external acquisition attempts restoration. If that rollback itself cannot be proven, the provider stays faulted and retains ownership. `input_status` may retry recovery only for a failed external acquisition that never granted a live external-host lease; it does not silently recover a granted live host behind its owner.

## Input monitoring and power-mode transitions

`input_status` reports UNKNOWN when state or hardware reads are uncertain, SOURCE for the provider's own active output, SETTLING during the configured source-off observation interval, EXTERNAL for qualified incoming power, and ABSENT otherwise.

Version 0.1.6 additionally detects qualified external input replacing the provider's source while the source request is still recorded. In that case it reports EXTERNAL so the controller can disconnect and drain before changing power modes rather than treating stale OTG configuration as authority to keep sourcing.

## Release, quiescence, and failure handling

`release_host` accepts only the active lease, whether source or external. It runs the same restore/readback path. Success clears the lease and external-lease flags; failure retains faulted ownership.

Provider quiescence refuses while a lease or pending source request exists. Once safe, it releases the I2C claim. A failed lower-level release keeps the provider pinned. `stop` clears dependency pointers only after the resource state is clean.

The implementation consistently treats unknown state as failure, not permission to source. It does not claim support for alternate PMICs, alternate BQ25896 addresses, unspecified board rail switches, or unsynchronized concurrent access.

## Tests and validation

The synchronized upstream host fixture `test/drivers/board_power_t5s3_v2_test.c` covers the prior source/rollback/fault/profile cases and adds v0.1.6 coverage for:

- source output being replaced by incoming external power;
- successful external-host acquisition while preserving power/boost settings;
- external lease validation;
- rejection of simultaneous source or second external leases;
- over-voltage and drooping external rails;
- live charging-fault rejection;
- external-power disappearance;
- ADC-write failure and rollback behavior;
- retained ownership when rollback is uncertain;
- legacy profiles not advertising or authorizing external-host operation;
- continued reuse with another synthetic base profile.

Physical Qi/controller operation is not established by the host fixture. The upstream documentation explicitly leaves receiver enumeration, Qi current capacity, and charging-pad interference as device-observation questions.

At this migration stage, the v0.1.6 source, ABI, host fixture, canonical metadata, and fail-closed builder are synchronized. The released-driver manifest remains `migrated: false` until destination CI executes these tests and reproduces the 12,660-byte canonical ELF.

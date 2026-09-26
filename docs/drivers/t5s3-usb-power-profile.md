# t5s3-usb-power-profile

## Purpose

`t5s3-usb-power-profile` is a board-specific immutable policy provider for the T5S3 USB/BQ25896 power path. It provides the `board.power.bq25896.profile` capability as configuration data consumed by the BQ25896 power driver.

This provider does not own I2C, does not probe the BQ25896, and does not write charger registers.

## Package identity

- Package/driver ID: `t5s3-usb-power-profile`
- Version: `0.1.0`
- Driver ABI: `2`
- Architecture: `xtensa-esp32s3`
- Board: `t5s3-pro`
- ELF filename: `driver.elf`
- Provided capability: `board.power.bq25896.profile`
- Capability API: `1`
- Required capabilities: none
- Source manifest status: `experimental-unpublished`

Source files in this repository:

- `Drivers/t5s3_usb_power_profile/driver.c`
- `Drivers/t5s3_usb_power_profile/manifest.json`
- `sdk/driver/RiscBq25896ProfileV1.h`
- `sdk/driver/RiscProviderV2.h`
- `scripts/build_t5s3_usb_power_profile.py`

## Provider ABI and exported symbol

The ELF exports only:

`t5_driver_get(uint32_t abi)`

It returns the static `risc_driver_v2` descriptor only for `RISC_PROVIDER_DRIVER_ABI_V2`.

The provider descriptor publishes the `board.power.bq25896.profile` capability and points to a static `risc_bq25896_profile_api_v1` structure.

## Capability interface

`risc_bq25896_profile_api_v1` contains immutable configuration fields:

- `api_version`
- `struct_size`
- `max_host_milliamps`
- `boost_millivolts`
- `boost_limit_milliamps`
- `boost_settle_ms`
- `input_settle_ms`
- `transient_window_ms`
- `transient_stable_ms`

The interface header documents this as installed electrical policy for a BQ25896 wired directly to USB VBUS. The BQ25896 driver is expected to copy and validate this profile before claiming hardware.

## T5S3 profile values

The current driver publishes:

| Field | Value |
| --- | ---: |
| `max_host_milliamps` | 500 mA |
| `boost_millivolts` | 5126 mV |
| `boost_limit_milliamps` | 1200 mA |
| `boost_settle_ms` | 80 ms |
| `input_settle_ms` | 500 ms |
| `transient_window_ms` | 250 ms |
| `transient_stable_ms` | 200 ms |

The header notes that `max_host_milliamps` is consumer admission policy, not an inrush-current limit. It also states that the boost voltage corresponds to an exact BQ25896 voltage step and that the boost-current field is constrained to supported chip current values.

The transient fields permit at most one cleared latched startup fault within the configured recovery policy. The interface comments state that live or repeated faults still fail.

## Lifecycle

### start

`start` accepts activation only when zero dependencies are supplied. It does not probe hardware or allocate resources.

### quiesce

`quiesce` always returns `true` because the provider contains immutable policy data and maintains no active hardware session.

### stop

`stop` performs no work.

## Hardware and dependency model

The provider itself:

- does not access I2C;
- does not own a BQ25896 instance;
- does not configure VBUS;
- does not perform register reads/writes;
- does not claim that every T5S3-like CPU/board wiring matches this profile.

The interface header explicitly says that a board requiring additional rail or OTG-pin switching needs a composed power provider; this profile alone does not assert support for such wiring.

The reusable `board-power-t5s3-v2`/BQ25896 provider consumes this profile together with `i2c.bus` and `platform.clock`.

## Build and validation

`scripts/build_t5s3_usb_power_profile.py` builds the source with the Xtensa ESP32-S3 GCC toolchain as a C11 PIC shared ELF using `-Os`, `-mtext-section-literals`, `-mlongcalls`, hidden default visibility, no standard startup files, and SysV hash style.

The build verifies:

- package identity/version;
- the only exported function is `t5_driver_get`;
- the output is an ELF32 Xtensa binary;
- output size and SHA-256 are recorded in the staged manifest.

## Published artifact parity

The T5S3-Reader release index currently records:

- Release tag: `driver-t5s3-usb-power-profile-v0.1.0`
- Asset: `t5s3-usb-power-profile--driver.elf`
- Published size: `2,356` bytes
- Published SHA-256: `f4512fabd137cc1e6ecd8bd5e09f59829a02600368b35d4f4011ade722647e74`

The standalone RiscRTE-Drivers build has reproduced that published size and SHA-256.

The published package metadata also records `.package.json`, `provider-abi.v1`, and `privileged-imports.v1`. The published privileged-imports metadata is effectively empty for this profile, consistent with the implementation containing only immutable data and generic provider lifecycle code.

## Confirmed limits and implementation status

- This is configuration data, not the BQ25896 hardware driver.
- It publishes exactly one board-specific profile.
- It has no runtime mutable state.
- It has no probing, discovery, I/O, interrupts, DMA, tasks, callbacks, or transport ownership.
- The manifest labels the source package `experimental-unpublished`; separately, T5S3-Reader has published a release artifact for version 0.1.0.

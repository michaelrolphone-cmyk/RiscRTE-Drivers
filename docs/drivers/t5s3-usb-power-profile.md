# t5s3-usb-power-profile

## Purpose and identity

`t5s3-usb-power-profile` is the immutable T5S3 electrical-policy provider consumed by the reusable BQ25896 power driver. Current upstream source and release metadata are version **0.1.1**, driver ABI **2**, architecture **xtensa-esp32s3**, executable `driver.elf`, board metadata `t5s3-pro`.

It requires no capabilities and provides `board.power.bq25896.profile@1`. It does not own I2C, probe the charger, change registers, allocate transport resources, or arbitrate USB itself.

Current upstream source tree: `1ba351fb2ae0efae1d1b100087f93805d77e50f4`.

## Release package

Upstream release tag: `driver-t5s3-usb-power-profile-v0.1.1`.

| File | Size | SHA-256 |
| --- | ---: | --- |
| `.package.json` | 594 | `9e99d08372bcdd6eb03467e5fe00ff7de3bdd9ff2e0ed33b2be186af32f782db` |
| `driver.elf` | 2,360 | `e1a61504f63be342a13afdeaccee637b2cba657ed192a20260bbf15041ae52ff` |
| `provider-abi.v1` | 56 | `ce73bd38038a7c183bcd3d4f980f34c92eeb04bd66008391b4c454c7a6674cb5` |
| `privileged-imports.v1` | 1 | `01ba4719c80b6fe911b091a7c05124b64eeece964e09c058ef8f9805daca546b` |

## Source, build, and root ABI

Relevant files are `Drivers/t5s3_usb_power_profile/driver.c`, its manifest, `sdk/driver/RiscBq25896ProfileV1.h`, `sdk/driver/RiscProviderV2.h`, and `scripts/build_t5s3_usb_power_profile.py`.

The standalone builder compiles a C11 PIC Xtensa ESP32-S3 shared ELF, verifies the sole public function export `t5_driver_get`, verifies ELF32 Xtensa identity, and fails unless output matches the canonical size/hash above.

`t5_driver_get(uint32_t abi)` returns the static driver only for `RISC_PROVIDER_DRIVER_ABI_V2`. `start` accepts zero dependencies, `quiesce` always returns true, and `stop` has no hardware work. The provider owns no bus claim, DMA, task, interrupt, callback, or mutable hardware session.

## Base profile values

The `risc_bq25896_profile_api_v1` base contains API/version size fields, consumer current limit, boost voltage/current, source/input settling, and transient-recovery timing. Current values are:

| Field | Value |
| --- | ---: |
| `max_host_milliamps` | 500 mA |
| `boost_millivolts` | 5126 mV |
| `boost_limit_milliamps` | 1200 mA |
| `boost_settle_ms` | 80 ms |
| `input_settle_ms` | 500 ms |
| `transient_window_ms` | 250 ms |
| `transient_stable_ms` | 200 ms |

The consumer budget is not an inrush-current threshold; the profile preserves the 1200 mA boost-current policy used by the current T5S3 implementation.

## Version 0.1.1 external-host suffix

Version 0.1.1 publishes a `risc_bq25896_external_profile_v1` object whose first member is the complete base profile and whose `flags` field is `RISC_BQ25896_EXTERNAL_HOST`. The base `struct_size` is the full extended-structure size, so consumers discover the suffix by size before reading it. Older consumers can continue reading the base prefix.

The header defines this flag as an opt-in board-wiring assertion that externally supplied charger VBUS also reaches the USB connector without an additional switch. It explicitly does not identify Qi versus a computer.

The flag is immutable policy only. It does not enable host mode, source VBUS, change charging state, or validate voltage. Those operations belong to the BQ25896 power provider and USB controller.

## Scope and compatibility

This implementation establishes exactly one T5S3 profile. It does not establish compatibility with another board merely because that board contains an ESP32-S3 or BQ25896. Boards needing additional rail or OTG-pin switching require a composed provider; this profile does not claim such wiring support.

The profile has no hardware identifiers or probing. Manifest field `board: "t5s3-pro"` is descriptive package metadata rather than an electrical run-time match.

For the current external-power host feature, the synchronized package set is `board-power-t5s3-v2` 0.1.6, this profile 0.1.1, and `usb-controller-esp32s3` 0.1.19. Legacy profiles without the suffix retain the original source-only behavior.

## Tests and limitations

The synchronized `test/drivers/board_power_t5s3_v2_test.c` fixture uses this real profile and verifies its external-host opt-in together with the BQ25896 provider, including qualified and invalid external rails and a legacy profile that does not advertise the extension.

The host fixture does not establish physical receiver enumeration, Qi current capacity, or charging-pad interference behavior. Those remain device-observation limits explicitly called out upstream.

At this migration stage the exact v0.1.1 source, ABI suffix, release metadata, and strict canonical builder are synchronized. The released-driver manifest remains `migrated: false` until destination CI reproduces the 2,360-byte canonical ELF and passes the synchronized host test.

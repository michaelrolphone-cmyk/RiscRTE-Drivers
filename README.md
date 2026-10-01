# RiscRTE-Drivers

Independent source, build, test, release, versioning, and implementation documentation repository for RiscRTE runtime drivers.

## Repository scope

Until source cutover is complete, `michaelrolphone-cmyk/T5S3-Reader` is the read-only upstream parity source. This repository must never write to, branch, or otherwise modify T5S3-Reader.

The target state is:

- every RiscRTE driver found in the upstream source tree is maintained here;
- each migrated driver builds independently from source in this repository;
- CI validates source/release inventory parity, package metadata, ELF ABI/architecture, build reproducibility where established, and required driver documentation;
- releases are versioned per driver;
- `manifest/released-drivers.json` records published-driver versions plus source-only driver inventory;
- `manifest/source-trees.json` records the inspected upstream driver tree baseline;
- ongoing upstream changes are synchronized here until official source cutover.

Parity maintenance uses reviewable branches and PRs. The owner authorizes merging validated in-scope external parity work after exact-head CI passes.

## Layout

```text
Drivers/                  migrated driver implementation sources/manifests
sdk/driver/               capability/provider ABI headers required by migrated drivers
scripts/                  standalone build, parity, validation, and release tooling
manifest/
  released-drivers.json   released/source-only driver inventory and migration state
  source-trees.json       upstream driver tree SHA baseline
docs/drivers/             one implementation document per migrated driver
.github/workflows/        CI parity/build/documentation checks
```

## Migration and parity model

A driver is not considered fully migrated solely because its manifest or source file exists here. A completed migration requires the implementation source and required ABI headers, an independent build path, relevant validation/tests, matching published version and artifact bytes where upstream publication provides a canonical artifact, a dedicated implementation document, and a README link to that document.

`scripts/check_parity.py` resolves source master and release-index once, then
reads every upstream tree/manifest at those immutable commits. It compares
actual local driver bytes and executable modes with both the recorded tree
baseline and that source snapshot. Changed, missing, added or untracked local
driver files cannot pass merely because the inventory still records old hashes.
Upstream-only, external-only, conflict and converged changes are reported for
review; the check never overwrites external improvements. API/manifest failures
fail closed instead of silently dropping an upstream driver.

The source and released inventory baselines must agree; unrelated master changes
remain informational when all driver trees/versions match. Released and source
versions remain distinct where explicitly recorded. Documentation is checked
separately by `scripts/check_driver_docs.py`.

See [source-aware validation](docs/SOURCE_PARITY_VALIDATION.md) for offline
commands, negative tests, provenance artifacts and exact scope limits.

## Build and test model

Current driver builds use the Xtensa ESP32-S3 GCC toolchain provisioned through PlatformIO. Individual build scripts validate the package identity and driver-specific output, including exported symbols and ELF architecture where implemented. CI runs parity/documentation checks separately from compilation so inventory drift cannot be hidden by a successful compiler invocation.

Published-byte parity is recorded only where it has actually been demonstrated. Source-only drivers have no published artifact to compare against.

## Driver documentation tree

Current upstream power/USB release synchronization has staged versions `board-power-t5s3-v2` 0.1.6, `t5s3-usb-power-profile` 0.1.1, and `usb-controller-esp32s3` 0.1.19. Their pages are indexed below, but their released-driver entries remain `migrated: false` until destination CI proves the synchronized host fixtures and canonical artifact bytes.


### Driver pages

- [display-epd-video](docs/drivers/display-epd-video.md) — EPD video bridge to the `display.output@1` provider, release 0.1.3; host lifecycle fixture passes and the independent build reproduces the published 5,852-byte ELF digest.
- [gt911-touch](docs/drivers/gt911-touch.md) — serialized GT911 `input.touch.raw@1` provider, version 0.1.1; dedicated CI run `36619949165` passed the host concurrency/retry/GAP/lifecycle fixture and strict historical replay, reproducing the published 42,976-byte ELF SHA-256 `44d753b736a2a433549ab500a3cae52f1e2844f79332fd119fc8df8d57cd11f4` with `byte_parity=True`.
- [gps-nmea](docs/drivers/gps-nmea.md) — allocation-free NMEA 0183 GGA/RMC GNSS provider over runtime serial/power/clock host services, version 1.0.0 (source-only upstream).
- [i2c-esp32s3-v2](docs/drivers/i2c-esp32s3-v2.md) — firmware-backed `i2c.bus@1` provider, version 0.1.6; strict CI run `36545152857` passed the fail-closed historical replay against the published 24,960-byte ELF SHA-256 `0230f71ca21340165c59cba89e18e30ba169671c714dd1098ed5b4994f90fc34` and the updated deadline host fixture passed immediately before it.
- [usb-cdc-acm](docs/drivers/usb-cdc-acm.md) — allocation-free USB CDC ACM descriptor/protocol provider for binding discovery and class-request encoding, version 0.1.0 (source-only upstream).
- [usb-cdc-acm-v2](docs/drivers/usb-cdc-acm-v2.md) — USB CDC ACM `serial.port@1` provider above `usb.host@1`, version 0.1.0; CI run `36447591163` reproduced the canonical 6,584-byte ELF byte-for-byte.
- [usb-ch34x-v2](docs/drivers/usb-ch34x-v2.md) — WCH-compatible CH34x `serial.port@1` provider above `usb.host@1`, version 0.1.0; validates one unambiguous vendor interface, performs CH34x vendor initialization/framing requests, and routes bounded bulk I/O through the host claim.
- [usb-cp210x-v2](docs/drivers/usb-cp210x-v2.md) — Silicon Labs CP210x `serial.port@1` provider above `usb.host@1`, version 0.1.0; CI run `36447591163` reproduced the canonical 5,788-byte ELF byte-for-byte.
- [usb-ftdi](docs/drivers/usb-ftdi.md) — single-port FTDI `serial.port@1` provider above `usb.host@1`, version 0.1.0; validates supported VID/PID/device generations, programs FTDI baud/line state, strips per-packet status bytes, and preserves buffered payload.
- [usb-hid](docs/drivers/usb-hid.md) — generic USB HID interface provider above the interrupt-capable `usb.host@1` extension, version 0.1.2; parses bounded composite HID descriptors, owns interface claims, fetches report descriptors, selects boot/report protocol, and forwards interrupt-IN reports.
- [usb-hid-gamepad](docs/drivers/usb-hid-gamepad.md) — descriptor-driven `usb.hid.gamepad@1` class provider over `usb.hid@1` + `platform.clock@1`, version 0.1.4; CI run `36673309318` passed the synchronized host fixture and reproduced the published 14,444-byte ELF SHA-256 `2d543f6a04e5b94192f732c64f2f5a436861e2a569f05fb4c6617ae7267db86c` with `byte_parity=True`.
- [usb-hid-keyboard](docs/drivers/usb-hid-keyboard.md) — boot-protocol keyboard class provider above `usb.hid@1`, version 0.1.1; tracks four keyboards, copied ordered subscriber events, snapshots, overflow gaps, and quiescent HID-session release.
- [usb-hid-text-input](docs/drivers/usb-hid-text-input.md) — USB HID keyboard to transport-neutral `input.text@1` translator, version 0.1.0; CI run `36447591163` reproduced the canonical 8,692-byte ELF byte-for-byte.
- [usb-host-v2](docs/drivers/usb-host-v2.md) — generation-safe `usb.host@1` provider above `usb.controller@1`, version 0.1.3; validates descriptor-derived bulk/interrupt endpoint access, quarantines failed releases, forwards optional diagnostics, and requires canonical published-byte parity from its standalone Xtensa build.
- [usb-mass-storage](docs/drivers/usb-mass-storage.md) — USB MSC BOT/SCSI `storage.volume@1` provider with FAT16/FAT32 filesystem handling, version 0.1.1; CI run `36352310880` passed the exact upstream FAT16 host fixture and reproduced the canonical 23,792-byte ELF byte-for-byte.
- [usb-stlink](docs/drivers/usb-stlink.md) — ST-LINK V2/V2.1/V3 `debug.vendor.stlink@1` provider above `usb.host@1` and `platform.clock@1`, version 0.1.0; CI run `36344579403` passed the exact host fixture and reproduced the canonical 9,304-byte ELF byte-for-byte.
- [usb-ui-navigation](docs/drivers/usb-ui-navigation.md) — composite `input.navigation@1` provider over semantic text, HID gamepad, and XInput gamepad sources, version 0.1.2; preserves foreground handoff and neutral rearm semantics.
- [usb-xinput-gamepad](docs/drivers/usb-xinput-gamepad.md) — Xbox 360 wired/wireless-format `usb.xinput.gamepad@1` provider above `usb.host@1` and `platform.clock@1`, version 0.1.3; CI run `36345187504` passed the exact upstream host fixture and reproduced the canonical 11,088-byte ELF byte-for-byte.
- [board-power-t5s3-v2](docs/drivers/board-power-t5s3-v2.md) — BQ25896 `board.power.vbus@1` provider, upstream release 0.1.6; exact source/ABI/test bytes and canonical 12,660-byte target are synchronized, including the opt-in externally powered host lease. Destination CI/canonical replay is pending before this release is marked migrated.
- [usb-msp](docs/drivers/usb-msp.md) — TI MSP-FET/eZ-FET `debug.vendor.msp@1` transport, version 0.1.1; CI run `36378453595` passed the exact upstream fixture and reproduced the canonical 14,116-byte ELF byte-for-byte.
- [platform-clock-v1](docs/drivers/platform-clock-v1.md) — generic `platform.clock@1` monotonic-time and sleep provider, version 0.1.0.
- [program-msp](docs/drivers/program-msp.md) — MSP430FR/XV2 FRAM programming provider above `debug.vendor.msp@1`, version 0.1.0; CI run `36447591163` reproduced the canonical 9,128-byte ELF byte-for-byte.
- [t5s3-usb-power-profile](docs/drivers/t5s3-usb-power-profile.md) — immutable T5S3 `board.power.bq25896.profile@1` provider, upstream release 0.1.1; exact source and external-host profile suffix are synchronized with a fail-closed 2,360-byte canonical build target. Destination CI is pending before this release is marked migrated.
- [usb-controller-esp32s3](docs/drivers/usb-controller-esp32s3.md) — ESP32-S3 `usb.controller@1` provider, upstream release 0.1.19; exact source/tests and externally powered host role switching are synchronized with canonical 789,504-byte replay metadata. Destination host tests/replay/loader audit are pending before this release is marked migrated.

## Documentation standard

Each migrated driver's `docs/drivers/<driver-id>.md` page is derived from its current implementation source, manifest, ABI/interface headers, build tooling, tests, and observed release metadata. Documentation describes established behavior rather than proposed design. When a driver or interface changes upstream during the transition period, its documentation must be updated with the same parity work.

## Source cutover

Until Michael explicitly declares `RiscRTE-Drivers` the source repository for drivers, T5S3-Reader remains the read-only source of truth for synchronization. After cutover, this repository's own source/version/release state becomes authoritative.

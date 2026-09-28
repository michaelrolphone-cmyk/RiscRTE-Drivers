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

Because this repository is new and has no downstream dependency consumers yet, migration work is committed directly to `main`.

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

`scripts/check_parity.py` compares the local released/source inventory against the read-only upstream source/release data. The recorded `source_master_sha` values identify the commit at which the stored parity snapshot was last refreshed; CI determines actual source drift from the current upstream driver manifests and exact per-driver tree SHAs, so unrelated upstream application/firmware commits do not make driver parity fail. The released-driver and source-tree snapshot SHAs must remain synchronized with each other. `scripts/check_driver_docs.py` requires every entry marked `migrated: true` to have a documentation page and corresponding README link.\n\nThe scheduled maintenance run still refreshes the recorded upstream snapshot SHA after inspection. A later upstream head is informational when all released versions and driver tree SHAs are unchanged, but any driver source/version/path/tree change continues to fail parity until migrated here.

## Build and test model

Current driver builds use the Xtensa ESP32-S3 GCC toolchain provisioned through PlatformIO. Individual build scripts validate the package identity and driver-specific output, including exported symbols and ELF architecture where implemented. CI runs parity/documentation checks separately from compilation so inventory drift cannot be hidden by a successful compiler invocation.

Published-byte parity is recorded only where it has actually been demonstrated. Source-only drivers have no published artifact to compare against.

## Driver documentation tree

### Migrated and independently buildable

- [gps-nmea](docs/drivers/gps-nmea.md) — allocation-free NMEA 0183 GGA/RMC GNSS provider over runtime serial/power/clock host services, version 1.0.0 (source-only upstream).
- [i2c-esp32s3-v2](docs/drivers/i2c-esp32s3-v2.md) — firmware-backed `i2c.bus@1` provider with exclusive address claims and one FreeRTOS mutex serializing provider state plus complete synchronous transactions, version 0.1.5; canonical release-byte parity remains to be reproduced independently.
- [usb-cdc-acm](docs/drivers/usb-cdc-acm.md) — allocation-free USB CDC ACM descriptor/protocol provider for binding discovery and class-request encoding, version 0.1.0 (source-only upstream).
- [usb-cdc-acm-v2](docs/drivers/usb-cdc-acm-v2.md) — USB CDC ACM `serial.port@1` provider above `usb.host@1`, version 0.1.0; parses and claims one unambiguous ACM function and routes class/bulk transfers through host-owned claims.
- [usb-ch34x-v2](docs/drivers/usb-ch34x-v2.md) — WCH-compatible CH34x `serial.port@1` provider above `usb.host@1`, version 0.1.0; validates one unambiguous vendor interface, performs CH34x vendor initialization/framing requests, and routes bounded bulk I/O through the host claim.
- [usb-cp210x-v2](docs/drivers/usb-cp210x-v2.md) — Silicon Labs CP210x `serial.port@1` provider above `usb.host@1`, version 0.1.0; owns vendor matching/control, line configuration, generation-safe sessions, and bounded bulk I/O.
- [usb-ftdi](docs/drivers/usb-ftdi.md) — single-port FTDI `serial.port@1` provider above `usb.host@1`, version 0.1.0; validates supported VID/PID/device generations, programs FTDI baud/line state, strips per-packet status bytes, and preserves buffered payload.
- [usb-hid](docs/drivers/usb-hid.md) — generic USB HID interface provider above the interrupt-capable `usb.host@1` extension, version 0.1.2; parses bounded composite HID descriptors, owns interface claims, fetches report descriptors, selects boot/report protocol, and forwards interrupt-IN reports.
- [usb-hid-gamepad](docs/drivers/usb-hid-gamepad.md) — descriptor-driven `usb.hid.gamepad@1` class provider over `usb.hid@1`, version 0.1.3; parses bounded HID layouts, normalizes buttons/axes/hat state, coalesces compatibility notifications, and exposes discovery diagnostics.
- [usb-hid-keyboard](docs/drivers/usb-hid-keyboard.md) — boot-protocol keyboard class provider above `usb.hid@1`, version 0.1.1; tracks four keyboards, copied ordered subscriber events, snapshots, overflow gaps, and quiescent HID-session release.
- [usb-hid-text-input](docs/drivers/usb-hid-text-input.md) — USB HID keyboard to transport-neutral `input.text@1` translator, version 0.1.0; upstream now publishes a canonical release and independent byte parity is pending CI confirmation.
- [usb-host-v2](docs/drivers/usb-host-v2.md) — generation-safe `usb.host@1` provider above `usb.controller@1`, version 0.1.3; validates descriptor-derived bulk/interrupt endpoint access, quarantines failed releases, forwards optional diagnostics, and requires canonical published-byte parity from its standalone Xtensa build.
- [usb-mass-storage](docs/drivers/usb-mass-storage.md) — USB MSC BOT/SCSI `storage.volume@1` provider with FAT16/FAT32 filesystem handling, version 0.1.1; CI run `36352310880` passed the exact upstream FAT16 host fixture and reproduced the canonical 23,792-byte ELF byte-for-byte.
- [usb-stlink](docs/drivers/usb-stlink.md) — ST-LINK V2/V2.1/V3 `debug.vendor.stlink@1` provider above `usb.host@1` and `platform.clock@1`, version 0.1.0; CI run `36344579403` passed the exact host fixture and reproduced the canonical 9,304-byte ELF byte-for-byte.
- [usb-ui-navigation](docs/drivers/usb-ui-navigation.md) — composite `input.navigation@1` provider over semantic text, HID gamepad, and XInput gamepad sources, version 0.1.1; preserves foreground handoff and neutral rearm semantics.
- [usb-xinput-gamepad](docs/drivers/usb-xinput-gamepad.md) — Xbox 360 wired/wireless-format `usb.xinput.gamepad@1` provider above `usb.host@1` and `platform.clock@1`, version 0.1.3; CI run `36345187504` passed the exact upstream host fixture and reproduced the canonical 11,088-byte ELF byte-for-byte.
- [gt911-touch](docs/drivers/gt911-touch.md) — GT911 raw-touch provider over `i2c.bus@1`, version 0.1.0; independent CI output matches the published upstream ELF.
- [platform-clock-v1](docs/drivers/platform-clock-v1.md) — generic `platform.clock@1` monotonic-time and sleep provider, version 0.1.0.
- [program-msp](docs/drivers/program-msp.md) — MSP430FR/XV2 FRAM programming provider above `debug.vendor.msp@1`, version 0.1.0; standalone build requires canonical release-byte parity.
- [t5s3-usb-power-profile](docs/drivers/t5s3-usb-power-profile.md) — immutable T5S3 BQ25896/USB power-policy profile provider, version 0.1.0.

### Staged migrations awaiting parity completion

- [board-power-t5s3-v2](docs/drivers/board-power-t5s3-v2.md) — reusable BQ25896 `board.power.vbus@1` provider over `i2c.bus@1`, `platform.clock@1`, and an installed electrical-profile provider, version 0.1.5; exact source/test/ABI/build/docs staged and canonical 10,860-byte ELF parity pending CI.
- [usb-msp](docs/drivers/usb-msp.md) — TI MSP-FET/eZ-FET `debug.vendor.msp@1` transport above `usb.host@1` and `platform.clock@1`, version 0.1.1; exact source/test/build/docs staged and canonical 14,116-byte ELF parity pending integrated CI.
- [usb-controller-esp32s3](docs/drivers/usb-controller-esp32s3.md) — ESP32-S3 physical `usb.controller@1` provider, version 0.1.18; exact upstream source and implementation documentation staged, with repository-independent PIC ESP-IDF build/audit and canonical 783,576-byte ELF parity still pending.

## Documentation standard

Each migrated driver's `docs/drivers/<driver-id>.md` page is derived from its current implementation source, manifest, ABI/interface headers, build tooling, tests, and observed release metadata. Documentation describes established behavior rather than proposed design. When a driver or interface changes upstream during the transition period, its documentation must be updated with the same parity work.

## Source cutover

Until Michael explicitly declares `RiscRTE-Drivers` the source repository for drivers, T5S3-Reader remains the read-only source of truth for synchronization. After cutover, this repository's own source/version/release state becomes authoritative.

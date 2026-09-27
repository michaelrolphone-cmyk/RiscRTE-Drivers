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

`scripts/check_parity.py` compares the local released/source inventory against the read-only upstream source/release data. `scripts/check_driver_docs.py` requires every entry marked `migrated: true` to have a documentation page and corresponding README link.

## Build and test model

Current driver builds use the Xtensa ESP32-S3 GCC toolchain provisioned through PlatformIO. Individual build scripts validate the package identity and driver-specific output, including exported symbols and ELF architecture where implemented. CI runs parity/documentation checks separately from compilation so inventory drift cannot be hidden by a successful compiler invocation.

Published-byte parity is recorded only where it has actually been demonstrated. Source-only drivers have no published artifact to compare against.

## Driver documentation tree

### Migrated and independently buildable

- [gps-nmea](docs/drivers/gps-nmea.md) — allocation-free NMEA 0183 GGA/RMC GNSS provider over runtime serial/power/clock host services, version 1.0.0 (source-only upstream).\n- [usb-hid-text-input](docs/drivers/usb-hid-text-input.md) — USB HID keyboard to transport-neutral `input.text@1` translator, version 0.1.0 (source-only upstream).
- [gt911-touch](docs/drivers/gt911-touch.md) — GT911 raw-touch provider over `i2c.bus@1`, version 0.1.0; independent CI output matches the published upstream ELF.
- [platform-clock-v1](docs/drivers/platform-clock-v1.md) — generic `platform.clock@1` monotonic-time and sleep provider, version 0.1.0.
- [t5s3-usb-power-profile](docs/drivers/t5s3-usb-power-profile.md) — immutable T5S3 BQ25896/USB power-policy profile provider, version 0.1.0.

### Present but not yet migration-complete

The repository also contains partial scaffolding or parity metadata for additional upstream drivers. They are intentionally not listed as completed documentation entries until their source/build/package migration and dedicated documentation are complete. See `manifest/released-drivers.json` and `manifest/source-trees.json` for the full current inventory.

## Documentation standard

Each migrated driver's `docs/drivers/<driver-id>.md` page is derived from its current implementation source, manifest, ABI/interface headers, build tooling, tests, and observed release metadata. Documentation describes established behavior rather than proposed design. When a driver or interface changes upstream during the transition period, its documentation must be updated with the same parity work.

## Source cutover

Until Michael explicitly declares `RiscRTE-Drivers` the source repository for drivers, T5S3-Reader remains the read-only source of truth for synchronization. After cutover, this repository's own source/version/release state becomes authoritative.

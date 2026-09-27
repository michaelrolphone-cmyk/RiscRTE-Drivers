# Migration status

Source inventory on `T5S3-Reader:master` at `1ed24d19d226658d375163eab6062faa74a1519e`: 22 driver directories with manifests. Of those, 20 are canonical released drivers in `T5S3-Reader:release-index`; two are source-only ABI-v1 drivers (`gps-nmea` and `usb-cdc-acm`).

## Migrated and independently buildable

- `gps-nmea` v1.0.0 — exact source/ABI headers migrated with standalone Xtensa build and ELF/export validation; upstream is source-only, so there is no canonical released ELF for byte-parity comparison.
- `gt911-touch` v0.1.0 — source/header mirrored and independently buildable; CI run 36277642683 produced 8,736 bytes with SHA-256 `9b934b1056fc8a99f4973dccc311d991f4c4ff1e46b826d27fc2919e6555ec6d`, exactly matching the current published upstream ELF.
- `platform-clock-v1` v0.1.0 — source/header mirrored, standalone build, ELF validation, and byte-for-byte published ELF parity.
- `t5s3-usb-power-profile` v0.1.0 — source/manifest/profile ABI migrated, standalone build, ELF validation, and byte-for-byte published ELF parity.

## Released drivers still requiring source migration

- board-power-t5s3-v2 v0.1.5
- i2c-esp32s3-v2 v0.1.2
- program-msp v0.1.0
- usb-cdc-acm-v2 v0.1.0
- usb-ch34x-v2 v0.1.0
- usb-controller-esp32s3 v0.1.17
- usb-cp210x-v2 v0.1.0
- usb-ftdi v0.1.0
- usb-hid v0.1.2
- usb-hid-gamepad v0.1.3
- usb-hid-keyboard v0.1.1
- usb-host-v2 v0.1.3
- usb-mass-storage v0.1.1
- usb-msp v0.1.1
- usb-stlink v0.1.0
- usb-ui-navigation v0.1.0
- usb-xinput-gamepad v0.1.3

## Source-only drivers still requiring compatibility migration

- usb-cdc-acm v0.1.0 (driver ABI 1)

The parity workflow reads the source repository only. It never writes to T5S3-Reader.

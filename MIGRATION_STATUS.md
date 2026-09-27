# Migration status

Source inventory on `T5S3-Reader:master` at `935ac7f81191994e78a00bb94f443389764c1a35`: 23 driver directories with manifests. Of those, 20 are canonical released drivers in `T5S3-Reader:release-index`; three are source-only drivers (`gps-nmea`, `usb-cdc-acm`, and `usb-hid-text-input`).

One released driver currently has a newer source version on master than its published release-index version: `usb-controller-esp32s3` is released at v0.1.17 with source at v0.1.18. The parity manifest records both versions independently so unpublished source progress is not mistaken for a published release.

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
- usb-controller-esp32s3 released v0.1.17 / source v0.1.18
- usb-cp210x-v2 v0.1.0
- usb-ftdi v0.1.0
- usb-hid v0.1.2
- usb-hid-gamepad v0.1.3
- usb-hid-keyboard v0.1.1
- usb-host-v2 v0.1.3
- usb-mass-storage v0.1.1
- usb-msp v0.1.1
- usb-stlink v0.1.0
- usb-ui-navigation v0.1.1
- usb-xinput-gamepad v0.1.3

## Source-only drivers still requiring compatibility migration

- usb-cdc-acm v0.1.0 (driver ABI 1)
- usb-hid-text-input v0.1.0 (driver ABI 2)

The parity workflow reads the source repository only. It never writes to T5S3-Reader.

# Migration status

Source inventory on `T5S3-Reader:master` at `99abac00a0ec49e16da0110833f1f51e8d23c6d0`: 23 driver directories with manifests. Of those, 21 are canonical released drivers in `T5S3-Reader:release-index`; two are source-only drivers (`gps-nmea` and `usb-cdc-acm`).

## Migrated and independently buildable

- `gps-nmea` v1.0.0 — exact source/ABI headers migrated with standalone Xtensa build and ELF/export validation; upstream is source-only, so there is no canonical released ELF for byte-parity comparison.
- `usb-hid-text-input` v0.1.0 — exact source/ABI headers migrated with the upstream host translation/lifecycle test and standalone Xtensa build/export validation. Upstream now publishes a canonical 8,692-byte ELF with SHA-256 `796cd1b754de33f3c12cd5e29a54036766d081b71a5f05ff2d66a37beeb3ef3d`; independent byte-for-byte build parity remains to be confirmed by integrated CI.
- `gt911-touch` v0.1.0 — source/header mirrored and independently buildable; CI run 36277642683 produced 8,736 bytes with SHA-256 `9b934b1056fc8a99f4973dccc311d991f4c4ff1e46b826d27fc2919e6555ec6d`, exactly matching the current published upstream ELF.
- `platform-clock-v1` v0.1.0 — source/header mirrored, standalone build, ELF validation, and byte-for-byte published ELF parity.
- `t5s3-usb-power-profile` v0.1.0 — source/manifest/profile ABI migrated, standalone build, ELF validation, and byte-for-byte published ELF parity.

## Released drivers still requiring source migration

- board-power-t5s3-v2 v0.1.5
- i2c-esp32s3-v2 v0.1.2
- program-msp v0.1.0
- usb-cdc-acm-v2 v0.1.0
- usb-ch34x-v2 v0.1.0
- usb-controller-esp32s3 v0.1.18
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

The parity workflow reads the source repository only. It never writes to T5S3-Reader.

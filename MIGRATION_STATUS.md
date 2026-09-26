# Migration status

Source inventory on `T5S3-Reader:master`: 20 driver directories with manifests. Of those, 18 are canonical released drivers in `T5S3-Reader:release-index`; two are legacy ABI-v1 source-only drivers (`gps-nmea` and `usb-cdc-acm`).

## Migrated and independently buildable

- `platform-clock-v1` v0.1.0 — source/header mirrored, standalone build, ELF validation, and byte-for-byte published ELF parity.
- `t5s3-usb-power-profile` v0.1.0 — source/manifest/profile ABI migrated, standalone build, ELF validation, and byte-for-byte published ELF parity.

## Released drivers still requiring source migration

- board-power-t5s3-v2 v0.1.5
- i2c-esp32s3-v2 v0.1.2
- program-msp v0.1.0
- usb-cdc-acm-v2 v0.1.0
- usb-ch34x-v2 v0.1.0
- usb-controller-esp32s3 v0.1.15
- usb-cp210x-v2 v0.1.0
- usb-ftdi v0.1.0
- usb-hid v0.1.2
- usb-hid-gamepad v0.1.3
- usb-hid-keyboard v0.1.1
- usb-host-v2 v0.1.3
- usb-msp v0.1.1
- usb-stlink v0.1.0
- usb-ui-navigation v0.1.0
- usb-xinput-gamepad v0.1.3

## Legacy source-only drivers still requiring compatibility migration

- gps-nmea v1.0.0 (driver ABI 1)
- usb-cdc-acm v0.1.0 (driver ABI 1)

The parity workflow reads the source repository only. It never writes to T5S3-Reader.

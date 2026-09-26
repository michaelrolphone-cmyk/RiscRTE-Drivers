# Migration status

Parity baseline: 18 canonical driver releases from `T5S3-Reader:release-index`.

## Migrated and independently buildable

- `platform-clock-v1` v0.1.0 — source, manifest, generic provider ABI headers, standalone build, ELF export/architecture validation.
- `t5s3-usb-power-profile` v0.1.0 — source and published manifest mirrored from master, BQ25896 profile ABI header, standalone Xtensa build, ELF export/architecture validation.

## Release metadata tracked, source migration pending

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

The parity workflow reads the source repository only. It never writes to T5S3-Reader.

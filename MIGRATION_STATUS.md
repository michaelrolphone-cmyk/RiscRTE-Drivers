# Migration status

Source inventory on `T5S3-Reader:master` at `403a9f418fe5e6d231711c4e2242ed0acda43329`: 23 driver directories with manifests. Of those, 21 are canonical released drivers in `T5S3-Reader:release-index`; two are source-only drivers (`gps-nmea` and `usb-cdc-acm`).

## Migrated and independently buildable

- `gps-nmea` v1.0.0 — exact source/ABI headers migrated with standalone Xtensa build and ELF/export validation; upstream is source-only, so there is no canonical released ELF for byte-parity comparison.
- `i2c-esp32s3-v2` v0.1.5 — exact current source, provider/private compatibility ABI headers, linker/export controls, and exact current upstream pthread-backed host behavioral test/stubs migrated. Version 0.1.5 removes the 0.1.3/0.1.4 relocatable atomic coordination design and uses one FreeRTOS mutex to serialize provider-local state plus complete synchronous transactions, so callers queue instead of receiving a synthetic busy failure and `release_device` drains in-flight work before removing a claim. The standalone validator expects the firmware bridge plus the four FreeRTOS queue symbols, forbids atomic helpers/direct hardware imports, and targets the published 24,496-byte ELF SHA-256 `7b8f51f62da71e99949b093b6cdc531435a0cec01f87740921f9436544f8bd9c`. Independent canonical byte parity remains to be reproduced by CI.
- `usb-cdc-acm` v0.1.0 — exact descriptor/protocol provider source and ABI header migrated; the host validation test passes under `-Wall -Wextra -Werror`; standalone Xtensa ELF/export validation is wired into CI. Upstream is source-only, so there is no canonical released ELF for byte-parity comparison.
- `usb-cdc-acm-v2` v0.1.0 — exact current ABI-v2 source and current upstream host fixture migrated above `usb.host@1`, with standalone Xtensa/export/import validation. The fixture covers dependency binding, configuration and data-claim failures, ACM line coding, DTR/RTS, bulk routing, explicit cleanup, stale tokens, direct-stop cleanup, restart, and quiescence. Canonical published ELF target: 6,584 bytes, SHA-256 `34b3eeea0cca3927517e849298a1f088cbd1a14651bf81ff2b74f655a78af56f`; independent byte parity pending CI.
- `usb-cp210x-v2` v0.1.0 — exact current ABI-v2 CP210x source and exact upstream host fixture migrated above `usb.host@1`. The fixture covers Silicon Labs VID gating, interface enable/disable control requests, baud/framing requests, DTR/RTS, bulk routing, failed-close retention, stale-token rejection, and quiescence. Canonical published ELF target: 5,788 bytes, SHA-256 `96f22bbb6cc8cf00f138656a910d3078d491d6c117ffc4e23a2842c458481c61`; independent byte parity is to be established by integrated CI.
- `usb-hid-text-input` v0.1.0 — exact source/ABI headers migrated; the exact upstream host translation/lifecycle test passes locally under `-Wall -Wextra -Werror`; standalone Xtensa build/export validation is wired into CI. Upstream publishes a canonical 8,692-byte ELF with SHA-256 `796cd1b754de33f3c12cd5e29a54036766d081b71a5f05ff2d66a37beeb3ef3d`; independent byte-for-byte build parity remains to be confirmed by integrated CI.
- `gt911-touch` v0.1.0 — source/header mirrored and independently buildable; CI run 36277642683 produced 8,736 bytes with SHA-256 `9b934b1056fc8a99f4973dccc311d991f4c4ff1e46b826d27fc2919e6555ec6d`, exactly matching the current published upstream ELF.
- `platform-clock-v1` v0.1.0 — source/header mirrored, standalone build, ELF validation, and byte-for-byte published ELF parity.
- `program-msp` v0.1.0 — exact source and `program.msp@1`/`debug.vendor.msp@1` ABI headers migrated with the upstream host behavior fixture; standalone Xtensa build requires exact parity with the published 9,128-byte ELF SHA-256 `19b0999f41e45522e877097addf3cfd55651b2fd00ae925fa6084b769b66527f`.
- `t5s3-usb-power-profile` v0.1.0 — source/manifest/profile ABI migrated, standalone build, ELF validation, and byte-for-byte published ELF parity.

## Released drivers still requiring source migration

- board-power-t5s3-v2 v0.1.5
- usb-ch34x-v2 v0.1.0
- usb-controller-esp32s3 v0.1.18
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

None.

The parity workflow reads the source repository only. It never writes to T5S3-Reader.

# Badge native Runtime build

This is a Drivers-owned product build profile, not a Runtime fork or a new
hardware-provider ABI. It selects the actual ESP-IDF native Runtime CPU port
and the shared CO5300 implementation using the board's existing 466x466
profile. The scoped I2C/GPIO/display APIs are registered by Runtime's existing
`NativeHardware.cpp` and `CpuPort.cpp` during Runtime boot admission, before
provider activation or hardware I/O.

There is no native early hardware/startup hook in this composition. It neither
writes the AXP2101 nor resets GPIO2. The graph's passive DCDC1 observer owns the
read-only supply check; the CST9217 provider owns touch reset; the CO5300 native
backend owns panel reset GPIO1. The original two-reset preparation helper must
not be linked into this target.

## Exact source and build selection

- Runtime: `13eca114cb58469cf6004fafc3da84fbd9d29beb`, unmodified.
- ESP-IDF 6.1: `fff9895c82d744c7237be8847347bdd1b07c6643`.
- GCC 15.2.0: `esp-15.2.0_20251204`, ESP32-S3 frontend.
- Native source: `components/co5300/native`, with its unchanged source notices.
- Application declaration capacity: 24 rows for shared Home's 20 declared
  required/optional services. Live application grants remain at the default 16.
- Flash: 32 MiB QIO; octal PSRAM enabled with 8 MiB physical capacity expected.
- New-board layout: NVS at `0x9000`, factory application at `0x10000` of size
  `0x300000`, flash SPIFFS bootfs at `0x310000` of size `0x1cf0000`.
- No SD, external RTC, paired banks, AppData, radio IQ, or native startup reset
  selection. Native realtime remains unset until a separately authorized seed;
  build time is not an external RTC or authoritative current time.

The 28.9375 MiB bootfs requires 512-byte SPIFFS pages because the pinned SDK's
page/object/span indices are 16-bit. The bootfs packager must match the actual
generated configuration, including page size, object-name length, metadata
length and magic flags. A 256-byte-page image is incompatible. The builder does
not create, install, erase, format, merge or flash this store.

## Reproduce and inspect

Use an installed/exported pinned ESP-IDF environment. The checkout and compiler
identities are checked before configuration. The target always builds with one
job, and all generated files and logs remain under the selected output path.

```sh
python scripts/build_badge_native.py \
  --runtime /path/to/pinned/RiscRTE --idf "$IDF_PATH" \
  --cc "$(command -v xtensa-esp32s3-elf-gcc)" \
  --python "$(command -v python)" --output /tmp/badge-native
```

On success, `build-proof.json` records inputs and artifact hashes;
`build/qualification/linked-firmware.json` records Runtime's actual linked
boundaries; `build/qualification/badge-native.json` checks the final display
profile, native provider registration, partition bytes and flash/PSRAM config.
`configure.log`, `build.log`, `runtime-proof.log` and `badge-proof.log` retain
separate outcomes. `.github/workflows/badge-native.yml` reproduces this exact
build cohort in the pinned official IDF container.

This target links Runtime and the native panel implementation only. The shared
Home application and ordinary loadable providers stay in the separately built,
validated boot store. A linked target alone is not proof of boot-store admission,
physical boot, display correctness, touch mapping, power behavior, or a release.

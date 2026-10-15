# Generic driver native toolchain

Generic ABI-v2 ESP32-S3 drivers build with the ESP-IDF 6.1 toolchain distribution
`esp-15.2.0_20251204` (GCC 15.2.0). Their capability/provider headers and driver
sources are unchanged. Building these hardware-independent ELFs does not require
Arduino, PlatformIO, or ESP-IDF private headers.

## Compiler selection

Activate the pinned ESP-IDF environment, or run the Linux x86_64 CI installer:

```sh
bash scripts/install_native_driver_toolchain.sh
# Outside Actions, apply the export command printed by the installer.
python3 scripts/build_platform_clock_v1.py
```

The installer verifies Espressif's archive SHA-256 from the selected IDF commit
`fff9895c82d744c7237be8847347bdd1b07c6643` before extraction. It puts the native
compiler on `GITHUB_PATH` in Actions.

`native_driver_toolchain.py` chooses:

1. An explicit compiler argument (where supported), otherwise `NATIVE_DRIVER_CC`.
2. The matching ESP32-S3 frontend next to `xtensa-esp-elf-gcc` found on `PATH`.
3. `xtensa-esp32s3-elf-gcc` on `PATH`.
4. The legacy PlatformIO toolchain under `PLATFORMIO_CORE_DIR` or `~/.platformio`.
   The X4 board builder also retains its earlier `/tmp/xtensa` fallback.

Explicit compiler paths and executable names are accepted. A missing explicit
compiler fails rather than selecting another one. An explicit unified native
compiler maps only to its own installation's S3 frontend. Executable basenames
are preserved, including symlinks, because the frontend selects the target.
The selected compiler must report a supported Xtensa target and predefine
little-endian/windowed-ABI macros.

Do not invoke the bare unified compiler for an ESP32-S3 ELF. It defaults to
big-endian generic Xtensa. An absolute `-mdynconfig` path alone is insufficient:
it can select S3 instructions while missing the S3 libgcc multilib. The supplied
S3 frontend configures both correctly. Missing native frontend files fail closed.

## Native compatibility without a firmware ABI change

- The generic clock's native build uses the narrow
  `sdk/compat/elf-time32/time.h` overlay. Runtime's ordinary `clock_gettime`
  import remains an 8-byte signed time32 structure, with nanoseconds at offset 4.
  GCC15's native 16-byte timespec must not cross that boundary. The overlay is
  applied only to this ordinary consumer, never native firmware/private IDF
  providers, and leaves historical compiler inputs unchanged.
- Clock and radio builds link a hidden, bounded source-owned `__udivdi3` on the
  unified toolchain. GCC15's stock archive helper crashes the shared-object
  linker and is unsuitable for this restricted ELF contract. The helper adds
  no firmware import or public driver export. Legacy GCC keeps its old archive
  inputs.
- Native USB mass storage disables loop-distribution pattern recognition to
  retain the existing `memcpy`/`memset` import ceiling. Otherwise GCC15 introduces
  a `strlen` import from a source loop. Imports may be optimized away.

## Historical byte parity

Native output records its actual digest and does not claim historical GCC8
byte equality. These formerly implicit release gates now accept an explicit
`--require-byte-parity` mode:

- `build_board_power_t5s3_v2.py`
- `build_t5s3_usb_power_profile.py`
- `build_usb_mass_storage.py`
- `build_usb_msp.py`
- `build_usb_hid_gamepad.py`
- `build_usb_ui_navigation.py`
- `build_program_msp.py`
- `build_display_epd_video.py`

The separate `historical-release-parity` CI job provisions the legacy compiler,
selects it explicitly, and requires those digests. Dedicated I2C, GT911, and USB
controller historical replay builders retain their pinned environments and
strict parity purpose unchanged. Their original SDK/framework dependencies are
not replaced by the generic native compiler. Root `platformio.ini` now only
provisions the legacy toolchain; it no longer requests an Arduino framework.

## Verification

```sh
python3 -m unittest discover -s test -p test_native_driver_toolchain.py -v
NATIVE_DRIVER_INTEGRATION=1 python3 -m unittest discover -s test -p test_native_driver_toolchain.py -v
```

The first command covers selection, explicit overrides, native frontend matching,
legacy fallback, executable/symlink handling, endian/ABI rejection and opt-in
parity. The second requires pinned GCC15, builds clock/radio/storage/display,
checks ELF32 little-endian Xtensa ET_DYN, imports/exports and manifests, and
proves GCC15 cannot pass the historical display digest gate. Set
`NATIVE_DRIVER_ELF_VALIDATOR` to an existing structural validator executable to
also validate those exact ELFs.

Host tests for the source-owned division helper and time32 clock consumer run in
CI with AddressSanitizer/UndefinedBehaviorSanitizer. The division test includes
boundary values and 10,000 deterministic randomized cases. The time32 consumer
checks canaries, nanosecond conversion, the INT32_MAX-second boundary, errors,
restart, and sleep chunking.

For a separate Runtime checkout containing its native time32 adapter, compile
`test/native_time32_runtime_adapter.c` with that checkout's
`lib/elf_loader/include` and compile `test/native_time32_runtime_test.c` with the
consumer overlay, then link the two objects. This tests the actual adapter's
native-time64 to ordinary-time32 conversion, boundary/error behavior and buffer
canaries without mixing their incompatible headers in one translation unit.

The GT911 builder retains its pre-existing four FreeRTOS queue imports. Those
are outside the ordinary System-Apps firmware export set; they remain in the
Runtime privileged OS/CPU symbol lists. A structural validator pass does not
establish ordinary-loader admission for that driver. This migration does not
change its source, imports, or admission policy.

These are software/build/ABI checks. They do not establish physical-device or
peripheral qualification, and native ELF digests are not historical parity.

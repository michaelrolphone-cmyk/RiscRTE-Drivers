# Shared CO5300 component

This source-only component owns a single `display.output@1` facade and a single
native ESP-IDF transport. Both are parameterized by `RiscCo5300Profile.h`; board
profiles are build inputs, not new driver implementations or Runtime ABI.
`facade/manifest.json` names the extracted provider `co5300-display` 0.1.0.
There is no published artifact, release inventory entry or installed firmware.

## Boundaries

- `facade/driver.c` is a loadable ABI-v2 provider. It uses existing selected
  hardware, native-display, clock, power-rail and GPIO capabilities. It requires
  verified enabled 3300 mV readback and never writes the supply.
- `native/Co5300Display.cpp` is statically linked by a device build through the
  existing Runtime `RiscCpu::selectedDisplayBackend()` hook. It is not a loadable
  ELF and is not added to generic Runtime. Native owner-task/core, checked DMA,
  retained timeout cleanup, dark first-frame and reset ownership remain intact.
- `include/` contains the extracted provider helpers/private build profile and
  existing ABI descriptors. Public Watch/Runtime headers are consumed at pinned
  revisions; no shared ABI is redefined or changed by this extraction.
- The default profile reproduces the old 2.06 dimensions, commands and timing.
  It is a regression reference, not automatic adoption by the 2.06 product.
  An independent 320×240 profile with Y offset validates general parameterization.
- Board-specific policy lives in `boards/`, not in another copy of this driver.
  The [1.75C profile](../../boards/waveshare-esp32-s3-touch-amoled-1.75c/DISPLAY.md)
  selects 466×466, offset6, safe insets, reset timing and QSPI framing.

Compile the facade and native backend with the same selected profile. The
existing Runtime admission check still verifies the actual selected hardware
configuration before access. A build profile does not grant hardware authority.
The eventual product must validate its complete graph and retain mapped code,
configuration and dependencies after failed quiescence.

## Reproduce

`scripts/run_badge_display_ci.sh` is the exact component qualification recipe.
It configures the pinned real ESP-IDF SDK through Runtime, tests facade/supply
and real preparation/CST composition normally and with ASan/UBSan, compiles the
native implementation for host and Xtensa, and validates both loadable ELFs with
the real Runtime loader. The native matrix covers default, Badge and independent
geometry with trace/probe combinations and retained timeout/failure cases.

The component does not require the 2.06 product or vendor checkout to build or
test. Runtime `13eca114`, Watch `605d5e7d`, IDF `fff9895c` and GCC15.2.0 are the
qualification inputs, not a new migration or hardware gate. Watch supplies the
existing shared provider/app ABI headers and ELF relocation/compaction tools;
no application code is copied or forked.

Use `scripts/stage_co5300_native.py --help` for explicit device-build staging.
The staging input accepts an optional profile path rather than a finite board
list. Full board graph/startup, flash partition assembly and shared Watch
provisioning remain product integration work. Hardware colors, orientation,
touch agreement, load voltage and sleep behavior remain unmeasured.

See [qualification and explicit limits](QUALIFICATION.md) and
[provenance and retained notices](PROVENANCE.md). No merge, release or device
operation is performed by these scripts.

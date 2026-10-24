# Badge shared-display integration

This source unit reuses `components/co5300/facade` and `components/co5300/native`.
It does not add an application variant, duplicate display driver, installable
firmware, Runtime boot hook or device qualification. The extracted default profile retains 2.06 behavior as a regression reference.
The 2.06 product repository, board graph, release recipes and published bytes are
not changed. This unit does not make that product consume the shared component.

## Selected source profile

Compile **both** facade and native backend with `-include display-profile.h`
and `components/co5300/include/`. The profile is private build data, not a
new Runtime ABI. It selects 466×466 RGB565, columns 6–471, rows 0–465, active-low
LCD reset 10/150 ms, and the canonical BSP MADCTL=0 orientation. A 69-pixel safe
inset on each side exposes a conservative 328×328 content rectangle through the
existing display API. These are mathematical bring-up bounds, not measured
visible bounds; no contour-clipping or touch transformation is claimed.

The pinned BSP supplies its own initialization table, replacing the component
default. The profile retains the component's MADCTL/COLMOD preamble, BSP bank
selection and 600 ms waits, and both display-on commands. It deliberately uses
brightness `51=00` instead of vendor `51=ff`: existing backend custody keeps the
panel dark until an entire checked frame is complete. Pixels use direct QSPI
`32 00 2c 00`, not the 2.06 split RAMWR/RAMWRC sequence. The default header retains
all original 2.06 dimensions, offsets, timings and wire sequence.

Primary sources:

- [Waveshare BSP, exact init and startup](https://github.com/waveshareteam/Waveshare-ESP32-components/blob/010c5fb39ea7f0e996efd1f8e162cc70e569e57f/bsp/esp32_s3_touch_amoled_1_75c/esp32_s3_touch_amoled_1_75c.c#L52-L68)
- [Exact ESP-IOT CO5300 reset/init/draw implementation](https://github.com/espressif/esp-iot-solution/blob/2d289eb1f14431e345b345c3fd6fc299a555d770/components/display/lcd/esp_lcd_co5300/esp_lcd_co5300_spi.c#L188-L315)
- [BSP physical pin definitions](https://github.com/waveshareteam/Waveshare-ESP32-components/blob/010c5fb39ea7f0e996efd1f8e162cc70e569e57f/bsp/esp32_s3_touch_amoled_1_75c/include/bsp/esp32_s3_touch_amoled_1_75c.h#L43-L54)

The-Badge application subsequently mirrors both axes and uses a different gap.
Its installed upstream firmware is not evidence that this unrotated BSP profile
matches physical touch orientation. That remains a later device test.

## Power and reset ownership

DCDC1 supplies VCC3V3 to CPU, display and touch. Do not select the 2.06 ALDO2
provider. `supply/driver.c` implements the existing `power.rail@1` as a passive
observer. It reads AXP2101 identity03=4a, enable80 bit0, and voltage82 bits4:0.
Codes0..19 encode 1500+100×code mV; reserved codes fail. Successful readback is
not voltage-under-load measurement. The facade accepts only verified, enabled
3300 mV, and the observer never changes supply, charge, cutoff or power-key
registers. `set_enabled` always refuses without I/O. Failed cleanup retains the
I2C claim and dependency references until a checked retry succeeds.

- [AXP2101 register specification, §§6.13.2.69/71](https://files.waveshare.com/wiki/common/X-power-AXP2101_SWcharge_V1.0.pdf#page=40)
- [Vendor DCDC1 voltage implementation](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75C/blob/4bfdc80eedbf95031e384610796bed15e0240312/examples/arduino/libraries/XPowersLib/src/XPowersAXP2101.tpp#L1396-L1432)
- [Board schematic, VCC3V3 supply](https://files.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.75C/ESP32-S3-Touch-AMOLED-1.75C-schematic.pdf#page=1)

`touch-startup.c` composes the existing Drivers preparation helper/configuration,
selecting only GPIO2 (10 ms assert, 50 ms recovery). Its retained GPIO2 token
outlives the CST9217 provider. The caller must quiesce/stop CST9217 before calling
`risc_board_prepare_finish`; the helper cannot enforce consumer lifecycle itself.
GPIO1 is not claimed by this adapter. The existing facade briefly owns GPIO1 to
clear old deep-sleep hold, releases it, then the native backend owns it until
checked close. There is no release/reclaim of a live consumer's reset and no
new reset handoff ABI. Full graph validation and scoped GPIO/I2C callbacks remain
responsibilities of the eventual Runtime product assembly.

## Qualification and remaining assembly

Pinned source inputs for this unit:

- Watch `605d5e7d4e2b46f6b33ff2f195582db6f882535c` (shared app/safe-insets contract).
- This Drivers source tree (preparation and CST9217 inherited unchanged from
  `0a04382db4b6871a9e8298cbc964d4896e8b483a`).
- Runtime `13eca114cb58469cf6004fafc3da84fbd9d29beb` (actual loader and SDK build).
- IDF `fff9895c82d744c7237be8847347bdd1b07c6643`, GCC15.2.0.

`tests/test_badge_display.py --watch PATH [--sanitize]` tests the same facade
under default2.06, selected1.75C and unrelated320×240/asymmetric-inset profiles.
`tests/test_badge_touch_composition.py [--sanitize]` links the real
preparation helper and CST9217. The facade test also includes all DCDC1 codes/enables and read/claim/release
failures. Existing preparation and CST9217 regression suites remain unchanged.
`tests/test_co5300_native.py --idf PATH --compile-commands PATH` compiles against
actual SDK declarations for three geometries × trace/probe variants, with an
optional `--host-only --sanitize` run. No substitute SDK ABI is used.

`scripts/build_badge_display.py --watch PATH --runtime PATH
--cc COMPILER --output PATH` emits two target-qualified provider ELFs and a
private startup archive. It validates the ELFs through the actual Runtime loader.
The new CI reproduces host, ASan/UBSan and target checks. Leak checking is disabled
for the ptrace-hosted runner; no LeakSanitizer pass is claimed. These are component
results, not a linked firmware or physical test.

Remaining software work is actual board/provider graph and startup binding,
32 MiB product/partition assembly, battery/time service selection without a
nonexistent external RTC, and packaging the existing shared Watch default app.
No SD is advertised. Source preparation can proceed while held PRs and inherited
Drivers parity diagnostics are resolved; neither implies hardware qualification.

## Shared ownership and provenance

The reusable facade/native implementation is owned by this Drivers repository.
The board namespace owns only the 1.75C profile, DCDC1 observation and touch-reset
composition. See [the shared component](../../components/co5300/README.md) and
[its provenance](../../components/co5300/PROVENANCE.md). The originally reviewed
2.06-local candidate is preserved as source evidence, not published to that product.

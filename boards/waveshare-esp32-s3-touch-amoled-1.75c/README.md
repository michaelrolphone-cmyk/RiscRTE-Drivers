# Waveshare ESP32-S3-Touch-AMOLED-1.75C board sources

This package contains preparation and [shared-display integration](DISPLAY.md)
for the aluminium round Badge port. It is not an installable RiscRTE image,
Runtime board manifest or hardware qualification. The original preparation
archive is not a loadable provider or a finished Runtime startup adapter. Do not rename
`preparation.json` to `board.json` or add this archive to the driver inventory.
The shared `RiscRTE-Watch` default application remains the only application owner.

## Verified source mapping

`preparation.json` pins the reference URLs and exact source-byte hashes:
The-Badge `76172dfa`, Waveshare BSP 3.0.0 `010c5fb3`, CST9217 2.0.0
`9daffb71`, and Espressif CO5300 2.2.0 `2d289eb1`.

- 32 MiB flash and 8 MiB PSRAM are physical capacities, not a selected partition map.
- Shared I2C is controller 0, SDA15/SCL14, 400 kHz. CST9217 is address 0x5a.
- CO5300 is 466x466, QSPI clock38, data4/5/6/7, CS12, reset1, column offset6.
- CST reset2 and IRQ11 are separate from LCD reset1. The BSP's “shared reset”
  comment contradicts its actual pin definitions; the definitions are used.
- BOOT is GPIO0, active low. PWR is AXP2101 PWRON, not GPIO0 or SYS_OUT.
- LCD/touch use DCDC1 with the ESP32, per the pinned upstream board analysis.
  The 2.06 ALDO2 display-power provider must NOT be selected. It is not an
  independently switchable display supply. No supply voltage is inferred from
  register readback; voltage under load and actual board revision remain unmeasured.
- No external RTC or SD card is advertised. QMI8658 and ES8311/ES7210 are
  present but unselected in this slice; no unsupported service is granted.

The conservative proposed content rectangle uses 69-pixel insets, leaving
328x328 inside the 466-pixel circle. This is mathematical bring-up geometry, not
measured visible bounds. Physical raster and touch coordinates stay 466x466;
this data does not implement contour clipping or display output.

## Startup boundary

`boards/common/prepare.c` is a data-driven internal helper, not a new public ABI.
A future admitted adapter supplies the exact scoped I2C bus and GPIO/clock
callbacks. It checks AXP2101 identity and that DCDC1 is already enabled using
only reads. It never configures or disables a PMIC rail, charging, cutoff,
interrupt, or power key. An unexpected rail state fails rather than rewriting it.
Upstream charge-policy and unused-rail changes are intentionally not copied.

After releasing its PMIC claim, the helper claims each reset initially asserted,
checks pad readback, waits the pinned assert interval, deasserts, checks readback,
and waits recovery. LCD is 10/150 ms; touch is 10/50 ms. Total successful reset
delay is 220 ms. It retains the GPIO tokens while the consumers are alive.
No automatic reset after controller startup and no duplicate prepare of a live
state are allowed. Failure keeps every acquired token and borrowed API pointer
until checked cleanup succeeds. Cleanup is not a power-off or sleep operation;
it must run only after consumers stop. GPIO release electrical policy belongs
to the selected GPIO owner. The helper does not allocate or access hardware by
itself, and does not prove those callbacks are wired into Runtime.

## Reproduce

Run `bash test/run_board_preparation_test.sh` for normal and ASan/UBSan C tests
and ten Python profile tests. The C harness exhausts all 16 startup I/O failure
locations, with and without false/nonzero retained claims, retries retained
cleanup including mixed success/failure, rejects success-with-zero claims, rejects malformed inputs, tests incorrect power/readback, and tests a
three-reset alternate pin/polarity configuration. Profile mutation tests reject
pin overlap, invalid timing/types, wrong coordinate agreement and false readiness.
The board-specific builder separately admits the exact 1.75C identity, pin map,
controllers and shared-supply policy; generic structural validation alone is not
board admission. Negative tests cover plausible 2.06/other-controller substitutions.
LeakSanitizer is disabled, so no leak-check pass is claimed.

Run `python scripts/build_board_preparation.py` with the repository's supported
Xtensa compiler on PATH or `NATIVE_DRIVER_CC` set explicitly. It creates a PIC
static archive and records object architecture, sizes, hashes and compiler.
It does not create a `driver.elf` or claim Runtime ELF admission. Existing
`bash test/run_cst9217_touch_test.sh` remains unchanged and is also run in CI.

## Next technical integration, not hardware gates

1. Bind the tested touch-only adapter and shared CO5300 component into the real
   startup/provider graph. The adapter retains GPIO2 only; the native display
   owns GPIO1. The original two-reset preparation must not run concurrently
   with that composition. Stop consumers before cleanup and retain dependencies
   on failure.
2. Select the [tested 1.75C display profile and passive DCDC1 observer](DISPLAY.md)
   in that graph. These sources now exist, but a full linked boot graph does not.
   The 2.06 panel profile and separate ALDO2 rail remain incompatible.
3. Compose the existing CST9217 provider and shared Watch default with admitted
   providers, real onboard-flash storage/time policy, partitions and target image.
   No app migration, contour extension, or unrelated parity repair is a blanket gate.
4. Qualify the built candidate on hardware separately: boot, colors/damage,
   orientation/touch, BOOT/PWR, storage, power and later sleep/current.

Existing Watch PR15 and Drivers PR44 source and accepted driver inventories are
unchanged by this slice. The inherited PR44 source/release parity failure remains
separate. Upstream Badge success does not qualify this RiscRTE port.

# Waveshare ESP32-S3-Touch-AMOLED-1.75C board sources

This package contains preparation, [shared-display integration](DISPLAY.md),
a [scoped Home boot graph](home/README.md) and [native target build](native/README.md)
for the aluminium round Badge port. Software qualification is not physical
qualification or a released/installable combined image. The original preparation
archive remains an unselected historical helper. Do not rename `preparation.json`
to `board.json` or add that archive to the driver inventory.
The shared `RiscRTE-Watch` default application remains the only application owner.

## Verified source mapping

`preparation.json` pins the reference URLs and exact source-byte hashes:
The-Badge `76172dfa`, Waveshare BSP 3.0.0 `010c5fb3`, CST9217 2.0.0
`9daffb71`, and Espressif CO5300 2.2.0 `2d289eb1`.

- 32 MiB flash and 8 MiB PSRAM are the specified capacities. The separate native
  profile now selects a 3 MiB factory application and remaining-flash bootfs.
- Shared I2C is controller 0, SDA15/SCL14, 400 kHz. CST9217 is address 0x5a.
- CO5300 is 466x466, QSPI clock38, data4/5/6/7, CS12, reset1, column offset6.
- CST reset2 and IRQ11 are separate from LCD reset1. The BSP's “shared reset”
  comment contradicts its actual pin definitions; the definitions are used.
- BOOT is GPIO0, active low. PWR is AXP2101 PWRON, not GPIO0; PWRON is indirectly observable through T1-inverted SYS_OUT/GPIO3.
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

## Historical preparation helper (unselected)

`boards/common/prepare.c` is a data-driven internal helper, not a new public ABI.
A caller supplies the exact I2C bus and GPIO/clock callbacks. The production
Home graph does not use this helper: the CST9217 provider only claims its
controller address and owns its reset directly. Runtime scopes I2C by bus, not
by a per-consumer address ACL. The historical helper checks AXP2101 identity
and that DCDC1 is already enabled using
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
It does not create a `driver.elf` or claim Runtime ELF admission. The current
CST9217 suite runs with `bash test/run_cst9217_touch_test.sh /path/to/runtime`;
it now qualifies production scoped dependency/reset ownership.

## Current composition and remaining qualification

`home/board.json` is the actual hardware graph, distinct from preparation data.
The four loadable providers are scoped I2C, shared CO5300, CST9217 and the passive
DCDC1 observer. PMIC metadata is not a fake battery provider. GPIO2 stays with
CST9217 until checked teardown; GPIO1 remains under the existing display custody.
The native target excludes both historical startup helpers.

Shared Watch PR15 remains unchanged. The default ELF is built once by its
maintained `scripts/build.py` and copied byte for byte. Home receives display,
touch, native realtime-control and provider-promotion. Other services are explicit
optional declarations with no grants. Civil time starts UNSET; battery, sleep,
radio and application persistence are unavailable, not simulated.

See `home/README.md` for source/graph/provider/behavior evidence and the separate
native link. No source result confirms physical boot, display orientation,
touch mapping, measured rail voltage, BOOT/PWR, storage wear or later sleep/current.
No merge, release, flashing or device control is part of this qualification.
Historical accepted release bytes and the 2.06 product remain unchanged. The
inherited PR44 source/release parity drift remains separate from this unit.

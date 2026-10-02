# Xteink X4 Pro drivers

Loadable RiscRTE capability ELFs for the Xteink X4 Pro (ESP32-S3, 800×480, GT911, dual frontlight). Protocol and pin choices are derived from CrossPoint Reader's FreeInk X4 Pro notes for the `app1` / `ESP32S3_X4_TL_SSD1677` image. They are not a flash of CrossPoint, and they have not been run on a device in this tree.

These packages live under `boards/xteink-x4-pro/` so they do not enter the T5S3 `Drivers/` parity gate.

## Load order

1. `platform-clock-v1` (existing clock ELF)
2. `x4pro-gpio` provides `gpio.bank@1`
3. `x4pro-i2c` provides `i2c.bus@1` on GPIO39/GPIO38
4. `x4pro-panel` provides `display.output@1` (SSD1677 800×480 MONO1)
5. `x4pro-gt911` provides `input.touch.raw@1`
6. `x4pro-buttons` provides `input.navigation@1`
7. `x4pro-frontlight` provides `display.frontlight@1`
8. `x4pro-battery` provides `board.battery@1`
9. `x4pro-sd` provides `storage.volume@1`

Do not load `i2c-esp32s3-v2` on this board. That ELF delegates to firmware Wire on the T5S3 pin pair.

## Hardware used

| Function | Pins / address | Source note |
|---|---|---|
| Panel SPI | SCLK 12, MOSI 11, CS 13, DC 18, RST 14, BUSY 6 active-high | FreeInk app1, confirmed there |
| Touch | SDA 39, SCL 38, INT 10, RST 4, power GPIO2 active-low, GPIO1 high, 0x5D | FreeInk app1 |
| Buttons | 0 left, 7 right, 3 power, active-low | digital, not the vestigial ADC ladder |
| Frontlight | 8 cool, 9 warm, active-high | stock uses 25 kHz / 10-bit LEDC |
| Battery | CW2017 0x63, charge status GPIO21 active-high | read-only; no BATINFO write |
| SD | SDMMC CLK 41, CMD 42, DAT0 40, power GPIO5 active-low | SPI CMD0 is silent on this slot |

Panel init follows the recovered SSD1677 sequence: reset, `0x12`, temp `0x18=0x80`, booster `0x0C`, gate `0x01=DF 01 02`, then full update `0x22=0xF7` / `0x20`. UC8179/UC8279 share the glass and pinout; this ELF stays on SSD1677 when the probe is inconclusive.

Touch reports are mapped with X at byte 0 and `swapXY`, matching the FreeInk X4 Pro profile. `flipX` / `flipY` are not applied.

Frontlight is on/off per channel. Stock proportional PWM needs the LEDC clock, which this ELF does not claim.

SD identifies the card and can parse a FAT boot sector. Directory and file calls fail closed until a filesystem walk is published. `last_error` reports why.

## Build

```text
python3 scripts/build_x4pro_drivers.py
```

Outputs `dist/xteink-x4-pro/<id>/driver.elf` plus a manifest. Each ELF exports only `t5_driver_get` and is an Xtensa ESP32-S3 shared object (`e_machine` 94).

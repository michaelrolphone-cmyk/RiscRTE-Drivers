# s3-radio-iq-v1 (`radio.iq` API 1)

Self-contained receive-only driver for one ESP32-S3 modem IQ burst. The
catalog app does not call PHY, regi2c, or dump registers. Those steps run
inside this ELF.

`register_chipv7_phy` is not linked. ROM `esp_rom_regi2c_read` /
`esp_rom_regi2c_write` (`rom_i2c_readReg` `0x40005d48`, `rom_i2c_writeReg`
`0x40005d60`), `rom_pbus_rd` (`0x40005df0`), and `ets_delay_us`
(`0x40000600`) are called by absolute address from
`esp-idf` `v5.4.1` `components/esp_rom/esp32s3/ld/esp32s3.rom.ld`. They are
not ELF imports.

The register program is the published eSpDR receive path, commit
`f279bf823eee41796dfd1ac21f13e1ed9b418c82`:

- `esp32s3/src/board.h` dump, BB, AGC, PBUS, and regi2c block addresses
- `esp32s3/src/radio.c` `power_up_modem`, `tune_pll`, `configure_receiver`
  (LO `2440000000` Hz, 80 Msps circular dump, width 40, gain selector 24;
  PBUS writes `pbus_write(4,1,0)` and `pbus_write(5,1,0)` leave both TX
  groups off)
- `esp32s3/src/capture.c` dump window only: `DUMP_CTRL_CIRCULAR`, bank bit 0,
  `DUMP_CTRL_RUN`, then stop and copy words from `0x3FCB0000`

No FPGA GPIO stream. No transmitter.

Power and clock bits are the published ESP-IDF S3 definitions
(`rtc_cntl_reg.h`, `syscon_reg.h`, `clk_gate_ll.h` at tag `v5.1.4`), including
`MODEM_RESET_FIELD_WHEN_PU` and `SYSTEM_WIFI_CLK_WIFI_BT_COMMON_M`. Wi-Fi MAC
clock bit 6 is the extra bit eSpDR `radio.c` sets because
`SYSTEM_WIFI_CLK_WIFI_EN` is 0 in that header.

Build:

```sh
python3 scripts/build_s3_radio_iq_v1.py
```

Output is `dist/s3-radio-iq-v1/driver.elf` plus `manifest.json`. The only
exported function is `t5_driver_get`. Capability id is `radio.iq`, API 1.

`capture_burst(context, pairs, count)` copies `count` pair-words, 1 through
256. Return values: 0 ok, 1 not running, 2 PLL failed, 3 PBUS failed,
4 dump timeout, 5 bad argument. `start` still succeeds when PLL or PBUS
bring-up fails, so the app can acquire the capability and display that code.

## Watch install

This package is not `platform.radio@1` and it is not load-order instance 15
(`wifi`). Do not replace those. Do not make Waterfall the default app.
Clock stays `store/default.elf`.

Follow the launcher store in Watch PR #8
(`scripts/build_clock_deployment.py`, `default_app` remains `default.elf`):

1. Copy `driver.elf` and `manifest.json` to `store/s3-radio-iq/`.
2. Append to `store/boot.json` `drivers`:
   `{"manifest":"s3-radio-iq/manifest.json","instance_id":17}`.
   Instance 17 is past the PR #8 load-order list, which ends at 16.
3. Grant only the waterfall manifest. Do not add this grant to Clock:
   `{"manifest":"waterfall.json","grants":[
      {"capability":"display.output","api":1,"instance_id":5},
      {"capability":"input.touch.raw","api":1,"instance_id":6},
      {"capability":"radio.iq","api":1,"instance_id":17}]}`.
   Display and touch instance ids are the PR #8 launcher grants. The app
   acquires `radio.iq` version 1 with `instance_id` 0, the same way the
   portable client acquires the single authorized display provider.

Host check for the LO planner (no modem):

```sh
cc -std=c11 -Wall -Wextra -Werror -O1 test/s3_radio_iq_plan_test.c -o /tmp/s3-plan && /tmp/s3-plan
```

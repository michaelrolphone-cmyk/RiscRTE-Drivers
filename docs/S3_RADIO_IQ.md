# Guarded ESP32-S3 receive bursts

`s3-radio-iq-v1` 0.1.1 provides `radio.iq@1`. It remains an experimental,
unpublished component. `start` only validates the selected hardware record and
`platform.radio.iq.resource@1`; boot performs no modem register or ROM access.
The required record is `espressif,esp32s3-iq`, `radio.integrated@1`, unit 0,
features 1. The resource service must admit the actual SoC/ROM, reserve the entire
64 KiB bank at 0x3FCB0000 before heap setup, and exclude Wi-Fi/BLE/native mutation.
An idle logical station claim is not active Wi-Fi.

Each synchronous `capture_burst` claims that resource, snapshots digital state,
brings up the receiver, snapshots changed analog registers, captures 1–256 words,
stops the dump writer, disables both transmit PBUS groups, restores state and
releases the lease. A successful result never retains a radio session. No call
writes a nonzero transmit-group value. There is no transmit API, FPGA streaming,
frequency selector, or signature classifier. RF operation is not hardware-qualified.

The dump bank is initialized with impossible pair sentinels. Samples are selected
from the stopped final write index; reset and wrapping indices cannot select
stale pre-start data. The pipeline is settled before copying/releasing. Bank 3,
which upstream uses for ROM working memory, is never selected or overwritten.
Output words contain signed 10-bit I in bits 0–9 and Q in bits 10–19.

The existing API prefix is unchanged. An append-only `suspend(context)` allows
cleanup retry. Statuses: 0 OK, 1 NOT_RUNNING, 2 PLL_FAILED, 3 PBUS_FAILED,
4 DUMP_TIMEOUT, 5 BAD_ARGUMENT, 6 BUSY, 7 CLEANUP_RETAINED. Ordinary refusals
perform no MMIO. Cleanup failure retains the native lease and mapped provider;
`quiesce` returns false and `stop` does not clear custody. Retry `suspend` before
normal I/O, sleep, exit, or unload. Restored-but-unreleased leases retry only the
resource release, without touching powered-down registers.

The RF program derives from the receive path in
[eSpDR f279bf8](https://github.com/h0m3us3r/eSpDR/tree/f279bf823eee41796dfd1ac21f13e1ed9b418c82/esp32s3/src),
with its 0BSD notice beside the source. Fixed defaults are 2440 MHz LO, 80 Msps
burst input, width 40 and gain selector 24. Vendor PHY calibration is deliberately
not linked or called; PLL behavior, sensitivity and physical coexistence after
reinitialization remain untested. Host success does not establish useful RF data.

ROM addresses are the published ESP32-S3 entries: rom_i2c_readReg 0x40005D48,
rom_i2c_writeReg 0x40005D60, rom_pbus_rd 0x40005DF0, ets_delay_us 0x40000600.
See [ESP-IDF 4.4.7 ROM map](https://github.com/espressif/esp-idf/blob/v4.4.7/components/esp_rom/esp32s3/ld/esp32s3.rom.ld).
Clock/power definitions come from the S3 RTC/system headers documented in the
source. The ELF imports only `strcmp`; tuning and register operations stay in
the driver, and no raw capability is granted to Waterfall.

## Checks

- `bash test/run_s3_radio_iq_test.sh`
- `SANITIZE=1 bash test/run_s3_radio_iq_test.sh`
- `NATIVE_DRIVER_CC=/path/to/xtensa-esp32s3-elf-gcc python3 scripts/build_s3_radio_iq_v1.py`

The fixtures execute production driver code with only MMIO/ROM/CPU-cycle access
mocked. They cover lazy boot, invalid dependencies/configuration/bank geometry,
admission refusal, repeated captures, index reset/wrap, PLL/dump failure, PBUS
and release cleanup retention/retry, stop/quiesce, register restoration and
transmit-group values. Target builds check ELF32/Xtensa/DYN, sole export,
imports and hashes. No device, radio, installer or release is accessed.

The current Watch integration owns the instance ID, manifest grants and cohort
package. The old PR #8 instance/pin examples are not a valid current deployment.

# Guarded ESP32-S3 receive bursts

`s3-radio-iq-v1` 0.2.0 is the next experimental driver increment, following the
0.1.5 capture implementation shipped with Watch 1.0.7. It provides `radio.iq@1`
with the unchanged base and diagnostics prefixes. This source change does not
publish or replace any accepted Watch binary.

`start` only validates `platform.radio.iq.resource@1`; boot performs no modem
register or ROM access. The native resource service must admit the actual
SoC/ROM, reserve the entire 64 KiB bank at 0x3FCB0000 before heap setup, calibrate
and enable the PHY, and exclude Wi-Fi/BLE/native mutation. The driver neither
resets the calibrated modem nor imports vendor PHY functions. An idle logical
station claim is not active Wi-Fi. No board pins or external radio mapping are
changed.

## Compatibility and capabilities

The original `capture_burst` and `capture_burst_traced` accept 1–256 pairs with
the same 2440 MHz nominal LO, 80 MSa/s nominal rate, 40 MHz width selection and
forced gain selector 24. They never inherit settings from another call.

Consumers may use `risc_radio_iq_extended_api_v1` only after checking that the
base API's `struct_size` covers the member they want to read. The extension
appends `capabilities`, `capture_configured`, and `capture_configured_traced`
after the complete original diagnostics prefix. All calls must be serialized;
synchronous trace re-entry is refused without releasing the outer call's lease.

`capabilities` copies the format, supported discrete settings, raw-code bounds
and defaults without claiming or reading hardware. Configured captures accept
1–8192 pairs, including a complete 256/512/1024/2048/4096/8192-point FFT input.
Every successful call returns one contiguous interval of one acquisition.
Separate captures are discontinuous; callers must not concatenate them to
manufacture a larger FFT input or continuous recording.

The per-call settings and returned format are declared in
[`RiscRadioIqV1.h`](../sdk/driver/RiscRadioIqV1.h):

| Setting | Implemented values | Meaning |
| --- | --- | --- |
| Center | 1,841,666,667–2,790,000,000 Hz | Upstream LO planner range; auto normal or 5/6 conversion; quantized nominal center returned |
| Sample rate | 16,000,000 or 80,000,000 pairs/s | Upstream dump-clock selections, nominal |
| Bandwidth | 20,000,000 or 40,000,000 Hz | Front-end width selection, not a measured passband |
| Gain selector | 0–127 | Forced receiver selector; not dB and not live AGC |
| RF gain | 0–511 or `RISC_RADIO_IQ_AUTO` | Raw RF PBUS word or the stage selected by the forced gain |
| BB gain | 0–127 or AUTO | Raw BB code, programmed as `0x180 | code`, or selected stage |
| Filter | Mask `0x3f3f` | I code in bits 0–5, Q code in bits 8–13 |
| DC offsets | Four values, each 0–511 or AUTO | Raw PBUS codes; individually restored on exit |
| IQ correction | Mask `0x3f1f` or AUTO | Amplitude code in bits 0–4, phase code in bits 8–13 |

AUTO is `0xffffffff`; this API does not expose the upstream wire protocol's
separate DC sentinel. NULL settings selects the default initializer. No
settings persist. Invalid settings are rejected before the native claim or any
MMIO, ROM or SRAM access. Format reports the quantized LO, nominal rate, selected
width, exact returned count, effective raw stage/correction codes and LO mode.

Each output word contains signed 10-bit I in bits 0–9 and signed 10-bit Q in bits
10–19; all other bits are zero. Components span −512…511 and their normalization
scale is 512. Capability and format flags explicitly identify coherent bursts,
nominal frequencies and uncalibrated amplitude. They do not claim dBm, calibrated
sensitivity, measured sample-clock accuracy, or hardware qualification throughout
the upstream tuning range. Device-level qualification remains required for this
new extension. There is no transmit API or FPGA streaming.

## Capture and rollback

Each capture claims the native resource, snapshots changed digital and analog
state, configures the receiver and performs one burst. The writer is stopped,
its bank selection cleared, and the pipeline settled before CPU sample reads.
Both transmit PBUS groups are explicitly kept off, manual DC values are restored,
all changed registers are restored, and the native lease is released. Failure to
restore or release retains custody for an explicit cleanup retry.

The bank is initialized with impossible pair sentinels while CPU-owned. While
RUN is set, progress polling accesses only the MMIO write index, with the
existing bounded cycle timeout. The first cursor transition is discarded because
RUN may reset a stale index. The live cursor is saved before STOP because STOP
may reset it again. After settling, a bounded 1024-pair sentinel search locates
the committed endpoint, including late pipeline writes. Every requested sample
must be present. A fully overwritten bank, exhausted endpoint guard, missing
sample, or progress timeout fails closed. No CPU access to active capture SRAM
is permitted. Only the leased bank is selected; the ROM's bank 3 is untouched.

Configured captures clear valid-sized output buffers on failure (format keeps
only its initialized `struct_size`). Invalid pointers or pair counts cannot
authorize clearing arbitrary memory. Callers must check the result before using
any output. Statuses remain: 0 OK, 1 NOT_RUNNING, 2 PLL_FAILED, 3 PBUS_FAILED,
4 DUMP_TIMEOUT, 5 BAD_ARGUMENT, 6 BUSY, 7 CLEANUP_RETAINED.

`suspend(context)` is idempotent. On CLEANUP_RETAINED, retry it before normal I/O,
sleep, exit or unload. `quiesce` returns false and `stop` preserves custody until
cleanup succeeds. A restored-but-unreleased lease retries only native release,
without touching powered-down registers. The synchronous stage callback is
available for configured captures and is called only with the writer stopped
and bank selection clear.

## Primary references

The receive program follows the 0BSD
[eSpDR radio.c at f279bf8](https://github.com/h0m3us3r/eSpDR/blob/f279bf823eee41796dfd1ac21f13e1ed9b418c82/esp32s3/src/radio.c),
including `valid_setting`, `set_width`, `configure_receiver`, `radio_dump_control`
and `reconfigure`. The copied license is beside the driver. The
[board register definitions](https://github.com/h0m3us3r/eSpDR/blob/f279bf823eee41796dfd1ac21f13e1ed9b418c82/esp32s3/src/board.h)
identify the 64 KiB bank and `0x00010000` 16 MSa/s dump-clock bit. These radio-side
registers are reverse-engineered upstream definitions, not a public Espressif
SDR API. The existing copied
[LO planner](https://github.com/h0m3us3r/eSpDR/blob/f279bf823eee41796dfd1ac21f13e1ed9b418c82/esp32s3/src/lo_plan.h)
uses a nominal 40 MHz crystal. Upstream
[capture.c](https://github.com/h0m3us3r/eSpDR/blob/f279bf823eee41796dfd1ac21f13e1ed9b418c82/esp32s3/src/capture.c)
documents ownership, sentinel endpoints and delayed ADC writes. This driver
uses a single stopped burst, without upstream's multi-bank FPGA stream.

ROM addresses are the published ESP32-S3 entries: rom_i2c_readReg 0x40005D48,
rom_i2c_writeReg 0x40005D60, rom_pbus_rd 0x40005DF0, ets_delay_us 0x40000600.
See the [ESP-IDF ROM map](https://github.com/espressif/esp-idf/blob/v5.4.1/components/esp_rom/esp32s3/ld/esp32s3.rom.ld).
Clock/power definitions come from the S3 RTC/system headers documented in the
source. Tuning and register operations stay inside the driver; consumers receive
copied data and metadata, never raw hardware authority.

## Checks

- `bash test/run_s3_radio_iq_test.sh`
- `SANITIZE=1 bash test/run_s3_radio_iq_test.sh`
- `NATIVE_DRIVER_CC=/path/to/xtensa-esp32s3-elf-gcc python3 scripts/build_s3_radio_iq_v1.py`

The production-driver fixture checks every returned pair through 8192 at both
rates and widths, startup delay, index reset/wrap, cycle-counter rollover,
in-flight writes, full-ring/guard exhaustion, missing samples, metadata and
compatibility, setting bounds, PLL failure/readback failure, manual DC application
and restoration, PBUS/native-release cleanup retention/retry, repeated capture,
re-entrant tracing, lazy admission, native refusal, register restoration and
receive-only behavior. `mprotect(PROT_NONE)` revokes CPU access while SRAM is
writer-owned. ASan/UBSan may run with `ASAN_OPTIONS=detect_leaks=0` where the
executor uses ptrace and LeakSanitizer cannot run. Target builds check
ELF32/Xtensa/DYN, exports, imports and hashes. Host success does not establish
physical RF performance. No device, installer or release is accessed by tests.

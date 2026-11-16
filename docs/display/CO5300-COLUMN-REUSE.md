# CO5300 persistent column-window reuse (default off)

`RISC_CO5300_REUSE_COLUMNS=1` selects bounded native CASET reuse. The default
remains zero. This option is independent of queued pixels, stripe size and the
optional completed-pixel shadow. It changes no row ordering, rotation, pixel
format, RAMWR/RAMWRC selection, DMA ownership or visual content.

## Register authority

The CO5300 CASET register stores the column interval separately from RASET.
All accepted native row operations use the same build-selected full-width
interval. `columnWindow()` learns that interval only after its own CASET command
has completed and manual CS has successfully deasserted. Initialization table
writes are not assumed to have established it. Cached knowledge never substitutes
for transfer completion or owner/ready/closing/failed admission.

RASET, RAMWR and brightness preserve the interval. Any other command invalidates
knowledge before submission, including CASET itself, reset, sleep/display state
and MADCTL. Every existing failed-state transition clears knowledge. Close clears
it before cleanup; reopening starts with zero-initialized state. An incomplete
or unknown operation cannot establish authority, and later cleanup cannot bless
it. No new allocation or panel readback is required.

## Qualification and limits

`tests/test_co5300_column_reuse.py` compiles the actual backend with four panel
profiles (including nonzero gaps and rotation 180), synchronous/queued operation,
and multiple stripe sizes. An independent command/register/panel-memory oracle
models persistent columns, independently mutable rows, RAMWR cursor restart and
RAMWRC continuation. It verifies initial black fill, full/identical/dirty frames,
noncontiguous accepted stripe updates, shadow OOM, smaller DMA fallback, reset,
reopen and exact physical pixel mapping. Failure injection covers every actual
submission/reap position of a changed frame, CASET CS failures, initial CASET
failure, retained unknown completion and no later DMA mutation on rejected calls.
Default-off historical queued tests remain separate. Sanitizer leak checking is
disabled because intentionally retained unknown-custody child processes exit
without releasing owned resources; ASan/UBSan otherwise remain enabled.

For the selected 466x466, 16-row Badge profile (pixel command 2C, no separate
RAMWR), an already-known full changed frame uses 60 transactions instead of 90.
The first unknown column window adds one. All 434,312 pixel bytes remain.
At 40 MHz the eliminated thirty 64-bit CASET command/address/data transactions
save only 48 microseconds of ideal wire time; software/CS overhead is additional
but not measured on hardware. Host CPU tests include mock packet copies and are
not physical SPI timing or an FPS claim. Mixed/error epochs require conservative
command accounting: decoding `3 * dirty_windows` as an exact transaction count
would be wrong after selecting this option. Integrators must record the selector
and adjust diagnostic estimates accordingly.

## Primary protocol evidence

- CO5300 datasheet, CASET/RASET pages 159–162, RAMWR page 163:
  https://files.waveshare.com/wiki/common/Co5300_Datasheet.pdf
  SHA256 `2ad0461aec31aa046e2661f52f43e9c36b36218c58c53f07217c73bef7d710c6`.
- Waveshare-pinned Arduino CO5300 `writeAddrWindow` already retains register bounds
  between writes and only reissues changed windows:
  https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-2.06/blob/b099739ad0e33b34e5fbaae77f02bd84805d79a3/examples/arduino/libraries/Arduino_GFX/src/display/Arduino_CO5300.cpp

This is not a held-CS chaining or hardware-mirror implementation. Those require
separate address-order/protocol qualification and remain research. No device
operations or physical performance validation are implied by these tests.

## Build and decode

The existing native builder accepts `--prototype --reuse-columns`; the CMake
option is `RISC_BADGE_REUSE_COLUMNS`, default OFF. Qualification checks the exact
single `-DRISC_CO5300_REUSE_COLUMNS=0/1` definition, rejects contradictory or
undefined selection, forwards it to linked native evidence validation, and emits
`co5300_transport.column_reuse` in build-proof.json. Successful native opens log
`PANEL_TRANSPORT stripe_rows=N queued=0/1 column_reuse=0/1` through the existing
bounded diagnostic adapter. This adds no per-row logging. Old synchronous builds
with neither optional transport feature retain their prior no-epoch behavior.

The compatible decoder successor is `f7eb9e30f2d91abd98db4289feb73c59f7f47c52`
(owning Controllers repository), based on queued decoder `d758dd80`. For this
Badge profile additionally specify `--no-ramwr-before-pixels`; do not silently
reuse the legacy separate-RAMWR default. Reuse-enabled aggregate window counts
produce conservative CASET bounds, not a fabricated exact invalidation count.

An interleaved twelve-repeat host mock run (five warmup + thirty measured full
changed frames per process) gave milliseconds/frame min/median/max:
- off: 0.1770 / 0.1949 / 0.5692
- on: 0.1797 / 0.1876 / 0.2160

This small/noisy host difference does not establish a meaningful device-speed
improvement. The independent structural result is one fewer command transaction
per changed stripe while CASET authority holds. It is not a 40–60 FPS solution.

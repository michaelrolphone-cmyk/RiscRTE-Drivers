# cst9217-touch 0.1.0

Experimental, unpublished source provider for the CST9217 touch controller.
This is a software prerequisite for the Waveshare ESP32-S3-Touch-AMOLED-1.75C
port, not a board profile, firmware release, hardware qualification or full
round-display implementation.

## Source evidence

The known-working user firmware is [The-Badge at
76172dfaf0fa0ffedee91a61da3261afdde56ce3](https://github.com/curisama/The-Badge/tree/76172dfaf0fa0ffedee91a61da3261afdde56ce3).
Its resolved Waveshare CST9217 managed component is version 2.0.0,
registry component hash
f755f8e3ae61df23413256a6a447e367a4b844538229c183ff0e89dbbdc15319.
The protocol reference is [Waveshare-ESP32-components at
9daffb7168b3c093a330da700988a22092294eef](https://github.com/waveshareteam/Waveshare-ESP32-components/tree/9daffb7168b3c093a330da700988a22092294eef/display/touch/esp_lcd_touch_cst9217).
The board BSP is separately version 3.0.0 at
010c5fb39ea7f0e996efd1f8e162cc70e569e57f. Those are distinct sources.

The 1.75C has a 466x466 CO5300 display, CST9217 touch, AXP2101 power and
QMI8658 IMU, 32 MB flash and 8 MB PSRAM; it has no external RTC or SD.
Do not substitute the FT3168 or the plain 1.75/2.06 board profile.
The BSP uses I2C SDA15/SCL14, touch reset2 and interrupt11.

## Contract and implementation

The driver uses existing driver ABI 2 and input.touch.raw v1. It requires
hardware.device, the loader-scoped i2c.bus, platform.gpio, platform.clock,
power.rail and platform.sync, all API 1. No public ABI is added.

Before any service call or hardware I/O it requires exactly one of each
dependency with valid table size/version and required callbacks. The typed
hardware record must be `hynitron,cst9217`, revision `unspecified`, `touch.i2c`
version 2 with the full v2 record. Admission is intentionally limited to the
1.75C source configuration: bus101/controller0, SDA15/SCL14 at400kHz,
466x466, address0x5a, active-low resetGPIO2 with10/50ms timing, active-low IRQ11
without pull-up/output, no alternate address and no separately controlled
power pin. Reserved bytes and unused SPI pins are checked. The v1 record is
not reinterpreted as v2. This is configuration admission, not chip revision or
physical wiring detection. The loader supplies the correctly scoped contexts;
the public bus API does not expose an independent identity query.

The provider checks its separately selected power.rail observer for verified,
enabled3300mV. It never calls set_enabled, claims PMIC0x34 through the touch
bus, or changes CPU-shared DCDC1. It then claims GPIO2 initially LOW, checks
readback, waits at least10ms, writes/checks HIGH and waits at least50ms. Each
wait checks the monotonic clock around the yielding sleep. This uses the
existing generic GPIO owner contract, including initial-level staging before
output enable, rather than a caller-only preparation adapter.

CST itself retains the reset token until all subscriptions and touch transfers
have drained and the touch bus claim has successfully released. A reset or
probe failure uses the same checked cleanup. False/nonzero claims are retained
for cleanup but never used for I/O. A failed bus release does not release reset;
a failed GPIO release retains its token and all borrowed dependencies for retry.
GPIO1 belongs separately to the native display. The older board preparation
helper remains source evidence and must not run alongside this provider.
Polling is explicit; IRQ11 is declared but not claimed or driven.

Register selection is a two-byte big-endian write with STOP, followed by a
2 ms yielding delay and a separate read. A combined repeated-START transfer
is not equivalent. Startup mirrors the vendor's duplicated D101 mode command,
10 ms delay, checkcode/resolution/project reads; the reported
dimensions must agree with the admitted466x466 configuration. No undocumented chip signature
is invented. Each transfer has a 20 ms budget. A poll services one report,
bounded by two transfers and one 2 ms yield, even when a larger report budget
is supplied.

A D000 report is ten bytes, with AB marker at byte6, single-contact count in
byte5, active status6 in the low nibble of byte0 and packed 12-bit coordinates.
Valid inactive states release the contact. Coordinates remain physical and
unrotated; geometry/transforms belong to the existing higher-level stack.
The provider emits down/move/up transitions, fans out through the existing
bounded queues and invalidates snapshots after malformed or failed reads.
Consumers see GAP and must resynchronize. Queue overflow also signals GAP,
but leaves a successfully sampled snapshot available. Startup's empty contact
snapshot is initialized state, not evidence that the physical screen was read.

Runtime's existing owner-task-only platform.sync guard covers the entire
select/delay/read sequence and provider state. Calls from another task or
reentrant calls fail without entering the operation. This is not a cross-thread
capture-task mutex. The provider imports no privileged FreeRTOS operations and
performs no compare-and-set against its PSRAM-backed BSS. Failed guard unlock or
destroy also retains cleanup custody and dependencies.
Failed cleanup retains the claim and dependencies for checked quiesce/stop
retry; it never reports release success on failure. Live subscriptions prevent
quiescence. Generation tokens remain monotonic across restart. Invalid clocks
at startup (including after probe) and during sampling fail closed.
Unlike the vendor reference's retry/reset loop, this provider makes one bounded
attempt per poll and reports a gap without resetting a shared device or bus.

## Reproduction and limits

Use Runtime `13eca114cb58469cf6004fafc3da84fbd9d29beb` for these commands:

- `bash test/run_cst9217_touch_test.sh RUNTIME_PATH` runs normal and ASan/UBSan
  protocol/stream tests.
- `python3 tests/test_badge_touch_composition.py --runtime RUNTIME_PATH`
  runs production dependency/reset/start/stop composition. Add `--sanitize`
  for ASan/UBSan; on ptrace-hosted systems use `ASAN_OPTIONS=detect_leaks=0`.
- Install the existing pinned native toolchain, then run
  `python3 scripts/build_cst9217_touch.py --runtime RUNTIME_PATH`.

The fake boundaries enforce the real service ABIs, owner guard, GPIO2 custody,
no GPIO1 access, supply observation, STOP-separated transactions and timing.
Coverage includes every typed config field, missing/duplicate/malformed service
tables, reset/readback/delay failures, false/nonzero and true/zero claims, every
probe transfer, mismatched dimensions, invalid clocks, active subscriptions,
failed bus/GPIO/sync cleanup with retry, reentrancy, stream corruption, overflow,
fanout and stale tokens. The production composition no longer relies on a
caller-created stop-before-reset-finish wrapper. The independent historical
board-preparation tests still cover the earlier helper's own contract.

`scripts/cst9217_sdk.py` stages one temporary header root from existing files,
using Drivers capability headers with the selected Runtime headers authoritative
for shared names. This avoids duplicate definitions from quoted adjacent
includes. It neither changes the shared SDK nor invents another ABI. CI checks
out the exact Runtime revision, builds the actual PR source, rejects unexpected
ELF imports/exports and preserves the candidate ELF and derived manifest.

These are source/host/target-build results, not a published package, linked-device
boot result or physical qualification. The separate Badge Home graph assembles
the selected provider and existing Watch default. Electrical timing, physical
controller revision, orientation and real touch behavior remain unmeasured.
No reset/retry loop, unrelated sensor, external RTC or SD capability is added.
User-reported Badge success is not RiscRTE validation.

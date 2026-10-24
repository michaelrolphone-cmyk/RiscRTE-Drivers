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

The driver uses existing driver ABI 2 and input.touch.raw v1, with i2c.bus v1
and platform.clock v1 dependencies. It adds no capability fields or board-name
branches. The generic controller claims address 0x5a; power and reset must
already be established by the future board provisioning/profile layer.
No GPIO/reset owner or interrupt provider is introduced here. Polling is explicit.

Register selection is a two-byte big-endian write with STOP, followed by a
2 ms yielding delay and a separate read. A combined repeated-START transfer
is not equivalent. Startup mirrors the vendor's duplicated D101 mode command,
10 ms delay, checkcode/resolution/project reads; it uses the reported nonzero
dimensions (up to 4096), not a hardcoded 466. No undocumented chip signature
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

A mutex covers the entire select/delay/read sequence and provider state.
Failed cleanup retains the claim and dependencies for checked quiesce/stop
retry; it never reports release success on failure. Live subscriptions prevent
quiescence. Generation tokens remain monotonic across restart. Invalid clocks
at startup (including after probe) and during sampling fail closed.
Unlike the vendor reference's retry/reset loop, this provider makes one bounded
attempt per poll and reports a gap without resetting a shared device or bus.

## Reproduction and limits

Run bash test/run_cst9217_touch_test.sh for normal and ASan/UBSan host tests.
The fake bus rejects combined transactions and checks timing, register shape,
dimensions, bounds, transitions, fanout, queue overflow, corrupt reports,
startup transfer failures, invalid final clock, reentrant access, stale tokens
and retained-release custody. LeakSanitizer is disabled; this is not a leak test.

Run bash scripts/install_native_driver_toolchain.sh, use its reported PATH,
then python scripts/build_cst9217_touch.py for the existing pinned native GCC15
Xtensa toolchain. CI builds the exact PR head, checks manifest/ELF architecture
and exported entry point, and preserves the candidate ELF plus derived manifest.
These are build artifacts, not published or admitted firmware packages.

Remaining board integration: power/reset sequencing, concrete I2C/profile
selection, CO5300 output provider with conservative safe insets, touch/display
coordinate agreement, shared Watch boot provisioning and on-device validation.
No board profile selects this provider yet. Electrical timing, actual controller
revision, reset/recovery, orientation and real touch behavior remain unmeasured.
User-reported Badge success is not RiscRTE validation.

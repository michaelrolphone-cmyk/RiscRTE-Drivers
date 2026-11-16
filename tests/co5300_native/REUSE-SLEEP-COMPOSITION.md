# Shadow-enabled sleep/power composition

Production remains9a9952c. The new C facade/C++ native test links the actual `facade/driver.c`, common display provider and native CO5300 implementation with reuse explicitly enabled. It first proves an identical completed frame skips every window; then Light-style prepare/resume, close-timeout rollback, three Deep reset-hold/resume cycles, rejected Deep hold rollback and active-frame cancellation exercise the actual callbacks. Every reopen asserts no prior shadow/allocation/authority, every resumed frame transmits all rows/windows before brightness restoration, and successful close asserts freed shadow. Normal and ASan/UBSan lanes pass (LeakSanitizer excluded under executor ptrace as before).

Selected source path:
- `components/co5300/facade/driver.c`: `panel_prepare_sleep` sets suspended/closing and calls `hw_stop`; `hw_stop` releases native ownership only after `backend->close` succeeds.
- `panel_resume`: complete prior cleanup; native open; brightness0; all HEIGHT framebuffer rows; restore requested brightness. It never assumes panel contents survived sleep.
- `panel_prepare_deep_sleep`: call ordinary prepare/close first, then claim reset and enable scoped GPIO hold. Failure releases/retains according to existing custody contract; rollback releases reset before reopening.
- `Drivers/legacy_power_compat/bridge.h` role3 only forwards the display provider's callbacks, including tagged Deep/resume suffixes. It has no independent PMIC/reset/pixel writes.
- `boards/waveshare-esp32-s3-touch-amoled-1.75c/supply/driver.c`: selected DCDC1 observer's `set_enabled` always returns false; facade only reads rail information. No selected rail-changing operation invalidates panel memory behind a live shadow.
- Native startup/reset/fill belongs to open, before shadow authority. `closePanel` frees shadow and resets PanelState only after existing SPI/bus/pad cleanup. Direct internal fill invalidates shadow before any command (including failure), separately covered by native reuse matrix.

This is source and host-composition evidence, not physical RAM retention, voltages, sleep entry or wake acceptance. No device operation occurs. The modeled facade uses the existing provider sync and GPIO/rail mocks and real native SDK-declaration SPI mocks; its rail-write callback asserts if invoked.

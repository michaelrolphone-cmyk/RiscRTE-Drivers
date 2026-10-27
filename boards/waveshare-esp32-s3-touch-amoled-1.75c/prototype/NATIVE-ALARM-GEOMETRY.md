# Explicit native Alarm / Countdown presentation

Selected only by `--native-alarm-geometry` together with `--native-alarms`,
`--native-daily`, `--include-files` and `--foreground-idle`. Exact build and
execution receipts record `native_alarm_geometry`. Unselected profiles keep
existing renderers; native UTC scheduling alone does not imply native geometry.

This profile uses the shared native safe-content primitives and original font
faces, with one layout shared between drawing and hit testing. It does not
resize a legacy paper or logical framebuffer. Alarm time is a once-only local
civil choice resolved to an absolute UTC deadline; Countdown is HH:MM:SS,
independent of the clock's 12/24-hour display preference. Nested editors keep
drafts separate from explicit Arm/Start. Back/Done do not silently save.

The selected service, tagged sleep tickets, clock-edit protocol and seven bound
keys remain those documented in NATIVE-ALARMS.md. No recurrence, sound, volume,
haptic output, new hardware grant or direct occurrence-ledger access is added.
Unknown outcomes retain custody; uncertain saves must remain visible and locked
until exact recovery. A rendered screen is not proof of successful persistence.

## Owner checks — physical results pending

After an owner-authorized installation and accurate Settings time entry:

- Open each app from Springboard; verify native text/buttons fit the circle.
- Edit each field, including hour boundaries, noon/midnight and AM/PM in12-hour
  mode; verify Countdown stays duration-formatted. Inspect the saved Alarm date.
- Use touch Back/Done and brief physical PWR for nested Back; leave only from
  the root after normal safety checks. Do not hold PWR through factory hard-off.
- Leave an unsaved nested editor idle through repeated returning Light wakes;
  the same page/draft must remain, with no implied schedule Save.
- Explicitly Arm/Start, then verify actual timed Home Light/Deep wake and visible
  dismissal as described in NATIVE-ALARMS.md. Also let a due alert interrupt an
  open editor and verify the draft after dismissal.

Only matching source, owning-test, full Runtime execution and image receipts
establish software qualification. Modeled circular rasters are not device
photographs; physical touch, panel aperture, RTC accuracy and wake timing remain
unverified. Keep the delivered morning image available as a separate rollback.

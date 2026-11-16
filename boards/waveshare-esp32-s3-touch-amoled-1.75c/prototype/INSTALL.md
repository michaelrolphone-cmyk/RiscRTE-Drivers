# Round Badge prototype candidate — device testing pending

Morning interaction candidate: retains the product-sized AppData mount, native
dark-until-complete-frame display gate, CST9217 handoff repair, and selected
180-degree panel transport. Home initializes an unset clock to January 1, 2026;
Settings can set actual time. Watch-native Springboard uses reported dimensions
and the shared native UTC/timezone service. Exact foreground Light selection is
recorded in store-proof.json. This new combined image is physically untested;
prior boot/display observations do not qualify these changes.


Only for the aluminum The-Badge using Waveshare ESP32-S3 Touch AMOLED 1.75C:
32MiB flash, expected 8MiB octal PSRAM, CO5300 466×466 offset6/0,
CST9217 touch. This is not a Waveshare 2.06 image. Hardware is untested.

The full image replaces **all32MiB**, including NVS, credentials, firmware,
preferences and application data. Do not erase first. Establish an owner-approved
backup and recovery plan before any device write. Secure Boot/flash-encrypted
devices are not qualified; do not use force/ignore/security-disable switches.
No serial or physical device operations were performed to create this package.

1. Extract this ZIP and run `sha256sum -c SHA256SUMS` in its directory.
   Exact source pins, image hash, regions and software evidence are in
   `package.json`, `source-package.json` and the admission/execution receipts.
2. Confirm the board and security state, USB data cable and the correct port.
   Use esptool5.6.0 (`python -m esptool version`). Replace `PORT` below with the
   owner's confirmed port. No command should select a port implicitly.
3. If separately authorized, back up the current complete image first:

```sh
python -m esptool --chip esp32s3 --port PORT read-flash 0x0 0x2000000 badge-before.bin
sha256sum badge-before.bin
```

Record device/security identity and retain that backup privately; it can contain
credentials. A backup from a different board is not this board's recovery plan.

4. After explicit device-write authorization and backup, install and verify:

```sh
python -m esptool --chip esp32s3 --port PORT --baud 460800 write-flash --flash-mode dio --flash-freq 80m --flash-size 32MB 0x0 badge-prototype-combined.bin
python -m esptool --chip esp32s3 --port PORT verify-flash 0x0 badge-prototype-combined.bin
```

DIO/80MHz/32MB are the actual SDK-emitted image headers. The retained
`flasher_args.json` describes native constituent inputs; the combined image also
contains bootfs and preformatted AppData. Do not force QIO.

5. Capture115200-baud serial output from reset to first frame and app return.
   For example, after the flashing tool has closed the port:

```sh
python -m serial.tools.miniterm PORT 115200 --raw
```

Save the complete timestamped capture with the combined-image SHA256. Follow
TEST-ON-DEVICE.md. A write/readback verification is not a successful boot test.
On an unset cold/reset clock, Home initializes the user-selected placeholder
January 1, 2026 at midnight in the saved timezone (UTC when missing). This is
not synchronized current time. Open Settings, choose Time Zone, then Set Time;
edit the calendar fields and Save. A verified save reads back native time.
Valid time on app reentry and admitted deep wake is preserved, not overwritten.

## Pocket time-check behavior

The default policy is staged HYBRID: brief PWR or 15 seconds of Home inactivity
enters Light first, then the configured subsequent interval (300 seconds when
absent) permits Deep only on timer expiry without an intervening key or alarm.
Valid saved mode and independent idle/deep timers override these defaults.
PWR wakes through GPIO3; BOOT retains its recovery role. Touch/motion are not
wake sources. Set accurate time once in Settings after cold boot; admitted deep
wake retains internal RTC-backed time. An ordinary reset invalidates Runtime
time authority; Home then applies the explicit initial-date placeholder again.
Hardware current, wake timing and RC-clock drift are not yet measured. Read the
sleep-cycle section in TEST-ON-DEVICE.md before relying on pocket battery life.

Check `store-proof.json` for `foreground_idle.enabled`. With the explicit
foreground profile, Settings, Springboard, Wi-Fi Settings, Calculator, Stopwatch
and Battery use returning untimed Light sleep at safe shared idle boundaries.
The app invocation and unsaved RAM draft remain intact; there is no automatic
Home/Save/Cancel. Home defaults to staged Hybrid: Light first, then Deep after
the configured subsequent timer. Explicit saved sleep modes still win. Without this opt-in,
automatic idle remains Home-only. Hardware Test can still remain awake, and
legitimate active native work can defer sleep. This is not an overall pocket-safe
or battery-life claim. See FOREGROUND-IDLE.md and TEST-ON-DEVICE.md.

A `QMI_IDLE reason=...` refusal means sensor state/sleep is not qualified; do not
infer low current or override the refusal. Light-retained drafts are not durable
against power loss, reset or a hard PMIC cutoff.

Home uses explicit native UTC authority and the shared saved timezone preference
(UTC when unset). It does not seek or seed an external RTC. The legacy external-RTC
profile remains available for other devices.

## Button and launcher checks

A brief PWR press in Springboard returns Home. A brief PWR press on Home
requests the saved sleep policy; release the button so the held-key safety
check can admit sleep. Use another brief PWR press to wake. The PMIC long-hold
hard-off function is not changed. Do not treat a dark screen alone as proof of
CPU sleep. Verify the sequence in TEST-ON-DEVICE.md before pocket use.

Springboard uses Watch presentation at the display-reported native dimensions,
not the generic unknown-refresh/paper fallback. Its toolbar reads the same
native time and saved timezone as Home; it has no authority to set the time.

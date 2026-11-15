# First device observations requested

Morning interaction candidate: retains the product-sized AppData mount, native
dark-until-complete-frame display gate, CST9217 handoff repair, and selected
180-degree panel transport. Home initializes an unset clock to January 1, 2026;
Settings can set actual time. Watch-native Springboard uses reported dimensions
and the shared native UTC/timezone service. Exact foreground Light selection is
recorded in store-proof.json. This new combined image is physically untested;
prior boot/display observations do not qualify these changes.


Status: **device testing pending**. Record `package.json` combined SHA256,
board model, flash/security identity, power source and timestamp for every run.
Start with one reset-to-Home serial capture plus a photo of Home and Hardware
Test; report the first failed transition. No physical results are pre-filled.

| Check | Expected observation | Result |
|---|---|---|
| Boot | Source/stage logs;32MiB flash, expected8MiB PSRAM;bootfs/AppData mount;graph/import admission;display/touch;completed first present;no reset loop | Pending |
| Home | Full466raster;readable safe content;initial2026-01-01 midnight in saved zone on unset cold start; after setting time, no external-RTC TIME CHECK ERR | Pending |
| Navigation | Home left swipe opens Apps; brief PWR on Springboard returns Home;each listed target opens;Back returns Home;missing-app fallback is not the normal route | Pending |
| Hardware Test | RGB565 red/green/blue bars at conservative brightness;raw touch positions;center/cardinal touches;safe controls visible | Pending |
| Touch | Center/cardinal taps,drag-out/release,re-entry,rapid launch/Back;no phantom held press | Pending |
| Settings | Set date/time;readback success;timezone and12/24h;return Home;ordinary reset applies initial2026 placeholder while saved preferences remain; valid deep wake and app return preserve the edited time | Pending |
| Stopwatch | Before supplied time,UTC-unavailable is honest;after setting time,start/pause/reset;return/reopen;ordinary reset does not fabricate elapsed civil time | Pending |
| Battery | USB/battery/charging observations;unknown SOC staysunknown;compare to independent equipment before claiming accuracy | Pending |
| Motion | Unselected: optional Hardware Test motion reports unavailable; no motion wake/sampling; separate sensor idle guard | Host guard checked; physical current unmeasured |
| PWR | Short/long events only where supported;no GPIO0 interference or unintended shutdown;factory long-hold cutoff remains unchanged | Pending |
| Wi-Fi | Open Networks,scan,connect/disconnect with owner network;credential save/reopen;no claims of throughput,coexistence or sleep reliability | Pending |
| Display | Conservative brightness;launch/return,failed-close recovery;no DCDC1 gating | Pending |
| Endurance | Repeat10cold/warm boots and20launch/return cycles;retain full serial,timings,heap/error counters and candidate identity | Pending |

Sleep is now an explicit device-qualification priority, not a display-idle claim:

1. Set accurate time once in Settings, select Hybrid, and record both idle and subsequent Deep-delay seconds.
   The build fallback is 15 seconds and can be changed; saved preferences win.
   Verify Home matches the timezone selected in Settings, including UTC and a
   supported daylight-saving zone. Check both 12-hour and 24-hour formats;
   missing timezone defaults to UTC, not a hardcoded local zone.
2. Confirm the physical board follows the pinned 1.75C schematic. Observe
   GPIO3 released LOW and PWR pressed HIGH before relying on wake. Do not probe
   with an output driver or alter shared rails.
3. Verify brief PWR and automatic idle each enter Light first; brief PWR wakes
   back into Home without a reboot. Repeat without input: only expiry of the
   configured subsequent timer may enter Deep. An earlier/equal alarm must wake
   its alert instead. Capture native entry diagnostics and the later Deep reset.
   Separately check explicit saved Light and Deep choices remain functional.
   Confirm time advances across 10, 100 and repeated overnight cycles. Compare
   elapsed UTC with an independent clock; record drift, not an assumed accuracy.
4. While asleep, rub/tap the display and move the badge. Neither is configured
   as a wake source. A deliberate brief PWR press must wake to a readable Home.
   Release it normally. Holding or reasserting PWR must not cause rapid deep-reset
   loops. Do not hold it through the PMIC's existing hard-off duration.
5. Check short PWR behavior after wake, idle return, rapid press/release, failed
   storage writes, Wi-Fi disconnect and interrupted display/touch preparation.
   Any retained/unknown cleanup must block entry rather than discard data.
6. Measure battery-side current in awake, display-dark and native-deep states,
   both USB disconnected and connected. Report instrument/sample interval,
   board revision, battery voltage, peripherals and exact firmware hash. Do not
   infer sleep or battery life from screen darkness or host tests.
7. Verify saved settings/credentials/app records survive repeated deep cycles.
   Ordinary reset intentionally invalidates civil-time authority; set time again.

No external RTC, SD, LoRa, IR or haptic device is selected. QMI8658 acceleration is unselected for pocket power;tap/wake and audio remain
unselected (CAPABILITIES.md). Stop testing on unexplained heating,reset loops,
unintended power-off or a wedged bus; preserve logs before recovery.

A useful failure report includes the candidate hash, power source, exact action,
expected/actual screen, full serial from reset, repeatability and a photo/video.
Do not include Wi-Fi passwords or other secrets. Current host/native receipts
establish software behavior under their modeled boundaries, not these results.

Check for `QMI_IDLE ready=1 state=power_down` before relying on the sleep path.
Unsupported inherited mode is an explicit refusal, not an automatic reset or
mode conversion. Record the exact reason and prior firmware/power-cycle history.
Do not erase data or force a register/rail override to hide this diagnostic.
The source-backed mode owner is tested, but battery-side current remains unknown.
For an enabled foreground-idle cohort, leave each of the six selected shared
apps idle through repeated Light cycles. Verify the same screen, brightness and
touch coordinates return; no Home navigation, implicit Save/Cancel, or app exit
may occur. In Settings retain an unsaved edited value, in Calculator retain a
pending expression, and in Stopwatch retain elapsed/running state. After each
wake hold/release PWR, then make a fresh press: delayed wake latches must not
navigate or activate a control. Repeat with saved idle intervals, absent/corrupt
preferences, and radio work in progress; legitimate work must not be cancelled.
Record actual light-sleep diagnostics and battery current separately from Deep.
Do not power-cycle to test RAM retention; a hard reset is not a Light wake.

Hardware Test is still a known awake-only diagnostic gap. With foreground idle
disabled, other non-Home apps can also remain awake indefinitely. Do not infer a
universal pocket-safe result from successful Home or selected-app tests.

## Explicit button sequence

Repeat Home → Apps → brief PWR → Home → brief PWR → sleep → release →
brief PWR → Home. Test both saved Light and Deep policies, including repeated
cycles and a key held before entry. Expect safe refusal while held, not a reset
loop. Record native sleep/reset diagnostics as well as screen/touch state.
The same physical wake button is required; touch/motion do not wake this build.

## Optional Files composition only

If the build inventory includes `file_browser.elf`, select the green folder in
Springboard. Files should show only the admitted application/provider manifests
and executable inventory (including virtual folders), not arbitrary AppData or
SD roots. Open a manifest and inspect its text preview; local Back returns one
level. A brief PWRON/SYS_OUT press is local Back (preview to details, then
list/root); a long press is Home and should return directly to a fresh Home.
Open the filter editor and leave an unsaved draft, wait through returning Light,
then press PWRON/SYS_OUT to wake: the same editor and draft should remain, not
be saved, canceled or sent Home. Repeat three cycles. Verify the header follows
the saved 12/24-hour format and minute changes. This is a manual check sequence,
not evidence these physical outcomes have been observed.

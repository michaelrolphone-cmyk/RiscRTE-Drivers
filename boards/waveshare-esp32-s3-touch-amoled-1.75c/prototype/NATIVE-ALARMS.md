# Explicit native visual schedule profile

Applies only when store-proof.json has native_alarms=true and
alarm_profile=native-utc-schedules-clock-edit. This supplement supersedes inherited
baseline descriptions that say Alarms/Countdown are absent or timer wake is not
selected. Exact package receipts determine software qualification. All physical
results remain pending; this is not a safety-critical alarm or wake guarantee.

## Composition and authority

Twelve app ELFs and nine launcher entries are packaged. Alarms and Countdown
join Home and nine returning-Light foreground clients through mandatory
alarm.service API2. No API1 cast/fallback or implicit service selection occurs.
Points, factory recurring schedules, audio and haptic output are not selected.
Alerts are visual. Alarms/Countdown use the generic safe-content renderer, not
new NOVA/native-resolution app designs. Hardware Test remains awake.

The software service explicitly selects bound-KV API2 (2048-byte limit) for its
316-byte editor ledger; old API1 profiles remain unchanged. It receives exactly
seven bound keys: schedule configuration
alarm_utc_cfg/timer_utc_cfg in namespace3 read-only; occurrence records
alarm_utc_occ/timer_utc_occ in namespace4 read/write; alert_mode/alert_dnd in
namespace1 read-only; clock_edit_auth in namespace4 for explicit editor intent/authorization. Alarms/Countdown receive app KV namespaces3 and1, never4.
Application KV grants are namespace-wide, not per-key sandboxing. No SD,
updater, arbitrary file or direct hardware RTC grant is added.

Runtime provides Unix UTC; the service explicitly converts to UTC seconds
since2000. The supported schedule range is bounded by the service's2038 limit.
An unset ordinary/cold reset still initializes Home to January1 2026 midnight
in the saved zone. This is a placeholder, not synchronization. Set accurate time
in Settings before relying on schedules. Valid deep retention is different
from an ordinary reboot; old civil deadlines are not elapsed-time guarantees.

## Explicit manual clock edits

Selected Settings alone dispatches the opted-in tagged editor protocol. It
prepares an exact copied permit before attempting a time write, verifies actual
native time, then completes checked durable authorization. Ordinary authorized
backward edits do not silently erase or rewrite schedules/occurrence history.
Unexpected clock jumps remain guarded. All selected clients recognize the
extension, but Home and other foreground apps do not invoke editor callbacks.
This is selected-code behavior, not method-level authority isolation: every
holder of the API2 service grant can see the tagged extension. The service-only
ledger key is not directly granted to applications.
Clock and KV updates are not one atomic storage transaction: interrupted intent,
unknown results or failed readback must remain visible uncertainty/recovery,
not a fabricated successful save. A verified authorization ledger is not RTC
provenance: after an ordinary reset, the requested January 2026 placeholder can
coincide with an authorized UTC interval. The service cannot infer synchronized
time from that equality. Re-enter accurate time after such a reset; only admitted
Deep retention is tested as preserving advancing UTC. Exact owning and integration receipts govern
which fault paths are qualified.

## Sleep behavior

Home supports timer plus owned PWR/GPIO3 Light/Deep wake for a saved deadline.
With no schedule, sleep remains untimed PWR-only. The shared foreground helper
uses returning Light, retaining the current invocation and draft. It prepares a
fresh tagged ticket after ordinary sleep preparation, validates bounded timing,
and consumes it exactly once after successful Light and safe restoration.
Refusal defers entry; unknown/retained custody fences further callbacks. No
app is forced through synthetic Save/Cancel/Home to sleep.

Integer-second deadlines require conservative early wake; reconciliation may
re-arm before delivery. Exact physical timing, oscillator drift, sleep current,
and timer/RTC retention have not been measured. Deep modeling starts an entirely
new host process/Runtime/service, retaining only modeled wall time/eligibility
stamp and durable KV bytes. It is not a physical deep-reset test or target
instruction execution. No durable foreground draft checkpoint is added.

## Owner device check sequence — all pending

Use only the owner's authorized installation after checking INSTALL.md and
exact package hash. Preserve the delivered morning image as the rollback copy.
Do not use these checks for a time-critical obligation.

1. Set accurate local time and timezone in Settings. Return to Home, then swipe
   left to Apps. On the nine-icon Springboard, an outer-ring icon needs one tap
   to center it and a second centered tap to launch. Alarms and Countdown are
   outer-ring entries. Confirm all controls fit the circular aperture.
2. In Countdown save a short duration; use the app's Back control to return,
   then brief PWR on Springboard to return Home. Select Light in Settings and
   return Home. Brief PWR requests sleep; release it. Wait without touching
   the device. Expect timer wake to a visible alert; dismiss it and confirm it
   does not immediately recur. Record observed times and exact image identity.
3. Repeat with an Alarms time in the near future. Verify the chosen timezone,
   displayed saved time and visible alert/dismissal, not merely an icon.
4. Repeat a saved Countdown while Calculator has a pending expression and
   goes idle. Expect returning Light, the alert, then the unchanged draft.
   Separately edit Alarms/Countdown without saving, sleep/wake repeatedly,
   and confirm the draft survives without silently scheduling it.
5. Wake early with a brief deliberate PWR press, release it, and let Home
   sleep again. Expect the original saved deadline still to deliver once.
6. Select Deep and repeat Countdown from Home. Expect a real deep-reset cause,
   fresh Home, retained advancing UTC, visible due alert and durable dismissal.
   Capture serial/reset evidence and compare with an independent clock.
7. Cancel schedules and confirm ordinary untimed PWR-only sleep. Display
   rubbing/motion should not wake it. PWR is the PMIC PWRON/SYS_OUT control,
   not BOOT/GPIO0. App short PWR is local Back; long event is Home where
   supported. Do not hold through the unchanged factory hard-off interval.

Stop at the first wrong transition, retained/error message, crash or reset loop.
Record button action, screen, timestamps, reset reason and candidate SHA256.
Do not mark hardware accepted from the supplied modeled raster or host tests.

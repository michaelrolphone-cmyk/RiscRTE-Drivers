# Conditional alarm product execution cases

The target's `native_alarms` flag selects 34 cases; non-alarm profiles retain
exactly the earlier 23/21/11 cases. Cases 23–33 use the production service and
app compilation recipes, actual Runtime/provider graph and actual Home/app
controllers. No happy-path schedule record is injected. Input is modeled at
the actual touch/power-key transport; Save must create a decodable durable
record, and dismissal must create a matching acknowledged occurrence record.

| Case | Scenario |
| --- | --- |
| 23 | Countdown default five-minute Save, Home Light timer wake, visible dismissal |
| 24 | Alarm next-local-minute Arm, Home Light timer wake, visible dismissal |
| 25 | Countdown Save, Calculator pending expression, returning timer Light, preserved draft and dismissal |
| 26 | GPIO wake before saved countdown, later TIMER wake and dismissal |
| 27 | Countdown Save, Home Deep, fresh host process/Runtime/service, visible recovered occurrence and dismissal |
| 28 | Actual Settings Year 2026→2027 explicit Save, countdown UTC deadline and wake |
| 29 | Saved countdown with one explicitly injected stale callback ticket, no native sleep until transaction restoration/fresh retry, then timer delivery |
| 30 | Saved countdown with unknown realtime result during sleep preparation, retained invocation and no later callbacks |
| 31 | Alarms unsaved edited draft, three returning untimed/GPIO Light cycles |
| 32 | Countdown unsaved edited draft, three returning untimed/GPIO Light cycles |
| 33 | Real editor durable INTENT, software crash cut before native seed, fresh-process pending error, safe Back and fresh explicit Save |

The modeled persistent medium may contain an ordinary user sleep preference
(Light or Deep); it never begins with an alarm/countdown configuration. Deep
reset retains only serialized KV bytes, SDK wall time and NativeRealtime's
actual retained eligibility stamp; `exec` creates entirely new Runtime, port,
service and app instances, discarding the preceding process and invocation.

The negative stale callback-output and realtime-result injections are explicitly
reported and are not substitutes for the happy-path controller saves. UI probes
only read actual app/modal state. Visibility additionally requires a completed
nonzero raster at nonzero modeled brightness; after dismissal a subsequent
frame is required. No hardware, Xtensa instruction execution, physical timing,
power consumption or exact physical deadline claim follows from these tests.

Visibility uses an actual completed modal frame serial and exact pixel snapshot;
a later backlight unblank counts only if that submitted frame remains current.
Dismissal requires a later distinct frame and matching durable ACK. Timer arming
must equal the actual returned ticket's conservative formula and the saved
configuration's deadline, with a 250 ms modeled post-ticket preparation bound;
visible due delivery is bounded to five modeled seconds after the deadline.
The fresh Deep case deliberately has no optional retained-wake presentation
Store: cold fallback is exercised with valid SDK-retained UTC and zero reseeds.

The modeled dismissal is a fresh GPIO3 press followed by a short-release PMIC
event, not merely a late latch: the production wake-key neutralizer must see
WAIT_RELEASE → WAIT_FRESH_PRESS → READY before the new release is accepted.

Each actual successful Light entry copies the exact returned service ticket.
The real `resume_sleep` must receive that unchanged ticket once, return OK,
and have exactly one successful consume per Light entry; refusals and Deep
reentry consume none. Observation runs before dispatch and after OK only, never
after a retained or failed result. Default/manual domains also assert exactly
one initial seed or two seeds including the one explicit Settings Save.

Case33 cuts execution only after actual protocol begin returns OK following
durable INTENT write/readback. It retains actual KV bytes and SDK wall time,
not the live permit or any Runtime/service state. A modeled software reset
invalidates native time eligibility; Home explicitly seeds its authorized 2026
fallback, never the unconfirmed requested 2027 time. Pending error is displayed
and dismissed through real input, then actual launcher/Settings Save creates a
newer VERIFIED generation and final Home reads 2027. This is an explicit crash
cut model, not a claim about physical power-loss atomicity.

The native clock-edit execution backend advertises the actual native 2048-byte
maximum using `RISC_KEY_VALUE_V2_BLOB_MAX`; Runtime still enforces API1's 64-byte
limit. The service must explicitly select bound KV API2 for its 316-byte ACE2
record. The record is never constructed by the fixture.

## Opt-in native Alarm/Countdown presentation successor

`native_alarm_geometry=true` is accepted only with native alarms and matching
aggregate/per-app receipt booleans, exact `PORTABLE_ALARM_NATIVE_PRESENTATION`
recipe flags and native safe-content adapter flags. Both exact per-app dependency
maps must include `alarm_native_layout.h`; every dependency digest is verified.
The execution receipt records the same selection; absent/false preserves all 34
generic paths and no native geometry assertion is inferred from native UTC.

A read-only bridge in each actual application translation unit calls the selected
production layout helper. It reports rectangles and page/field; it never invokes
a controller action or changes model state. Native touch centers come from those
rectangles plus the modeled capability's single 69-pixel safe origin, with no
240-to-native bitmap/touch scaling. The 328-square content is this product fixture's
capability, not a universal application layout assumption; owning renderer tests
qualify generalized geometry independently.

Cases 31/32 additionally execute real nested Edit → field → increment → Back →
field → Done → Back to root → Edit → different field. They then retain the exact
edited draft, nested page and field through three returning-Light cycles, with
no configuration Save. Other happy paths use the production root Arm/Start
rectangle and retain actual durable schedule/timer/visible-dismissal assertions.
Case count remains 34. The native profile is not qualified until both full lanes
run against its new exact target; generic receipts do not substitute for them.

The explicit Quick Actions successor adds eight separately documented cases
(see `BADGE_QUICK_EXECUTION.md`) without replacing the original34. Its broker
selection is opt-in; all original schedules, edited-time recovery and native
Alarm/Countdown geometry cases remain required on the same exact product graph.

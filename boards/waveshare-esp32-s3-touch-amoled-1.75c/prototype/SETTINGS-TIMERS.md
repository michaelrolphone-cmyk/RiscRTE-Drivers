# Settings: independent idle and subsequent Deep timers

This selected Badge profile exposes the existing `sleep_idle` and `sleep_deep`
preferences through the shared Settings controller. It adds no sleep provider,
namespace or storage authority. Other historical Settings profiles remain
available; this product selects `--sleep-timer-settings` instead of idle-only.

In Settings, open the sleep timer or Deep sleep timer, use −/+ to change the
draft, and choose Save. Cancel or Back before the write preserves saved values.
The idle timer changes in five-second steps; the subsequent Deep delay changes
in one-minute steps. Both cap at3600seconds. Existing saved values take priority;
absent idle defaults to15seconds and absent Deep delay to300seconds.

The existing default Hybrid workflow is preserved: Home's brief PWR or automatic
idle enters Light first. Without input, the configured subsequent delay can
advance to Deep; an earlier/equal alarm wakes its alert instead. A brief PWR
wakes Light back to Home. Existing explicitly saved Light/Deep/Hybrid choices
remain selectable; no mode is silently substituted for the user's saved choice.

Before a mode/timer write, the completed frame explicitly says "Saving - not
confirmed". An asynchronous display keeps the draft frozen until that frame
completes; repeated Save does not duplicate the write, and Back cancels before
write. A recoverable failed write shows "Save unconfirmed - retry"; review the
reloaded/current value before explicitly retrying. Retained/unknown custody
allows no later provider or display callback, so that pending frame remains;
it is never presented as saved success.

Selected time/date, timezone,12/24-hour format and existing navigation workflows
are preserved. This is the existing compact safe-content Settings presentation,
not a claim that every Settings screen has a new full-native layout.

Qualification includes actual installed UI saves creating both preference keys,
Home rereading them and using each interval, Cancel/Back/reopen, existing time
Save, and actual storage IO refusal/unconfirmed frame/explicit retry. Owning
async/retained tests are separately identified; host models are not physical
button, display, power/current or retained-wake acceptance.

Full32MiB installation resets NVS/preferences and application data. No device
operation was performed for this package. See INSTALL.md and the exact package
hash; do not infer hardware success from software qualification.

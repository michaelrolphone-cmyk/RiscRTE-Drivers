# Returning foreground idle qualification

The explicit `--foreground-idle` profile composes the maintained shared adapter
for Settings, Springboard, Wi-Fi Settings, Calculator, Stopwatch and Battery.
It uses returning, untimed Light sleep while retaining the same app invocation,
RAM model, unsaved editor draft and private grants. It never sends synthetic
Home, Save or Cancel, unloads the app, or chooses Deep/Hybrid from Home's mode.
Home defaults to staged Hybrid: Light first, then Deep after the configured
subsequent timer. Explicit saved sleep modes still win.

The shared `sleep_idle` preference controls inactivity. Valid saved seconds win;
the build's fallback applies only to a confirmed missing record. Malformed or
cleanly unavailable preferences disable automatic idle. Transient BUSY/IO uses
settled, bounded-backoff retry without resetting input activity or changing the
user's timer. Confirmed Settings idle-timer Save updates the active cache.
The read helper never writes preferences. Namespace1 grants expose the ordinary
KV API, not a read-only authority; the helper releases its temporary grant before
normal app work. Stopwatch2 and Wi-Fi6 remain separate private namespaces.

## Safety boundary

The common settled adapter/editor boundary defers for pending frames, held or
queued input, legitimate radio/audio/update/capture work and unsafe storage.
It closes touch, detaches input claims without releasing the navigation owner,
uses Runtime's native-file sleep transaction, and calls typed power services.
The selected CO5300 service retains the completed framebuffer and restores all
native rows while dark before restoring brightness. No fixed240px copy exists.

The key filter's prefix-compatible neutralize suffix survives through the
navigation dependency. It suppresses held wake and delayed PMIC latches until
a new physical press is observed, on every Light cycle. Touch and motion are
not wake sources. A held physical wake key refuses entry.

Clean refusals restore the same session. CONTEXT, malformed status, retained
custody and unconfirmed cleanup fence the invocation without later provider
calls, freeing or implicit exit. Recovery then requires an owner-controlled
restart; no battery-saving claim is made for an uncertain refusal state.

## Remaining boundaries

- Hardware Test/bringup is a custom diagnostic app not yet opted into this
  shared boundary; it may remain awake. Its idle path is the next integration
  item, not a permanent product constraint.
- Active Wi-Fi/transfer/capture work can intentionally defer sleep. This unit
  does not cancel work to manufacture an idle result.
- Light sleep preserves RAM only. Power loss, brownout, a hard PMIC cutoff or
  ordinary reset can still lose an unsaved draft. Durable Deep recovery is a
  separate app-owned checkpoint unit, described in FOREGROUND-DEEP-FOLLOWUP.md.
- Physical sleep current, time drift, wake latency and repeated on-device
  display/touch recovery remain unmeasured. Light is not claimed to consume
  the same current as Deep.

Use exact package/store/execution receipts for actual software results. Selected
foreground packages require both normal and sanitized full 21-case execution
receipts in addition to target admission and image/filesystem verification.
Two existing text-host queue/overflow fixture failures were reproduced on the
unchanged System baseline; they are not represented as passing. The actual
adapter/modal bridge and the independently passing host/client cases are scoped
separately from a complete physical keyboard-provider round trip.

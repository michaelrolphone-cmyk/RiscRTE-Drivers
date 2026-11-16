# Foreground Light suspension

`foreground_light_sleep.c` is linked into an explicitly selected PortableApps
application. It implements `portable_app_foreground_idle_sleep` from the shared
`PortableForegroundIdle.h`. It is not a provider, scheduler, storage client or board
policy. No board identity, pin, timer, Deep entry, launch or persistent write is
present in the helper.

## Required composition

- Canonical Runtime ABI with acquire/release, retain-invocation and the native
  sleep prepare/resume transaction. A truncated Runtime refuses before I/O.
- Unique authorized `display.power@1` with checked `resume_status`.
- Unique authorized `power.sleep@1` with ordinary untimed `light_sleep`.
- Unique authorized `input.power-key@1` with the negotiated, prefix-compatible
  `RiscPowerKeyNeutralizeV1.h` suffix.
- The caller keeps its own key/navigation dependency grant alive across the
  helper call and subsequent input reopening. This preserves the provider's
  gate after the helper releases its temporary key grant.
- The selected display-power provider must retain the completed image through
  preparation and restore it before the old brightness. The CO5300 facade meets
  this contract by retaining its native framebuffer, reopening dark and replaying
  every native row before brightness restoration. The helper neither borrows nor
  overwrites a framebuffer: `display.output.acquire` is not a snapshot contract.
  Geometry and stride remain entirely provider-owned; no fixed-size image or
  extra full-frame allocation is introduced.

The caller first settles presentation, closes touch/subscriptions, relinquishes
active navigation claims, and defers unsafe work. It keeps the original app
stack, model, drafts and grants alive. It reopens and neutralizes touch/navigation
only on the helper's 0 or 1 result. A cached idle preference controls when the
caller attempts sleep; the helper never opens Settings or changes that value.

## Results and custody

- `1`: ordinary Light wake, display and native-file transaction restored.
- `0`: clean pre-entry/refusal result; every attempted prepare is paired and the
  display remains/restores its original frame. No fallback mode is attempted.
- `-2`: retained/unknown custody. The Runtime fence retains the invocation. No
  subsequent provider operation, diagnostic, scheduler poll, release or free is
  allowed, including on a later accidental helper call. If the terminal fence
  itself cannot be installed, the helper does not return into ordinary cleanup.

Unknown Light statuses, malformed wake results, boolean external acquisition
failure, failed/uncleared grant release, failed neutralization, or any uncertain
restore are terminal. Known Light refusal statuses restore in reverse order.
The Runtime transaction resumes only after sleep and display restoration;
MEDIA_UNAVAILABLE is clean native cleanup, not proof a removed medium is usable.
An absent key suffix safely refuses before preparation. The key gate is armed
before entry and after ordinary restore, suppressing the wake press, release and
late latch until a fresh physical press is observed. No event is converted into
Home, Save, Cancel or another application action.

## Focused qualification

From this Drivers tree:

```
python3 scripts/test_portable_light_sleep.py --runtime PATH_TO_RUNTIME --system PATH_TO_SYSTEM
python3 scripts/test_portable_light_sleep.py --runtime PATH_TO_RUNTIME --system PATH_TO_SYSTEM --sanitize address
python3 scripts/test_portable_light_sleep.py --runtime PATH_TO_RUNTIME --system PATH_TO_SYSTEM --sanitize undefined
```

The runner stages one coherent SDK from current System PortableApps headers,
canonical Runtime app/driver/hardware headers and Drivers capability headers.
It exercises the actual wake-key filter with the helper, including repeated
Light cycles, held/reasserted keys, late events, failed preparations, each
cleanup boundary and refusal paths. Host results do not qualify physical sleep
current, display electrical behavior or wake reliability on a device.

## Confirmed inactivity timer

`foreground_idle_preferences.c` reads only namespace 1's shared `sleep_idle`
record, then releases that temporary grant before ordinary app work. It reuses
`PortableSleepPolicy.h`'s codec without writes. The KV capability itself exposes
writes; only this helper's behavior is read-only. Existing app-private namespaces
must remain separate (for example Stopwatch 2 and Wi-Fi 6).

The deployment supplies `PORTABLE_FOREGROUND_IDLE_FALLBACK_SECONDS` (5..3600).
That fallback applies only to a confirmed missing record. Valid saved seconds
win; corrupt records or a clean unavailable capability disable automatic idle.
Clean transient KV BUSY/IO returns retryable -3 for a settled, bounded-backoff
retry. This backoff does not change user activity or the saved inactivity timer.
CONTEXT, unknown results, partial acquisition or failed/uncleared release retain
custody. A canonical-empty unavailable KV acquisition is clean only while the
canonical Runtime getter still proves an active owner.

`portable_foreground_idle_timeout_ms()` and
`portable_foreground_idle_timeout_changed(ms)` perform no I/O. Settings invokes
the latter only after a confirmed idle-timer Save; changing the separate Deep
or mode preference does not select terminal Deep for a live foreground stack.

# Measured released-key preparation and timed sleep forwarding

The selected `platform.clock@1` ABI promises monotonic duration and scheduler
**yielding**, not a minimum delay. Native `cooperativeDelayTicks` rounds the tick
count, not the task's current sub-tick phase. A requested 50 ms can therefore
return after 49 observed milliseconds. Changing global clock behavior would
alter an established contract; the provider must measure its release interval.

Preparation rejects a held key immediately. For a released key it requests the
remaining interval, then checks a non-sentinel, non-regressed clock, cached
sleep guard and released-key read before admitting preparation. All subtraction
is performed only after monotonic validation; there is no overflowing absolute
deadline. A legitimate sub-millisecond yield may make an integer-ms observation
unchanged. Retrying uses an aggregate requested-delay budget of twice the
configured interval plus two milliseconds. This permits tick quantization and
bounds total requests, including a stalled clock. Configuration validation
bounds the interval to 1..100 ms, so the maximum requested budget is 202 ms and
all iterations request at least one millisecond. This is a bounded conservative
refusal policy, not a promise of progress from a defective clock or scheduler.

Each yield is followed by guard/key rechecks. A failure or reasserted key never
becomes success. After retained custody, prepare/resume/entry calls perform no
provider or clock callback; cleanup keeps the owned token pinned. This patch
adds no diagnostic I/O, PMIC changes, wake-source policy or shared Runtime delay
change. HYBRID/Light/Deep selection remains owned by the shared app policy.

The existing provider exposes timed Light and timed Deep through the checked
GPIO ABI suffix. Native Runtime273 already binds these callbacks, timer arm /
clear and GPIO wake. Owning tests now exercise real suffix forwarding as well
as absent/truncated suffixes: both polarities, timer and GPIO wake causes,
configured duration bounds, repeated resume/prepare, returned failures and
terminal retention. Native adapter and CpuPort timer/fault tests remain distinct
from the provider's unit tests and the complete product's integration tests.

Validation includes 330,000 modeled tick-phase/polarity/duration combinations
for 1, 2, 10 and 20 ms scheduler ticks, quantized zero progress, shortened wait,
backward/sentinel/near-overflow observations, persistent stall, post-yield guard
and read failures, held/reasserted keys, old ABI and terminal no-callback fences.
Normal and ASan/UBSan lanes use the exact selected merged provider SDK; the old
b7 source fails the phase-regression assertion with the same test. Xtensa object
compilation introduces no external helper imports. These are software tests,
not proof of actual device sleep current, wake reliability or the sole cause
of the reported PMU preparation failures.

## Exact selected contract and adjacent callers

At Runtime `273bdbccf65647a803ab0586a418f8283732baec`:
`sdk/driver/RiscPlatformClockV1.h:2–4,14–15` defines monotonic duration and MUST
yield, without minimum-elapsed guarantee. `src/ports/esp32s3/CpuPort.cpp:235–236`
forwards the clock wait to `Hardware::sleep`, bounded to5000ms.
`NativeHardware.cpp:189–195` calls `vTaskDelay(cooperativeDelayTicks(...))`.
`CooperativeDelay.h:4–9` rounds the tick COUNT and explicitly does not add a full
tick for exact requests. Selected native SDK config is1000Hz. None of these
contracts authorizes assuming a50ms request necessarily observes50ms elapsed.

Read-only adjacent-caller audit (not blanket qualification):
- `cst9217_touch/driver.c:378–396` already measures/retries its remaining reset
  interval, bounds attempts and checks backward/sentinel observations.
- `qmi8658_idle/driver.c:39–44` still performs one sleep then requires elapsed
  >= requested. Its1/15ms oscillator and667ms sensor settling calls can suffer
  the same concrete short-wait false refusal. This is a separate identified
  repair candidate; the existing cache-ready sleep guard performs no wait/I/O.
- `qmi8658_accel/driver.c:47` has a measured31ms loop with32attempt bound; unsigned
  subtraction lacks explicit sentinel/regression checks and merits separate
  fault qualification. It is not selected as Badge's idle guard.
- `bq25896` uses sleep only between bounded deadline/state polling, rather than
  asserting one requested delay completed the deadline. It is not this board's
  selected PMIC. BLE scheduler delays and GPS polling likewise are not evidence
  of this exact minimum-interval false-refusal defect; no changes made there.

The native selected GPIO table (`CpuPort.cpp:223`) contains timed Light/Deep and
multi-wake suffixes. `NativeHardware.cpp:197–200` binds GPIO wake and timer arm /
clear; `NativeSleep.h:18–28` preserves64-bit microsecond conversion and checked
cleanup. Repeated full Xtensa provider links (not just object compilation) are
byte-identical and have no undefined imports; the complete product's normalized
ELF/graph admission remains its separate gate.

# Fixture observation dependency audit

Host fixture contract. These test-only observations do not change target source.

Run from the repository root:

```sh
python scripts/test_badge_fixture_observation.py --output fixture-observation-receipt.json
```

The script derives the owning repository from its location, honors CXX/TMPDIR, and removes temporary generated executables. --root supports an explicit alternate source tree. It runs normal and ASan/UBSan lanes and checks actual callback placement before compiling extracted bodies.

## Observation ownership

| Former health work | Correct source of observation | Preserved evidence |
|---|---|---|
| Alarm/Countdown current modal, saved state, visibility, dismissal and restored draft | Completed display frame (completed=true); successful brightness update; PMIC 0x49 input read; CST report sampling; completed modeled delay; explicit host Home observation loop | Actual frame number/pixels, deadline +5 bound, durable ACK occurrence and revision, modal-restoration state and draft checks unchanged |
| QuickActions current state, saved revision/brightness, restored Home, alarm delegation | Same boundaries via alarmObserve -> quickObserve; completed=false never captures a new frame | Existing inAlarm recursion guard, pending-frame identity, real save counters and retained-result tests unchanged |
| Wi-Fi scan/editor, retained draft and restored Home | Same boundaries via alarmObserve -> wifiObserve | Actual frame captures remain completed=true; provider results and masked-draft checks unchanged |
| Picker UI refresh | Same explicit boundaries via pickerObserve(false) | Picker frame counts and final image/controller assertions stay in completed=true observer |
| Timecard current editor/draft/Quick/shared Home completion | Same explicit boundaries via timecardObserve(false); alarmObserve(false) also retains newly established visibility checks | Native frame counters/captures still completed=true; no synthesized ACK or key injection |
| Foreground three-Light completion | Completed delay and explicit observation boundaries | Existing cycle/restore/transaction/draft/grant checks unchanged; no time advance in observer |
| Grant usage maxima | Explicit observation boundaries, not clock reads | These are **boundary-sampled maxima**, not exact allocation high-water marks; values cannot be compared as clock-independent exact maxima |
| Fixture loop/time limits | Explicit observation boundaries and existing delay bounds | Existing numeric limits and stop predicate preserved; m.health is historical field name now counting boundary observations, not clock queries |
| Lifecycle/reset/readiness flags | Existing app enter/exit, diagnostic log, service and reset callbacks | No changes; next explicit boundary sees their new state |
| Legacy runtime fixture stop | Existing host-only Home loop gate | No scenario observers exist; health becomes pure uptime and lifetime bound remains in the host loop |

## Safety and scope

All seven modern boundary helpers assert nonterminal before observation. Input entry guards and PMIC write-return ordering remain intact. The clock callbacks only check terminal state and return m.ms; health likewise returns modeled uptime without observations. No controlExitRequested binding is added. Normal observation stop remains the explicitly documented host-only Home loop hook, not a target user action.

Observers read actual compiled-app probe state, fixture maps/pixels and modeled wall time. `running->grantUsage()` is local Runtime accounting inspection, not a provider operation. Existing observer bookkeeping can set fixture completion flags or terminate a passed test; it does not issue hardware/provider actions. Quick-to-alarm delegation retains its existing inAlarm guard. Completed=false cannot manufacture an independent completed-frame receipt.

## Focused proof

Seven extracted actual boundary/continue/health/clock helpers pass normal and ASan/UBSan with modeled observer/accounting dependencies. Each lane executes100,000 paired health/clock calls with zero observer effects, then validates explicit boundary observation and unchanged stop behavior. Source generation checks exact preservation of the former observer body and stop predicate. Every existing completed=true observer stays untouched. No full Runtime rerun by this worker; integrator owns that acceptance. Leak detection disabled because execution environment uses ptrace. The owning proof additionally checks PMIC write exclusion, terminal guards, delayed brightness, completed-row ordering, CST sampling, absence of Runtime navigation-stop binding, and legacy clock purity.

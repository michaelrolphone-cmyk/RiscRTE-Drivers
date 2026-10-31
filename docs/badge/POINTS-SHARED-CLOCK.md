# Explicit current Points product increment

`build_badge_prototype.py --include-points` adds the maintained Points in Time
application to the existing full Timecard cohort. It does not replace the frozen
prototype selection or switch to the separate catalog-style Points application.
The option requires Timecard, native alarms, QuickActions, foreground idle and
explicit Productivity and preserved Home Utilities source roles.

## Source roles and ownership

The default `prototype/sources.lock.json` is unchanged. Points explicitly selects
`prototype/points-sources.lock.json`; every selected role still requires a clean
checkout at its complete commit identity. There is no latest-ref fallback.

* Application System and Utilities are explicit successors containing the shared
  scene facade and mixed service respectively.
* Home continues to use its independently pinned `home_system` and
  `home_utilities` roles, not substituted application producer sources.
* Productivity owns the application, copied service-context client, current
  builder, native UI and frozen-profile preservation checks.
* Utilities owns participant persistence, history reconciliation, authorization
  and the mixed alarm service. The Runtime source is unchanged by this increment.

The Points lock names published draft successors: System #163, Utilities #103
and Productivity #29. Their source trees exactly match the locally qualified
commits; complete remote commit identities are retained in the lock.

## Selected authority

Points receives KV namespaces 1 and 5 and the existing copied-preference broker.
It receives neither occurrence namespace 4 nor `runtime.realtime-control`.
The product consumes exact owning grant receipts and rejects broadened policy.

The mixed service has exactly ten bound keys: the original alarm/countdown
configuration and history, alert preferences and clock-edit authorization, plus
read-only Points configuration, read-write Points occurrence history and
read-only timezone preference. The app reads a tagged, copied, reconciled
context. It cannot write service history or call clock-edit callbacks.

Clock editing uses a bounded participant record with exact configuration and
saved/virtual provenance. UTC creation floors authorize live projection without
rewriting saved creation time. History remains monotonic across edits, restarts
and timezone changes. Reconstruction of an already recorded occurrence requires
an exact authenticated event and original deadline; it cannot authorize a new
live event. Historical ACE2 records are imported read-only and never manufacture
Points authorization. Known failed editor reads invalidate the cached settled
view; BUSY remains a clean refusal, and retained custody forbids later callbacks.

## Qualification boundaries

The selected product includes Points in the real launcher catalog and preserves
Timecard and the other existing applications. Owning tests cover current
round/square controls, QuickActions, alarm interruption, Light return, retained
custody and stale-context refusal. Current and frozen native presentations keep
separate source/SDK/selector guards. Current scroll controls use supported UP/DN
labels; frozen controls retain their original labels.

`test_badge_timecard_execution.py --app points` uses the shared actual Runtime,
provider, launcher and input infrastructure with host instruction mapping. Its
focused cases exercise save/reopen, an unsaved draft through returning Light,
and real service occurrence delivery/ACK while a draft remains open. The default
Timecard selection stays separate. Provider doubles used by owning UI tests are
not a substitute for these composed execution cases.

`test_points_graph_reuse.py` can re-execute a previously qualified, hash-preserved
real Runtime graph validator only after checking its exact source, compiler
selection and image-count assumptions. Its output explicitly excludes native
ELF-link and physical-device proof; it does not upgrade old evidence.

A successful store build, host execution, target app/service loader check or
Runtime graph admission does **not** establish selected native-kernel image
qualification, device sleep/wake behavior, flashing, a release or task completion.
The selected native image must still be linked and checked with the same kernel
features. Frozen application GCC8 target-byte matrices remain a distinct check
from token parity and host tests. Draft publication does not establish any of these remaining acceptance gates.

# Badge time-server synchronization

This successor builds on the immutable 941d82e cohort. It adds a shared,
opt-in Wi-Fi time synchronization path; it does not replace manual time,
timezone selection, alarm semantics, sleep policy, or existing applications.

## Ownership

* Runtime `platform.time-sample@1` supplies a read-only SNTP sample. Its native
  worker owns DNS and UDP resources. Owner/UI calls post or poll copied state;
  they do not wait on DNS. Request cancellation can remain pending until the
  worker returns and checked cleanup completes.
* The `time-sample` ABI-v2 provider exposes `net.time-sample@1` to authorized
  clients. It never writes UTC. Failed/uncertain cleanup retains the provider
  and native dependency; successful close precedes release.
* The shared Wi-Fi client starts one attempt after an owned station connection
  has a nonzero IP address, and offers an explicit retry/cancel row. It holds
  its existing network-service lease until sampling and cleanup finish.
* UTC commit uses the shared alarm clock-edit transaction, native clock seed
  and readback. A sample is not a clock-write authorization. Timezone and
  manual preferences are not rewritten. Errors preserve prior time unless a
  seed was attempted, in which case the existing uncertain-write/recovery
  rules apply instead of falsely claiming that nothing changed.

SNTP is **unauthenticated**. Matching peer/nonce, protocol, era, range and age
checks reject unrelated or malformed responses; they do not establish server
authenticity. The configured default server is `pool.ntp.org`. No credentials
are sent to the time server or included in diagnostic evidence.

## Selection and qualification

`build_badge_prototype.py --time-server-sync [--time-server NAME]` selects the
shared app path and facade. `build_badge_native.py --time-server-sync` selects
`RISC_ENABLE_TIME_SAMPLE=1`. Both selection receipts and staged canonical SDK
bytes must agree. Existing canonical SDK files remain byte/mode-identical;
additive headers are reported separately, never called whole-tree equality.

Owning tests cover packet validation, asynchronous cancellation, custody,
clock/alarm transaction failures and existing manual Settings behavior.
Product qualification additionally uses the real Runtime/provider graph,
target ELF admission and the selected UI/controller. Simulated network samples
and host tests are not physical network or clock acceptance.

## Delivery and lifecycle

Michael requested time-server sync, then a return from operationalization to
incubation **after he confirms it**, with all remaining applications migrated
in that incubation phase. Confirmation has not been received. Neither a host
PASS nor publication changes that lifecycle condition. Existing app-migration
work remains preserved; no application is removed by this increment.

The full combined-image installation resets settings, saved credentials and
application data. This must remain explicit in the test-build handoff.

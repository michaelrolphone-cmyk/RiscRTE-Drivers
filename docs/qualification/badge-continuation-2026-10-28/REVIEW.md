# Badge continuation: reviewable recovery and CI changes

The full Badge app migration remains incomplete. This increment restores exact
source identities locally, prepares a repeatable consumer, and opens the six
existing published checkpoint branches as linked draft PRs. It does not select
Points, change public firmware, merge anything, or establish hardware acceptance.

## Concrete publication blocked by automatic review

Runtime commit `e125ae9ee7d3b8bc9aa2f1709cb1f34b5562de16` is ready on local
branch `badge/source-recovery-2026-10-28`, based on published snapshot
`07e982c27b9e584fcba6b49d1c84cacaa558d24f`. Its only changes are
`docs/badge-source-recovery/README.md`, `manifest.json`, and 20 packparts:
10,120,250 bytes containing 4,200 original Git objects. Combined pack SHA256:
`fc7d8da7e7c7df2f3750b602012c477a623183b1d350ca6a4d9191e220312ad7`.
It restores original Runtime `046f4c22b263739f3bf3f96f888617d72eb3d498`,
historical baseline and canonical SDK/clock helper anchors, without replacing
their identities with the publication commit. Source tree is
`876d77de61b53c05343a1de754c32eb476c3ed3b`; four shallow boundaries remain explicit.

The stopped action is publication of that exact commit to
`michaelrolphone-cmyk/RiscRTE`, branch `badge/source-recovery-2026-10-28`.
Automatic approval review rejected the push because it considered publication
of possibly private original source objects/SDK history insufficiently explicit
in the continuation instruction. No retry or alternate upload occurred. A fresh
read-only branches query found no such branch. Approval must concern this exact
payload and GitHub destination; it is not merge, release or device authorization.

## Local Drivers changes

Local commits `3475f1e3a4ab0b94584affc0465d3e1b4d3d1af1`,
`2006b135a297b60b63baf04e2afcd3488693d501`, and
`e16d2e58e9dfc6ce0e205021210ec1f44e25f2d6` prepare the consumer and CI dispatch.
Their base is the existing published recovery commit
`09b2fb6807827b8e9e53f8a265c082e98c013ab7`; these new changes are not pushed.
The following documentation commit retains their compact evidence.

`scripts/badge_source_pack.py` checks repository identity, part paths/lengths/
SHA256, combined SHA256, object count, source tree, boundary objects and Git
connectivity. `scripts/recover_badge_ci_sources.py` consumes immutable owning
publication IDs in `docs/badge-source-recovery/sources.json`, imports the packs,
then checks out original pins. It refuses existing role directories, retains
distinct Watch reference/bringup/alpha roles, verifies the shared clock helper
hash, and restores canonical Timecard anchors. It never falls back to main.

The SPIFFS override is recovered from the preserved standalone source bundle,
with original pin `525335a05af89522a6f16b57d21682bcaeb518cc`, tree
`2fc48a3d774b7106c8b736cc9a500d596eb8472c`, and explicit base boundary
`ad902cadceb39d0825a97e25ecb5867641f606ba`. It is not claimed upstream-addressable.

The selected CI lane now forwards all accepted app/rendering/time-sync flags,
Timecard, native time sampler/performance, 16-row queued pixels, column and
unchanged-stripe reuse, and the explicit SPIFFS override. It runs both normal
and ASan+UBSan lanes, including local Quick receipts and owning time-sync tests.
This orchestration has not completed a target/hosted run. The historical minimal
lane remains separate; its previously documented nonforeground recipe gap is
not claimed fixed by these changes.

## Checks completed

- Empty Git consumer import restores the original Runtime/tree and SDK anchors;
  source pack validation covers all six owners and the SPIFFS override.
- Final fresh recovery recipe passes for 15 clean exact roles, 15 pack imports,
  and eight additional owning SDK references. Runtime publication was supplied
  from the local reviewed commit using one explicit Git URL rewrite; other
  owners' packs came from GitHub. Therefore this is not clean GitHub-only proof.
- Five real-Git recovery test methods pass, with nine negative inputs. One
  mocked-dispatch test verifies actual workflow selections reach native and
  app builders; it does not execute those builders. Sixteen maintained CI-delivery
  tests and four time-sync-profile tests also pass. The artifact-dependent
  time-sync mutation gate now runs after actual native/target/execution outputs
  with explicit inputs in each lane; an initial no-input invocation was rejected
  by its parser and is retained, not reported as a product test pass.
- Timecard: 168 native UI cases plus 20 controller/storage cases in each normal
  and ASan+UBSan lane. Time sync: 24 host cases per lane. Exact receipts, logs,
  commands, source hashes and distinctions are in `validation.json`.
- Fresh saved Timecard week/keyboard pixels were inspected at 410x502, 502x410
  and 160x160. This is host rendering, not physical panel qualification.

Target build, native admission, full composed Runtime, package/image replay,
fresh deterministic output comparison, final-head hosted CI and hardware tests
have not been rerun. Historical passes do not qualify these new commits.

## Draft delivery and remaining work

Linked drafts: Runtime [102](https://github.com/michaelrolphone-cmyk/RiscRTE/pull/102),
Productivity [28](https://github.com/michaelrolphone-cmyk/RiscRTE-Productivity/pull/28),
System [162](https://github.com/michaelrolphone-cmyk/RiscRTE-System-Apps/pull/162),
Watch [16](https://github.com/michaelrolphone-cmyk/RiscRTE-Watch/pull/16),
Utilities [102](https://github.com/michaelrolphone-cmyk/RiscRTE-Utilities/pull/102),
Drivers [45](https://github.com/michaelrolphone-cmyk/RiscRTE-Drivers/pull/45).
They use already-public snapshot heads. Their base/head/diff identities and
observed CI runs are in `github-delivery.json`. Primary attempts are registered
in the existing Rhizome execution; queued/running is not terminal validation.

System and Utilities snapshots remove substantial content relative to current
main. Each deletion/overlap must be reconciled; these are review drafts and must
not be applied as replacements for main. No existing source or PR was overwritten.

Fresh inventory has 46 app/profile rows, 23 intended Watch roles, and the 13
historical installed apps. The 11 intended pending roles remain open. Other
discovered profiles remain unresolved, not excluded; no scope change was made.
The complete per-profile capabilities, source IDs and status are in
`app-inventory.json`. This is a discovery matrix, not completed workflow qualification.

Points still needs coherent tagged occurrences, durable clock-change records,
namespace/storage compatibility and actual composed scheduling/UI workflows.
The existing ACE2 clock-edit record is 316 bytes with four 68-byte rows for
two alarm/countdown identities and two history groups; its protocol loops over
two records. Existing Points state is separate. Removing the compile-time guard
would not cover it, and no such change was made. BLE, audio, updates, Contexts,
IQ and LoRa target/provider work remains as described by the continuation.

Xylem immutable source set `352b9e08-34f4-4646-97fc-077390f2c430` records
the published snapshot identities separately from original consuming commits;
indexing is queued, not a search-coverage success. Existing baseline source set
and evidence remain unchanged. Mycelium retains the bounded source-readiness
lesson, preserving uncertainty and the distinction between published inventory,
restored identities, executable composition and delivered product behavior.

## Updated CI observation at 02:36 UTC

The existing snapshot workflows now show nine failed runs alongside passing and
pending runs. Exact job/step identities and bounded failure excerpts are in
`ci-observation-0236.json`. This supersedes the earlier queued/running-only
observation, without rewriting it. Three registered primary checks passed
(Watch native height, Utilities native UTC, System native SDK); the remaining
primary attempts were queued. No aggregate success is established.

Observed causes include Runtime's extracted deepReady fixture lacking a
RiscAppData declaration; Watch unittest discovery importing the argument-driven
sleep-default script; System's stale Watch-color source-string assertion,
initial-brightness fixture calling alarm_output_modes instead of the selected
helper, typography regeneration drift, and USB-queue checkpoint abort; Drivers'
shared touch-composition SIGABRT affects both scoped Home and display workflows.
These require source/fixture reconciliation and real reruns; they are not
waived, relabeled, or fixed by a Runtime pack publication. No unchanged run was
retriggered. The full migration and these independent CI repairs remain open.


## Publication approval resolved in the Badge conversation

Michael replied Approved to the exact Runtime recovery-payload request. The one
approved CLI push attempt failed for absent Git credentials before publication.
The connected GitHub app then published all22 exact blobs and tree
`f9bac1666228b9375057885e5a550814f2a86c49` on the approved branch. Publication
`318d77283ba707b6f2820d2833e5d7ea3ff3f7e8` has the same tree as approved
local `e125ae9ee7d3b8bc9aa2f1709cb1f34b5562de16`, with distinct commit metadata.
Fresh GitHub fetch verifies the publication identity/tree. The earlier denial
and local-only checks remain historical evidence, not current approval blockers.
The consumer sources.json now pins the actual publication ID.

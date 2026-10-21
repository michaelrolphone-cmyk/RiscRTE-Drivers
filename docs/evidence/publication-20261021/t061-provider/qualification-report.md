# T061 local implementation and qualification

## Result

Implemented an opt-in shared resident Quick Actions Watch-battery page with
explicit per-session device selection and manual bounded refresh. Last acquired
value and current/not-current are distinct. No background scanning, automatic
peer choice, persistent identity claim, pairing change or RF-policy override.
The requested feature is **not deployed or hardware accepted**.

System Apps: base 7270a10ed9cefba6ec1368060196a13bb3370985,
branch tasker/t061-session-battery-local,
commit 2fb0af77dec58eca931099a29c81468529745b96.

Required paired Drivers: base a7ae39994603c4545a3c014f09051ecfc12084d2,
branch tasker/t061-battery-provider-local,
commit ba28f9bdfbe84d23de36350189cc2ec89fea1677.
The separate test-only checkpoint fe310f01a90a616ce05be02b677debdf3cfcd607
remains preserved. Both local worktrees are clean. No remote publication occurred.

## Verified scope

- Six Drivers suites pass normal and ASan/UBSan.
- Consumer component passes normal and ASan/UBSan.
- 48 controller executions pass, including exhaustive populated/empty hit maps.
- Real adapter passes four configurations: ordinary/actual atomic queue, each
  normal/sanitized. Cleanup custody, post-close restoration, stale epochs,
  terminal Home/crown, peer cache isolation and queued dismissal are covered.
- Seven actual software-rendered states are byte-identical across all four
  configurations and visually inspected. Corrected title/local-battery glyphs
  are in screenshots/contact-sheet.png.
- Actual producer codec -> patched scanner AD parser -> consumer session passes
  normal/sanitized, including duplicate field ambiguity, recovery and age.
- 20 new build tests, 8 existing Quick-native selection and 14 alarm-native
  selection tests pass. Explicit numeric local version must exceed the selected
  ordinary profile; 0.4.21 is only a test input, not reserved/verified unused.
- Actual Runtime 13eca114cb58469cf6004fafc3da84fbd9d29beb passes 11 admission
  checks and 4 real app entries in both normal and sanitized modes, retaining all
  eight ordinary emitted Home requirements. Missing sensor provider/grant admits
  Home but yields unavailable; explicit owner grant permits use. API/instance/
  undeclared/duplicate invalid grants reject. Stub providers perform no RF.
- Existing Quick Actions regression: 108 executions pass.
- Feature-disabled quick_actions.c, quick_session.c, quick_render.c and adapter.c
  host objects are byte-identical to the exact System base, with recorded compiler,
  flags and hashes. This is not a target firmware equivalence claim.

ASan/UBSan are enabled; LeakSanitizer is disabled under ptrace. No leak pass is
claimed. Final source/SDK dependency hashes and commands are in receipts/ and
qualification.json; logs contain exact successful test output.

## Source review and paired dependency

Independent review found and verified repair of five concrete issues:
1. Queued Scan could restart RF after modal final close. Live-modal/terminal
   admission guards plus checked post-drain close prevent it.
2. Multiple BTHome service-data fields were silently last-wins. The patched
   provider invalidates the entire ambiguous sample, preserving later recovery.
3. Safe close could fail to restore Bluetooth ON while claiming complete.
   Post-close status now preserves the warning and invalidates radio confirmation
   without re-enabling RF or falsely retaining safe cleanup.
4. Physical Home/crown was treated as page Back. Terminal navigation now closes
   the sheet and rejects pending remote actions.
5. A mandatory sensor declaration would reject an otherwise valid Home when
   absent. The supported optional declaration now permits unavailable operation
   without manufacturing an owner grant.

The consumer cannot detect ambiguity discarded by an older provider. Future
product delivery must pair the exact patched Drivers source, rebuilt under a
separately reviewed version, with the System change. The published retained
sensors ELF is not qualified by this patch's host tests.

## Existing delivery reconciliation

Published X4 test-native-provision-0.1.123 uses System 0651a555 and still displays
local board battery only. Current System 7270a10 and current product 6b7d1abe were
not substituted for that published pin. Existing standalone ble-telemetry 0.1.0
and telemetry-battery 0.1.0 sender releases do not prove receiver/UI integration.
Exact source sets, retained receiver hash, release assets and limitations are in
RELEASE_PROVENANCE.md and x4-123-contract.json. All accepted bytes and pins remain
unchanged.

## Remaining limits and handoff

No Xtensa target compiler was found; no target ELF, firmware, remote CI, flash,
physical radio or Watch/X4 end-to-end test ran. Actual adapter tests use fake
Runtime/sensor callbacks, while optional admission separately runs real Runtime
with synthetic providers. Neither qualifies a product build or physical device.
No persistent authenticated Watch association is claimed; a new advertiser
session may require reselection. Cache is RAM-only. Current means sampled scan
freshness, not a connected live link.

Next authorized engineering handoff: review the paired local patches and draft
PR, select exact product sources and a separately versioned candidate without
weakening frozen gates, qualify target imports/memory/custody and explicit grants,
then perform the separately authorized product/hardware acceptance. Publication
remains held. No approval question was added.

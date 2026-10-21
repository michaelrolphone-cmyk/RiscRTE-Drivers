# USB host mouse source recovery, 2026-10-21

## Result

The missing semantic mouse provider was recovered from the existing, open draft
[Reader PR 468](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/468).
No new mouse ABI or duplicate implementation is needed to start from this source.
It is a source-qualified provider, **not an integrated X4 mouse feature**.
Its own documentation explicitly excludes product consumer/cursor integration,
physical USB mouse interoperability and X4 VBUS electrical qualification.

The provider was first added on 2026-10-09. This recovery does not establish which,
if any, earlier hardware-deployed mouse implementation the original request
referred to. It does not infer nonexistence from the earlier default-branch search.

## Source and custody

- Repository: `michaelrolphone-cmyk/T5S3-Reader`.
- Branch: `codex/usb-hid-mouse-0.1.0`.
- Public source commit: `c334919a05d393b7b3a536fceb117cfbb573a4ef`.
- Source tree: `c1879781b404b11fd933972dbd1c8ebc8cac989b`.
- Evidence-only branch head: `59177418ea746c7605b7b5a5aec501e70bdae80f`.
- Evidence-head tree: `73a10b5e049be16325d9ad10a9c703146ddcfdcf`.
- Public parent: `f99adabdb99562fc3dfada242cc90136c3d553e2`, the still-open draft
  [PR 467](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/467).
- The source commit records original local commit `bf997f3d777e644e98c2f885a9c9f4c81e1a0e48`
  as tree-equivalent. The old local object/ELF was not recovered here; the public
  source tree and its receipt were verified directly.

The evidence successor adds only ten documentation/log files. Its production
source is unchanged. All ten changed-source receipt entries and eight SDK entries
match their archived byte lengths/SHA-256 values. The receipt itself matches
`bf9d42167c859b57e4c0cac1007a897203761111a4d93be34f614486e654c274`.
All seven archived test/build/audit log hashes match. Exact blob IDs, byte lengths,
SHA-256 values and permanent source URLs for eleven important files are in
`source-custody.json`; verification entries are in `custody-checks.json`.

Key source:

- `Drivers/usb_hid_mouse/driver.c`: blob `d8ec90bbb061b2b43e9bcb30a8a81316e9050eff`,
  SHA-256 `ab99811114d55066e0877209cee4dee3ac5b3b44223e76e30bd8df2065daa13f`.
- `Drivers/usb_hid_mouse/Descriptor.h`: blob `cbdb608d866975249f2037ae9aaed9e9e9bcf595`,
  SHA-256 `deb700acd68593c6a3114c891bd4e356c6cf0559357d641a74bd517ae68c0c52`.
- `sdk/driver/RiscUsbMouseV1.h`: blob `3daf083790098344db14c91c9aa7f7acf6795dfe`,
  SHA-256 `bbdf2c8c92adcc23a3a720e51fa630b83f6fa0f0f28853ddab17c1041a37f416`.
- `Drivers/usb_hid_mouse/manifest.json`: `usb-hid-mouse@0.1.0`, driver ABI 2,
  architecture `xtensa-esp32s3`, status `experimental-unpublished`, requiring
  `usb.hid@1` and `platform.clock@1`, providing `usb.hid.mouse@1`.
- [Consumer contract](https://github.com/michaelrolphone-cmyk/T5S3-Reader/blob/59177418ea746c7605b7b5a5aec501e70bdae80f/docs/USB_HID_MOUSE_PROVIDER.md).

The archived target receipt identifies a 15,560-byte Xtensa ELF, SHA-256
`adb4bb40f54c3969e7d8c1c4dd013abcdef3aa3cac1bd058f07d700aeac62bf2`,
exporting only `t5_driver_get`, importing `memcpy`/`memset`, with 33 audited
relocation sites. This pass verified the receipt/log bytes, **not that ELF**.
No target rebuild or binary recovery was attempted.

## Existing ABI and consumer boundary

`risc_usb_mouse_api_v1` has `subscribe`, `unsubscribe`, `poll`, `next`,
`snapshot` and `status`. Events are copied values with relative signed x/y,
wheel/pan counts, button state/transitions, device/session identity and kind.
The consumer owns cursor/scaling/scroll policy. New subscriptions begin in GAP;
snapshot establishes a baseline. Sticky GAP requires discarding old button
history and resnapshotting; lost relative motion cannot be reconstructed.
Disconnect releases prior held buttons. Consumers must unsubscribe before
releasing the graph lease and retain failed cleanup ownership.

Bounds include four mice, four subscribers, 32 queued events per subscriber,
descriptor <=512 bytes, report <=64 bytes, one supported mouse application/report
ID, relative signed fields 2–16 bits, and up to 32 buttons. Boot fallback is
explicit three-byte/three-button protocol, not inferred wheel support.

No product consumer was found in the exact recovered tree. Non-provider matches
for the mouse ABI/capability occur in the header, documentation, builder and two
tests, not in application/runtime input bindings. The real raw-HID/mouse
composition test is a test consumer, not production UI integration.

The unchanged raw HID `close` calls a void host `release` and then returns true.
That cannot certify physical quiescence. Lower-host quarantine and generic graph
pinning remain necessary. This is directly exercised by the composition fixture.
The separate checked-host/controller work must not be bypassed or overwritten.

## Tests replayed in this recovery

At 13:36:12–13:36:14 UTC, Debian GCC 14.2.0 compiled and ran the exact recovered
source with its existing `test/run_usb_hid_mouse_test.sh`:

1. Production parser/event/recovery/bounds/terminal-retention fixture: PASS.
2. Actual raw-HID ELF plus actual mouse ELF composition fixture: PASS.

Both passed again with `SANITIZERS=address,undefined` and
`ASAN_OPTIONS=detect_leaks=0`. LeakSanitizer was not run. The fixture reports
91 cooperative yields and includes malformed inputs, signed widths, report IDs,
multiple devices/subscribers, overflow/GAP, detach/faults, bounded work/retries,
exact failed-close priority, terminal retention and lower-host quarantine.
Fresh commands/compiler/output are in `fresh-normal-tests.log` and
`fresh-asan-ubsan.log`. These are two fixture bodies in two modes, not physical
tests or an end-to-end controller-to-UI test. Historical existing-HID regression
results were hash-verified, not rerun as part of this bounded recovery.

## Live PR status and ownership

Read-only GitHub observations on 2026-10-21 establish:

- PR468 is open, draft, unmerged; head is `59177418...` and base is `f99adabd...`.
- Created/last updated: 2026-10-09T13:05:52Z. Author is `michaelrolphone-cmyk`.
- No assignees, issue comments, inline comments or reviews were returned.
- No explicit owner handoff was found in the inspected PR body/discussion.
  Open/unmerged/old timestamps do not prove abandonment or assign ownership.
- All 11 GitHub check runs at the exact head completed successfully on Oct 9;
  separate legacy commit statuses are empty. See `remote-observation.json`.
- No workflow explicitly references the newly added mouse runner. Those green
  checks are not claimed as remote mouse-test coverage or hardware qualification.
- Parent PR467 remains open/draft/unmerged at `f99adabd...`, last updated
  2026-10-09T12:11:15Z. Preserve the existing stack rather than silently merging,
  rebasing, publishing or treating the whole branch as a new product baseline.

## Smallest remaining local integration slice

This is a proposal for subsequent authorized work, not work started here:

1. Reconcile the existing PR's ownership/handoff and intended consumer location.
   Preserve PR468 source and its ABI rather than create another mouse provider.
2. Add a narrowly scoped consumer lifecycle/event adapter above
   `usb.hid.mouse@1`, using the existing generic context-owned provider lease
   mechanism. For app consumers, the recovered tree already exposes
   `lib/NativeApps/include/T5ProviderCapabilityApi.h` (`acquire`/`release`),
   with capability declarations and permissions remaining mandatory.
3. Host-test initial GAP, snapshot capacity failure, exactly-once relative events,
   per-session held buttons, overflow recovery, disconnect, focus/lifecycle
   boundaries and failed unsubscribe/release retention using the existing ABI.
   Keep cursor placement/scaling and the choice of a product UI consumer explicit;
   no existing universal mouse/cursor path was recovered.
4. Before moving the provider into current Drivers packaging, compare the exact
   existing headers/build inputs, not the entire stacked Reader branch. The
   current Drivers `a7ae399...` has byte-identical raw-HID code, HID ABI and clock
   ABI, but `RiscProviderV2.h` differs by optional stream/poll extensions. The
   mouse uses the base descriptor; source/target compatibility still needs actual
   admission/build verification, not an assumed drop-in claim.

This slice requires no invented VBUS GPIO, power policy, role switching, product
pin change or hardware operation. Full X4 selection, root-provider integration
and physical acceptance remain separately blocked by authoritative electrical
evidence and the existing lower-host lifecycle contract.


## Capture note

This report records the bounded source-recovery observation at 13:36 UTC. Later source/evidence capture is in Drivers draft PR28 and System Apps draft PR151. Its proposed next steps are historical, not a claim that the captured mouse consumer is qualified. No production mouse, role/power policy or hardware acceptance is asserted. Administrative discovery notes and raw tool/account snapshots are excluded.

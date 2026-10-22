# Opt-in raw HID checked cleanup

Status: source checkpoint, qualification pending. This is not product acceptance,
a firmware release, or a ready-to-merge claim.

## Candidate and preserved baseline

`Drivers/usb_hid_checked_cleanup` is the separately built `usb-hid` 0.1.3
candidate. Its only builder is `scripts/build_usb_hid_checked_cleanup.py`, writing
`dist/usb-hid-checked-cleanup`. No default build, product selection, canonical
inventory, or Runtime admission policy selects this candidate. The legacy
`Drivers/usb_hid` 0.1.2 source, manifest, builder, and recorded artifact are unchanged.

Before selection on 2026-10-22 UTC, live Drivers/main and Reader/master manifests,
Reader/release-index, and both repositories' matching `driver-usb-hid-v` tag
names were inspected. The observed published version was 0.1.2; no published
0.1.3 conflict was found. This does not reserve a future release tag.

The candidate requires the existing complete `risc_usb_host_discovery_v1` table,
including `release_checked` and `control_claim`. It does not add or change an ABI.
Legacy void release and device-token control are not fallback paths.

## Custody semantics

- Reserve a non-reused HID identity before claiming the host interface.
- Preserve nonzero host cleanup custody even when claim returns false. Raw open
  still returns zero; immediately attempt checked cleanup. If cleanup fails,
  retain the unpublished orphan and exact token for provider quiesce cleanup.
- On close, revoke session I/O and presence before checked release. False release
  retains both tokens and the host dependency, and returns false to the caller.
- A published closing session is retried only by an exact-token close call;
  quiesce cannot silently consume a caller-owned handle. Stop cannot clear a
  dependency while custody remains. Stale/foreign or unpublished handles fail.
- Descriptor and boot-protocol operations use claim-qualified control, retaining
  the existing interface recipient and 100 ms maximum argument. No scheduling or
  timing qualification is implied.

## Exact cohorts are not interchangeable

This branch is based on Drivers PR40 head
`90e7a8faae1a6678a24c92c32f95032b2fbe1e24`. Raw baseline blob is
`f404a73dcea13d4a01d51efa4205e64644fb78d2`. PR40 host blob
`9bb24e8173eb657e4d670f914cac52e0abd22720` preserves false-with-nonzero
controller claim custody. PR41 head
`501e22b5bc41d39fee6b15346a25a87ab7ad5df3` has matching relevant ABI headers
but an older claim body: false-with-nonzero physical custody is not exposed to
raw HID there. Header equality does not establish host behavior equivalence.
Do not transplant PR41 into this candidate's required failed-open composition.

The unchanged Reader mouse source is pinned to
`c334919a05d393b7b3a536fceb117cfbb573a4ef`; existing Reader PR468 remains
separate. Existing System Apps PR156 at
`bfd82277b499c53d3f1e8cba487693bd5b7ce14e` and original PR151 are unchanged.
The pinned SDK cohort underlying PR40 remains Runtime
`6a3b3d46f5b2857eacc6e9863799dec1f466b036`, ESP-IDF
`fff9895c82d744c7237be8847347bdd1b07c6643`, esp-usb
`0f6820940f9e8075c0cc1483d9c2f5e89cc5b97a`, and GCC
`esp-15.2.0_20251204`. No SDK or dependency lock is refreshed.

## Pending qualification and explicit limits

Strict C compilation and one local candidate Xtensa build are preparatory only.
Published exact-head normal/sanitized custody and checked-host → raw-HID →
unchanged-mouse tests, target artifact checks, and remote artifact verification
remain pending at this checkpoint. Local-only work is not delivery evidence.

The real host will be composed with a deterministic controller boundary; that
is not actual USB hardware, callback timing, or production Runtime composition.
Existing Runtime already fences retained invocations. No new retention mechanism,
polling scheduler, mouse-to-touch/UI policy, electrical adapter, trusted admission,
product grant, firmware, tag, catalog, device operation, merge or release is added.
PR40 controller 0.1.27 is not admitted by the existing exact 0.1.25 Runtime policy.
The 20 ms mouse report / 100 ms control limits are not assumed to fit the 8 ms
provider / 10 ms pass schedule. Lower-host quarantine is not claimed broken.

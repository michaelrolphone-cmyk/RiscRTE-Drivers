# Later local source custody: time sampler

`qualification/local-source-custody.json` extends source declarations without
rewriting the historically pinned `manifest/local-source-custody.json`.
The parity command composes both through the same `validate_custody` path;
duplicated paths, identity conflicts, invalid provenance and source mutations
remain errors. No release inventory, historical byte lock, source baseline or
candidate manifest is changed.

`Drivers/time_sample` is a standalone local adapter from
`platform.time-sample@1` to `net.time-sample@1`, package ID `time-sample`.
The custody kind is not a release lifecycle: its manifest remains
`candidate-read-only-network-time`, physical verification pending. Schema
`candidate` denotes an alternate path sharing an existing upstream package ID;
this adapter has an independent identity. Neither upstream parity nor release
qualification is claimed.

Immutable source anchor: RiscRTE-Drivers commit
`a45497f5df164ace12e3b56b615a3eaa76581f2c`, complete directory tree
`3abc62d709d4ece9c27cb66606aa7dc9142ffd38`. The source is unchanged from original
local commit `873adb74adf0256b1fdd1dac5e915e193ea50231` and the retained local
build `21b9d1474d23301492c3fb938f9f793deba3fb85`. Retained build receipts match
provider ELF SHA256
`fa75aed47479015b1e1e9fff5cb9794f69be4780fc4fbdd7de6c9b091c4a8a56` and record
Runtime `046f4c22b263739f3bf3f96f888617d72eb3d498`, optional native time sampling
and wifi_settings consumption. This is retained build evidence, not a fresh
build, current hosted selection proof, complete tool/header closure or device
acceptance. Network time remains unauthenticated SNTP; application UTC mutation
remains a separate alarm-safe transaction.

Regression tests compare complete diagnostic multiplicities: only the later
untracked-directory label is removed, package rows and frozen inventories remain
identical. The original eight-entry historical witness is unchanged. All other
migration, source drift, build, network and physical uncertainty remains visible.

The existing Badge CI provenance hydrator uses the same composed declaration
loader before offline validation. It validates all repository/path/commit/tree
records before fetching only exact absent declared commits. Later manifests do
not authorize latest-ref fallback, arbitrary repositories, changed HEAD or a
source checkout. Its real-Git regressions cover shallow-object recovery,
offline reuse, unavailable later commits and cross-manifest conflicts. Badge
workflow path filters include the later declaration and shared loader.

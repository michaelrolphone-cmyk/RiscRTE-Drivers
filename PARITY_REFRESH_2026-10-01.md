# Master parity refresh — 2026-10-01

Reader master: `1e0188c1ff0234dd33fe054c9a6fb4fde36596df`.
Published release-index: `572746f4fcf3fde19947a066b7e5c8028cd76d21`.
Fresh target/source tree audits preceded writes; no external source conflict was found.
Independent build tooling and pinned SDK were retained. Published versions were copied,
without inventing a further version bump for byte-identical released payloads.

Target base: `9039be6c9abb30742b7a77a8ef39d507aaf01cea`.
Only the USB navigation C/manifest pair drifted. The local base tree matched the
recorded source-tree baseline. All other driver source files remain untouched.
The current source fixture was copied without altering external build infrastructure.

`usb-ui-navigation` 0.1.1 → 0.1.2: X is Back in HID and normalized XInput;
A/B have no Back alias. Press, held state and release assertions pass for both protocols.
The standalone pinned compiler build reproduces 7,212 bytes with SHA-256
`c907a608f746444831cd31638182d119b198c2e70188177a8cca3d9e4a42e2c7`.
The builder now enforces that published identity. Source inventory, released-version
inventory and per-package documentation are synchronized to the pinned master/index.

Full independent driver pipeline and exact-head CI are required before ready/merge;
run evidence is recorded in the PR and maintenance claim #5. Existing unrelated
migration limitations remain in MIGRATION_STATUS.md. This source/released-byte
refresh is separate from prospective U1 ZIP, architecture remediation, hardware
qualification, independent publication and runtime cutover. None is claimed here.

# CP210x current-source / historical-byte linkage

This declaration reuses the existing current-source qualification consumer and
immutable producer-lock support. It does not change the checker, source, SDK,
builder, historical inventory or accepted release bytes.

The current directory tree is `4c7f4db302942a6915a039be4dc7190426e85907`,
package `usb-cp210x-v2` version `0.1.8`. Exact source/build inputs match the hosted
artifact retained from Drivers commit
`562a86baff1d6fd4f652ed85d395a27dccaabe32`: 10348 bytes, SHA256
`097b67b120885a68f0780b20b29aac0f95060b7dd7714f76bbf9dfe742193e64`.
It explicitly does not reproduce historical accepted bytes.

The historical replay is version `0.1.0`, tree
`876bba87ab601fafbcd873ae3fd0e14f96151d3b`: 5788 bytes, SHA256
`96f22bbb6cc8cf00f138656a910d3078d491d6c117ffc4e23a2842c458481c61`.
The acceptance anchor is the original producer
`2fda8da83276a263a5fb902cebf7b5eac21674ee`, not mutable current-builder constants.
Its builder blob `b17631f61a3ec0591cbc4e4a238797f7069fe87b` supplies literal
`CANONICAL_SHA256` / `CANONICAL_SIZE`; the declaration separately binds its SHA256.
Raw downloaded manifests and build receipts are retained unmodified under
`qualification/evidence/usb-cp210x-v2/`.

The source/release snapshot remains explicitly pinned. Only two CP210x diagnostics
are resolved: current source version versus historical metadata, and refresh of
the recorded historical tree. Complete package rows and historical input bytes
are preserved. The actual current/upstream source conflict remains an error.

Actual-source regressions reject changed source, changed shared SDK, altered
canonical annotations even with a refreshed evidence hash, changed historical
artifact identity, unavailable/wrong original producer, and incomplete historical
source coverage. CP-specific failures cannot suppress CH qualification; a changed
shared SDK correctly invalidates both affected bindings.

Limits: this is source/build evidence binding, not behavioral equivalence, physical
device acceptance, adoption by a current firmware consumer, or release approval.
Available SDK hashes are over-approximate and do not prove every header was used.
Compiler/library/inspection-tool hashes are not complete environment closure.
Hashes bind retained evidence bytes, not independent cryptographic authentication
of hosted execution. No new target build was run for this metadata-only increment.

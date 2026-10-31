# BQ25896 current source and historical replay

The recorded local/base BQ0.1.6 tree17c99241 is identical; no local source changes
are overwritten. Current0.1.9 driver, charger-profile and shutdown helpers and
manifest are exact Reader9c8783d6 source tree9aa7bab2 files. They add real charger
readback/uncertainty and one-way shutdown ownership behavior, not mere metadata.

`qualification/bq-source-inputs.json` binds the original source and authoritative
VBUS header at Reader `sdk/driver/RiscUsbVbusV1.h`, blob8a58c88b. Its exact copy at
`Drivers/bq25896/RiscUsbVbusV1.h` is an explicit package-local dependency overlay,
not a silently altered shared contract. Existing shared SDK bytes remain intact;
this is not a ban on later compatible SDK evolution. The new header inserts only
the charger snapshot/API after the existing monitor/external-host prefix. Tests
verify original shared bytes, all prior offsets/sizes, tail placement and bounded
struct-size admission. Old consumers compile against the old shared header and
link to actual new provider source; new tests explicitly use this one overlay.
Future consumers should reuse this hash-pinned authoritative header location or
perform a separately qualified common SDK update, not invent another variant.

The T5S3 charge values in the upstream helper are board-specific:1000mA input,
512mA charge,4208mV regulation. They are not generalized electrical defaults.
A successful shutdown ACK does not establish observed power-off. Once BATFET may
have been written, provider custody remains pinned even on NACK; no readback or
unload is assumed possible after power loss. Existing OTG/lease/external-input
vetoes and source-off verification are preserved. No second chip owner is added.

The existing builder now defaults to pinned historical0.1.6 source. `--current-source`
selects0.1.9 and a separate output directory; it cannot alias/overwrite historical
output or combine with `--require-byte-parity`. The historical producer is
 a7ae39994603c4545a3c014f09051ecfc12084d2, accepted12660bytes/SHA
5f50b5eb048085e3128939b6bed3693a9a44ebe27c9e2cae3b40dd725aa6a4c4.
Historical parity mode uses exact original headers/normalizer and unchanged old
compiler flags. Current mode retains existing compiler flags and finds the exact
package header through normal quoted-include precedence. Current/historical
receipts bind source files, headers, helpers, compiler/libgcc/readelf and outputs.
They are run provenance, not deterministic receipt identity or complete implicit
toolchain closure. Available SDK inventory is broader than actually consumed files.
Failed historical parity prints its provenance before temporary staging is removed.

Tests cover actual old-prefix OTG/lease/rollback behavior, charger pre-read and
partial-write/readback failures, shutdown external/OTG veto, uncertainty recovery,
BATFET ACK/NACK, and three negative mutations. Normal/sanitized host fixtures use
fake register/controller boundaries. The hosted current build also compiles prefix
assertions with the actual target compiler; historical GCC8 replay is independent.
Target compiler artifacts and byte-parity success remain pending until those jobs
run. No physical battery safety, actual shutdown, firmware adoption or new release
acceptance is inferred from local host tests. Existing historical GCC8 loop remains.

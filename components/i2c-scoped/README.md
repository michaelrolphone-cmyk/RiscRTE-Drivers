# Scoped I2C provider

Drivers-owned, unpublished adaptation of the existing T-Watch `twatch-i2c@0.2.0`
provider. This is not the legacy firmware I2C shim. It consumes the actual
`platform.i2c.controller@1` device-scoped Runtime table, together with
`hardware.device@1` (`controller.i2c@1`) and `platform.sync@1`. It provides the
unchanged `i2c.bus@1` ABI. No board pin values or downstream chip protocols are
embedded here.

The source provenance and exact original hashes are in `source-map.json`.
The upstream repository is under the same owner; its `licenses/NOTICE.md` is
recorded there. No new third-party license grant or redistribution permission
is asserted by this adaptation. SDK headers are staged unchanged from the
selected Runtime and Drivers trees by `scripts/cst9217_sdk.py`.

The narrow adaptation replaces an `atomic_flag` whose Xtensa compare-and-set
could target PSRAM-backed provider BSS. Runtime owns the bounded, nonrecursive,
owner-task-only lock. This is not a cross-thread mutex. Child claims remain
exclusive, eight slots maximum, with monotonic tokens across restart, 7-bit
addresses 8..119, at most 512 bytes per phase, and total timeouts 1..1000 ms.
No unlock, close, or destroy failure erases retained custody. Active children
prevent quiescence. Failed unlock disables new transfer authority and cleanup
retries the retained lock. A fenced Runtime may refuse lock admission, so the
provider correctly remains mapped until cleanup can complete.

Run `python3 scripts/test_scoped_i2c.py --runtime RUNTIME --cc XTENSA_GCC`.
The test runs normal and ASan/UBSan host coverage and checks the target shared
ELF disassembly for compare-and-set plus imports/exports. Compilation and
host/native-boundary modeling are not physical hardware qualification.

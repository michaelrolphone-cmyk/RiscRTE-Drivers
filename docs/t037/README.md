# T037 shared HID qualification

Production source remains 58ad37140bd86bd2247bfa3244c4acc8942b2bd8 (HID 0.1.4).
Watch 1.1.30 and X4 0.1.116 already deliver the exact compact ELF; Wave 0.1.33
still delivers 0.1.2 and is upgraded by T039. The rebuilt compact SHA-256 is
6290d9cdfae051b9f2f2c83060f1c4000a62f727d47b680c67cd77b0d9e0c555.

The complete port/protocol suite passes normally and with ASan/UBSan, including
Secure Connections, numeric comparison, encrypted ATT report/boot protocol,
keyboard/mouse queues, scroll, backpressure, watchdog, disconnect recovery,
persisted bond reconnect and Forget/new-host races. The policy remains one
bonded peer; Forget permits switching hosts. No native controller change.

T039 also joins the production Buttons/FT6336U input path to this real NimBLE
implementation and an independent central, asserting 15 exact press/neutral
pairs before and after saved-key reconnect. Evidence is in Utilities docs/t039.
No physical RF, keyboard OS interoperability or device flashing was performed.

Reproduce with test/run_ble_hid_test.sh, then SANITIZE=1 test/run_ble_hid_test.sh.
Build target with scripts/build_ble_hid.py and the pinned Espressif 8.4.0 compiler;
compact metadata with RiscRTE-T-Watch-S3/scripts/compact_current_elf.py.

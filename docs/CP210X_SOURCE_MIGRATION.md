# Scoped CP210x source migration

Reader9c8783d6f20659a514bfaeb3847a77c7213dc4f6 CP210x0.1.8 is retained with one
explicit include of already-present RiscSerialPortV1.h; frozen USB headers remain
unchanged. Upstream manifest status `u1-class-functional` is source metadata, not
an independently established hardware-qualification claim.

Compared with retained0.1.0, this class tracks UART disable and physical release
as two acknowledged stages. Failed poll/inventory is unknown, not proof of detach;
a UART NACK while attached does not permit forgetting a claim. Confirmed host
detach skips impossible UART control and retries the same checked release. Failed
open orphans retain provider custody. Inventory, stream pump and generation-bound
sessions are additional current behavior, not source-equivalent cosmetic changes.
Retained host can itself preserve ownership even if an older class discards state;
this migration is not justified by an unsupported blanket unsafe-unload claim.

The actual retained host composition tests orphan recovery, attached disable
failure, failed checked release, host terminal-receipt consumption without duplicate
physical release, and detach recovery. A mutant that ignores failed checked release
must fail the specific close/quiesce assertion. Both normal and ASan/UBSan modes run
the same contracts. Local leak detection is disabled only for the documented ptrace
limitation; hosted sanitizer defaults are unchanged. Controller callbacks remain
simulated; this is not physical USB or complete runtime graph qualification.

Historical0.1.0 remains default pinned tree876bba87ab601fafbcd873ae3fd0e14f96151d3b.
Exact-byte replay pins original producer2fda8da83276a263a5fb902cebf7b5eac21674ee,
its USB/Provider headers and unchanged normalizer. Accepted5788B/SHA256
96f22bbb6cc8cf00f138656a910d3078d491d6c117ffc4e23a2842c458481c61 is unchanged.
--current-source selects0.1.8 with separate staged outputs; aliases and mismatched
retained outputs are rejected. Failure provenance survives temporary cleanup.
Clock generic staging/hash helpers are reused, hash-bound, without running clock.

This depends on the separately reviewed canonical shared pump/imported-support
candidate; it does not duplicate or change that held payload. The workflow patch
is layered after that candidate and requires coordinator reconciliation if its
base changes. CDC identity variants, native CDC, host behavior, frozen SDK,
historical inventories and independently held metadata qualification are untouched.
Native current and historical GCC8 ELF/accepted-byte checks remain hosted pending.

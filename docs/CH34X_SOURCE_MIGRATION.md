# Scoped CH34x source migration

Current source is Reader9c8783d6f20659a514bfaeb3847a77c7213dc4f6's CH34x0.1.7
plus an explicit include of the already-retained RiscSerialPortV1.h. This avoids
changing frozen RiscUsbProviderV1.h. No retained host source, HID control policy,
legacy CDC ABI1, CDC-v2 identity, CP210x source or native CDC candidate is changed.
The source provides inventory, checked claim/control/release, orphan recovery and
bounded provider-owned streams; it is not equivalent to the historical0.1.0 merely
because both advertise serial.port@1.

The exact shared SerialStreamPump.inc is retained at canonical Drivers/common.
It is one selected dependency of upstream's two-file common directory; the unrelated
pcf8563_rtc_ops.h is not imported. Do not claim complete common-directory equality.
Shared pump is a declared build input. Its source custody/import admission is a
separate scoped integration delta, not a reason to hide the path or relax own-repo
custody validation. Until admitted, the new untracked common diagnostic is expected
and must remain visible. Historical source/release inventory is unchanged.

The existing build_usb_ch34x_v2.py remains the sole CH34x builder. Default behavior
uses historical tree6b2dc3c8e1f685d6fdaba58d04a763378f524b1b and version0.1.0;
--current-source selects0.1.7 and a distinct output directory. Generic hash/staged-
publication helpers are reused from the already-qualified clock builder, whose
source hash is included in run provenance. That import does not run a clock build.

--require-byte-parity selects exact headers from historical producer504403db4ae9a315bb57e3ef98ed18a54de91641
and its unchanged normalizer. Accepted0.1.0 remains6340B/SHA256
8d88d227ac61116116b0ffb7c0e4052b7b547a0a19edc13a13af08792b95d621.
Current source is not historical accepted-byte replay. Native and GCC8 hosted
qualification are separate. Neither old accepted hashes nor release metadata is
updated to make a new build pass.

Builds stage before publication and refuse different retained files. Current output
cannot alias the historical output. Failure prints actual/expected byte and source/
compiler provenance before temporary cleanup. Run receipts pin source, SDK/helper,
shared dependency, compiler/libgcc/readelf and invocation; temporary paths vary,
and implicit compiler subprocess/system-library closure is not exhaustive.

Host tests cover the new class, actual retained host with simulated controller,
failed-open orphan/failed-close/checked terminal receipt ownership, bounded shared
pump backpressure/order/invalid counts, and a negative dropped-byte mutation.
The old CH34x fixture still runs against the exact historical source. These are
not USB hardware execution, complete firmware/runtime graph proof, target ELF
qualification or empirical equivalence. Hosted compiler/export/format/accepted-byte
checks must supply the separate target evidence.

The same runner supports normal and ASan/UBSan modes, including the actual host
and negative dropped-byte assertion. Both run in hosted CI. Local sanitizer
qualification disables leak detection only because LeakSanitizer explicitly
rejects this ptrace environment; hosted defaults retain leak detection. No local
leak-clean claim is made. The expected mutation must fail at the pending-byte
assertion, not merely crash for an unrelated reason.

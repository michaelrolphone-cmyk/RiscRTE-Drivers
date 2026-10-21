# T043 local BLE telemetry qualification

Observed 2026-10-21 UTC. Result: existing device-agnostic implementation is present and passes software qualification; a concrete fixture coverage gap was repaired locally. No production defect was reproduced.

## Deliverables

- Local commit: 7eebf43382df16f1927aef27b12487cb2fda422d.
- Base: a7ae39994603c4545a3c014f09051ecfc12084d2.
- Branch: tasker/t043-telemetry-qualification-local.
- Patch: t043-capability-custody-tests.patch.
- Proposed PR text: DRAFT_PR.md.
- Evidence: evidence/source-hashes.json, baseline-mutations.json, final-mutations.json, final-committed-host.log, final-committed-sanitized.log, hid-regression.log, python-unittests.log, published-package-validation.json, and remote-source-evidence.json.

The sole repository change is test/ble_telemetry_test.c. No production source, SDK, builder, release artifact, catalog record, or hardware state changed.

## Verified contract

The provider validates the complete bounded catalog from sensor.telemetry, selects stable source IDs, and reads only those selected IDs through that capability. Non-null capability context forwarding is now asserted for enumeration and reading. A strict temperature-only publication remains correct when enumeration order reverses, refreshes a valid zero instead of treating it as missing, and stops with controller release when the selected field disappears. The battery adapter obtains values only through board.battery. Generic source inspection finds no direct board model, register, GPIO, or runtime-health lookup.

The existing full fixture also covers explicit public consent, duplicate/unknown IDs, value ranges, catalog failure, source unavailability, changing metric identity, entropy failure, transport errors/timeouts, stale tokens, retained close/quiescence custody, and power restoration failure. Codec tests cover all seven scalar metric bounds and the advertising payload budget. The separate HID suite still passes authenticated Secure Connections, bonding, downgrade rejection, reconnect, and custody paths.

## Reproduced coverage gap

Temporary copies of production source were mutated independently to drop the enumeration context, drop the read context, or sample an unselected battery field. All three survived the old test; all three are caught by the amended test. The mutants never enter the repository or accepted artifacts. To reproduce: run python evidence/check_mutations.py --baseline, then python evidence/check_mutations.py require-killed from this deliverable directory.

## Existing delivery and source identity

The historical publication record is accurate. The standalone packages live in RiscRTE-T-Watch-S3, not a standalone release in RiscRTE-Drivers or the current T5S3-Reader index. Live release metadata is captured in remote-source-evidence.json. Exact ZIP copies in the Watch release's immutable-driver-inputs archive match the published GitHub asset digests. The enclosing archive's Git blob matches the firmware-v1.0.12 tree. Package manifests preserve their historical physical-pending labels.

ble-telemetry package: 66,832 bytes; SHA-256 8cece7a6e8f3dbe8ec2ef9a0f0dfbd919c147152c695143730d04cb10c80a5b1. Its accepted ELF: 14,712 bytes; SHA-256 677ac910f378af62dc3e0163c9dfdc3bc497e6d9ec990c13d8b3be52d96025de.

telemetry-battery package: 61,634 bytes; SHA-256 8859f855a59ba92709840b003a8121e91d69f1badfd8b738a6dae35bf44b3f3d. Its accepted ELF: 4,344 bytes; SHA-256 2d588612bd7c0e4764d7b995ffd5cde3b9d76d2a1af74c0cce0ae77855dd1cd7.

The relevant production files are byte-identical between accepted Watch source 4088b6892c2e2654b0342f04a7d191068e4a8e2e and reviewed main a7ae399. Exact-baseline CI run 38800327791 passed telemetry normal/sanitizer tests and native target builds. Its GCC 15 output is a distinct software artifact, not historical accepted-ELF byte parity: telemetry 14,636 bytes / 355b04b964ca1e7041cdc7d255b7841c16bebd9d226371c2e7c82afc2f53eb27; battery 4,372 bytes / 2b4b842cdd5565b04d13b7f8c6c914a0d2450cb41c81d8e90d406af610c6fc8e.

## Limits and next action

All five final host suites passed normally and with ASan/UBSan; LSan cannot operate under this environment's ptrace. The complete normal HID suite and 14 Python tests passed. A fresh target build was unavailable because no Xtensa compiler is installed. This limitation does not invalidate the verified existing baseline CI, but no exact-new-commit CI or whole-repository green result is claimed. Existing baseline parity failures remain outside this task.

The user's computer was offline and no saved coding environment was available; all work used a new isolated cloud checkout. The T039/T040 checkouts, canonical ESP-IDF, Xylem, and other active owners were not edited or displaced.

No real device, radio, pairing, flash, firmware publication, or release operation was performed. Available temperature on real deployments depends on an authorized sensor.telemetry source; a host mock is not proof that every target supplies one. Open advertisement behavior is not secure paired GATT. Product integration, actual receiver behavior, freshness/stop handling and radio restoration remain qualification work for the existing owners. The integrated Watch acceptance statement is not an exhaustive telemetry-specific acceptance.

Remote publication is held. This local test-only commit needs separate publication authorization if selected; do not reuse the Spectrum/T039 approval. T043 should remain awaiting qualification, not complete.

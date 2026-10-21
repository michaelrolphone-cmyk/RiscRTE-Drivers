# Draft PR: Qualify BLE telemetry capability context and selected-field custody

Base repository: michaelrolphone-cmyk/RiscRTE-Drivers
Base branch: main
Base commit: a7ae39994603c4545a3c014f09051ecfc12084d2
Prepared local branch: tasker/t043-telemetry-qualification-local
Prepared local commit: 7eebf43382df16f1927aef27b12487cb2fda422d

## Summary

Close two demonstrated gaps in the existing generic local-device BLE telemetry fixture without changing production code or accepted release artifacts:

- Require both telemetry enumeration and value reads to receive the exact non-null capability context.
- Publish a strict temperature-only subset and reject every attempt to sample an unselected battery or humidity field, even if its value is discarded.
- Check exact initial and refreshed advertisement bytes, valid zero temperature, stable-ID selection after discovery order changes, unavailable-read output preservation, and fail-closed cleanup when the selected field disappears.

Only test/ble_telemetry_test.c changes: 31 insertions and 5 deletions. The existing fixture runner already executes this test in normal and sanitizer CI paths.

## Why this change is needed

The existing fixture bound a null source context and published all three enumerated fields. In temporary isolated mutation checks, all three regressions below incorrectly passed that fixture:

1. Replacing the enumeration context with NULL.
2. Replacing the read context with NULL.
3. Sampling an unselected battery ID before encoding the selected values.

All three fail the strengthened fixture. The actual production implementation passes. This is a regression-coverage repair, not a claim that the production driver currently has any of those defects.

## Existing delivery preserved

The generic provider obtains its complete field catalog and each selected value solely through its bound sensor.telemetry capability. It contains no device model, GPIO, register, or board enumeration. The battery adapter separately uses the board.battery capability. Temperature is supported by the generic scalar ABI when supplied by the selected source; this does not prove an installed physical temperature source on every target.

The source is unchanged from the accepted Watch driver source snapshot 4088b6892c2e2654b0342f04a7d191068e4a8e2e to the reviewed main baseline. Live GitHub metadata and exact repository-custody archive bytes agree for the existing releases:

- [ble-telemetry 0.1.0](https://github.com/michaelrolphone-cmyk/RiscRTE-T-Watch-S3/releases/tag/driver-ble-telemetry-v0.1.0): 66,832-byte package, SHA-256 8cece7a6e8f3dbe8ec2ef9a0f0dfbd919c147152c695143730d04cb10c80a5b1.
- [telemetry-battery 0.1.0](https://github.com/michaelrolphone-cmyk/RiscRTE-T-Watch-S3/releases/tag/driver-telemetry-battery-v0.1.0): 61,634-byte package, SHA-256 8859f855a59ba92709840b003a8121e91d69f1badfd8b738a6dae35bf44b3f3d.

Those archives retain their historical experimental/physical-pending manifest labels. They were not rewritten. General owner acceptance of an integrated Watch image is not a telemetry-specific or cross-device hardware pass.

## Validation

- bash test/run_ble_sensor_test.sh: all five suites pass on the final committed tree.
- ASAN_OPTIONS=detect_leaks=0 SANITIZE=1 bash test/run_ble_sensor_test.sh: all five suites pass on the final committed tree. LeakSanitizer is disabled because the execution environment uses ptrace; the first default LSan attempt failed for that environment reason. ASan and UBSan remain enabled.
- Three temporary mutation checks: old fixture accepts all three; strengthened fixture rejects all three.
- bash test/run_ble_hid_test.sh: complete normal host suite passes, including authenticated Secure Connections, downgrade rejection, persisted bonds, reconnect, backpressure, and failed-close custody.
- python -m unittest discover -s tests -v: 14 tests pass.
- git diff HEAD^ HEAD --check: passes. Working tree clean.
- [Existing exact-baseline telemetry CI](https://github.com/michaelrolphone-cmyk/RiscRTE-Drivers/actions/runs/38800327791): normal tests, sanitizers, native target compilation, and artifact upload all succeeded for a7ae399. This is baseline evidence, not CI for the local test commit.

The current environment has no Xtensa compiler, so a fresh local target build could not run. Existing exact-baseline CI built the unchanged telemetry production source with the native GCC 15.2.0 toolchain. The source-only test change does not rebuild, replace, or publish the historical accepted ELF. Whole-repository CI is not claimed green: separate pre-existing parity jobs on the baseline fail.

## Safety and scope

No production provider, ABI, manifest, firmware, release, catalog, storage namespace, pairing logic, or controller ownership change. T042 remote sensor reading remains separate. Open nonconnectable telemetry still requires an explicit selected-ID list and public-broadcast consent; it is not confidential paired GATT. No radio or hardware operation was performed.

Publication is held. This is draft text only: no branch push, PR creation, merge, tag, release, or deployment has been performed. Publication of this test-only branch needs separate authorization; the existing Spectrum/T039 request does not cover it.

## Remaining T043 qualification

After any authorized publication, review exact-head CI. Separately, the existing hardware/integration owners would need source-attributed evidence for real battery/available temperature publication, receiver interoperability, refresh/stop/error behavior, and radio lease restoration on each selected target. No hardware test was requested or launched by this work, and the task is not marked complete.

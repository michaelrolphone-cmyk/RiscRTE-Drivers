# Nearby BLE sensor provider: local qualification

Task: 52a6c1b9-435d-47da-ae76-738635d6b67a, Expose nearby BLE sensor readings.
Date: 2026-10-21 UTC. Owner: Tasker.

## Result

The generic provider is already implemented. This pass reconciled the existing
source and recorded artifact chain, then qualified its capability interface with
synthetic transport. No new production defect was reproduced and no production
repair was made. The local test-only checkpoint is
`a66670d605fc88a3e562e48ceeb4021fb9a6a5f5`, branch
`tasker/t042-nearby-provider-qualification`, based on T061's exact repaired
Drivers source `ba28f9bdfbe84d23de36350189cc2ec89fea1677`.

The task is awaiting qualification, not completed or hardware accepted. The
source/host result is distinct from a released repaired provider. This pass does
not qualify Scanner UI, saved aliases, or T061's Quick Actions consumer.

## Existing implementation and supported scope

`Drivers/ble_sensors/driver.c` exports ABI-v2 driver `ble-sensors`, providing
`bluetooth.sensors@1`. It requires exact `bluetooth.hci@1` exclusive-host and
`platform.clock@1` contracts. Capability activation performs no radio I/O. Only
explicit open claims the shared lease. Scans are passive, bounded to 15 seconds,
retain at most 32 address/type identities, and process 1–16 events per poll.
Callers retain cleanup custody until close succeeds.

The capability returns copied identity, raw advertisement, advertised name,
RSSI, service/manufacturer metadata, sample ages and supported BTHome v2 readings:
- battery percent;
- signed temperature in centi-Celsius;
- humidity in centi-percent;
- pressure in centi-hPa;
- illuminance in centi-lux;
- voltage in mV;
- charging boolean.

Repeated supported objects remain ordered. Unknown IDs retain only a marked
partial supported prefix. Malformed known fields invalidate the whole sample;
encrypted and unsupported versions expose no readings. Unsupported GATT-only
services are listed as metadata, not decoded. This is not a universal GATT reader,
decryptor, pairing implementation, authenticated identity or continuous live
measurement. An advertisement without a new measurement may update seen age
without refreshing the last measurement's age. Aliases remain application-owned.

## Source and artifact reconciliation

The historical receiver source is Drivers
`4088b6892c2e2654b0342f04a7d191068e4a8e2e` (tree
`1c59a23db93d88ea11a2bc4964004df3505dc7d7`), recorded in the X4 .055 executable
plan. Its provider C file, SDK contract and manifest are identical to the local
repaired source. The only production delta is T061's existing repeated-BTHome
service-data ambiguity repair in `ble_scan_core.h`; it was reused, not recreated.

The reconciled X4 .123 release retains that older receiver rather than building
it from the .123 newer Drivers source input:
- sensor ELF: 9,820 bytes, SHA-256
  `cf34fa23f8a6086c7dbeb022a5ddc252088207eb0773251d183873bf1737d998`;
- manifest: 470 bytes, SHA-256
  `e1cbdd0a2490a88cd8984404e37b6ef059403d31f87f47be9c7756fb0133b0b5`;
- .123 product producer: `93ee386b7e23628f3776edc6cfd2805cf675d649`;
- .123 Drivers input: `5890721073ebabd781d5d278e447c761947a5c58`.

These identities were checked against existing local exact source contracts,
recovery executable plan/build-custody records and T061's retained release
provenance. No new public download or remote latest-release assertion is made.
The retained ELF still predates the ambiguity fix; passing host tests against
patched source do not qualify that binary. Source manifest version 0.1.0 and
accepted bytes remain unchanged, not a newly approved candidate version.

Sources:
- https://github.com/michaelrolphone-cmyk/RiscRTE-Drivers/tree/4088b6892c2e2654b0342f04a7d191068e4a8e2e/Drivers/ble_sensors
- https://github.com/michaelrolphone-cmyk/RiscRTE-XTEINK-X4-PRO/releases/tag/test-native-provision-0.1.123
- local provenance excerpts and hashes: `source-artifact-linkage.json`.

## What passed

Final test-only commit: all seven suites in `test/run_ble_sensor_test.sh` passed
normally and under ASan/UBSan, 14 successful suite executions. The original six
suites also passed in both configurations before the added coverage.

The new capability fixture exercises the production exported descriptor and
capability calls, using only fake HCI/clock dependencies. Its seven scenario
groups verify:
1. Capability identity/ABI and no implicit radio work on activation.
2. All seven metrics and units, flags and copied-result isolation.
3. Multiple sensors, address-type identity, stable indexes and isolated refresh.
4. Separate observation/measurement ages, encrypted/unsupported/malformed
   refresh clearing and later recovery.
5. Ordered repeated values, bounded future prefix, T061 duplicate-field
   invalidation and recovery through the capability, not just parser helpers.
6. Undecoded GATT metadata, atomic malformed-advertisement rejection, 32-entry
   saturation and bounded enumeration.
7. Scan expiry, explicit cleanup, retained stale results, session reset and stop.

Existing suites additionally cover 50,000 deterministic parser inputs, negative
and maximum values, command/credit timeouts, failed or partial claims, cleanup
retention, backpressure, clock failure, stale tokens, telemetry encoding and all
101 battery percentages through the encoder/parser round trip.

Compiler: GCC 14.2.0. Flags and command receipts are in `qualification.json`.
LeakSanitizer was disabled under the existing ptrace limitation; no leak pass is
claimed. The production source tree is byte-identical to T061's patched base.

Independent review at 09:41 UTC reran all seven suites normally and with
ASan/UBSan (detect_leaks=0), inspected the 171-line fixture, and confirmed the
production diff from ba28f9b is empty. No blocking issue was found.

## Exact limits and remaining work

The target build command was attempted once and stopped before output with
`missing Xtensa compiler; activate ESP-IDF 6.1 (xtensa-esp-elf-gcc), set
NATIVE_DRIVER_CC, or install the legacy ESP32-S3 toolchain`. Exit status: 1.
The log is preserved as `target-build-attempt.log`. No target ELF/firmware,
remote CI, physical radio, flashing, product pin change or hardware test occurred.

Tasker retains source/qualification ownership. Existing canonical target/build
and product owners are not displaced. The next scoped engineering step is an
exact-source, separately versioned target build with import/memory/lifecycle and
product-provider/grant qualification; physical supported-sensor discovery,
coexistence and controller restoration must then be validated under separately
authorized device testing. Publication is held and no approval question was added.

Scanner display and persistent naming remain separate tasks 518d3152 and
2379d8cf. T061's source repair and evidence remain independently owned; its clean
final worktrees were read only. No push, PR, merge, tag, release, catalog change,
unknown delivery replay or external-owner contact took place.

## Reproduce

From the isolated Drivers worktree:

    bash test/run_ble_sensor_test.sh
    ASAN_OPTIONS=detect_leaks=0 SANITIZE=1 bash test/run_ble_sensor_test.sh

`nearby-provider-tests.patch` contains only the new test and runner addition.
`SHA256SUMS` binds this report, the patch, logs and receipt. The isolated worktree
is `/workspace/scratch/c744abbbbd60/nearby-ble-provider/RiscRTE-Drivers`.

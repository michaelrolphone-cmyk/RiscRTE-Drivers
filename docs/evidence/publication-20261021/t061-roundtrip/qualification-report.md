# T061: Watch battery in X4 Quick Actions

## Result

The requested feature is not delivered in the inspected release or current source. The X4 resident Quick Actions sheet renders the local X4 board battery only. BLE telemetry encoding and generic reception interoperate, but no existing contract binds the user's Watch to Quick Actions or establishes when its value is presently acquired. No speculative UI, peer selector, radio schedule, pairing change or production adapter was added.

## Exact requested behavior

User message Sentinel_5ff7f99af3f8819190aee5b0273784a5, 2026-10-11 15:14:31 UTC:

“Add to the queue ahead of app refactors for shared ui components to add watch battery via bluetooth to the quick actions screen so I can see my watches battery level on my x4 (show last acquired value and an idicator if its presently acquired)”

The separate 2026-10-15 T061 comment requires Quick Actions to be framework-owned, not compiled into foreground apps. The selected X4 resident host architecture already satisfies that ownership pattern. The new feature must retain the last received battery value and show a separate present-acquisition indicator; a stale cached value must not masquerade as a live reading.

## Source findings

- Current System Apps: `7270a10ed9cefba6ec1368060196a13bb3370985`.
- Current Drivers base: `a7ae39994603c4545a3c014f09051ecfc12084d2`.
- Current X4 product: `6b7d1abe9b2dcc236f6086d07c0f619244c907cb`.
- System `quick_adapter.inc:572-577` polls `gauge->read` once per second. The gauge comes from `board.battery`; product instance 7 is the local X4 provider.
- `quick_reference.inc:137-141` renders one BATT bar/percentage. Neither current source nor the published System pin consumes `bluetooth.sensors` for Quick Actions.
- `bluetooth.sensors` uses a bounded 15-second passive scan and an exclusive HCI lease. It does not connect, pair, decrypt or use GATT. Activation alone does not claim the radio.
- The scanner identifies an observation by BLE address plus address type. Its persistent aliases in Utilities identify those same observed addresses, not a stable Watch or authenticated owner.
- The telemetry broadcaster transmits BTHome metric/value pairs without source IDs, source labels or a device name. It uses a new random-static address each publishing session. Multiple battery fields can exist; the convenience `battery` member is the first decoded battery field, not a designated Watch battery.
- Measurement age and last-seen age are distinct. A later presence advertisement does not establish that a cached battery measurement is fresh.
- Existing scanner operations obey saved Bluetooth/Airplane settings and explicit Scan intent. Quick Actions opening is not established Scan intent.

Source links:
- [Current Quick Actions read](https://github.com/michaelrolphone-cmyk/RiscRTE-System-Apps/blob/7270a10ed9cefba6ec1368060196a13bb3370985/lib/PortableApps/src/quick_adapter.inc#L572-L577)
- [Published Quick Actions read](https://github.com/michaelrolphone-cmyk/RiscRTE-System-Apps/blob/0651a55513b0aa0dff1b6a9072303def08a97378/lib/PortableApps/src/quick_adapter.inc#L563-L580)
- [Published battery rendering](https://github.com/michaelrolphone-cmyk/RiscRTE-System-Apps/blob/0651a55513b0aa0dff1b6a9072303def08a97378/lib/PortableApps/src/quick_reference.inc#L137-L141)
- [Telemetry privacy/identity contract](https://github.com/michaelrolphone-cmyk/RiscRTE-Drivers/blob/a7ae39994603c4545a3c014f09051ecfc12084d2/sdk/driver/RiscBluetoothTelemetryV1.h)
- [Generic receiver contract](https://github.com/michaelrolphone-cmyk/RiscRTE-Drivers/blob/a7ae39994603c4545a3c014f09051ecfc12084d2/sdk/driver/RiscBluetoothSensorsV1.h)

## Local reusable work

A test-only Drivers commit feeds the actual telemetry encoder into the actual passive scanner parser. It qualifies the shared wire contract without inventing a production selection policy.

- Branch: `tasker/t061-watch-battery`
- Commit: `fe310f01a90a616ce05be02b677debdf3cfcd607`
- Base: `a7ae39994603c4545a3c014f09051ecfc12084d2`
- Changes: add `test/ble_telemetry_sensor_roundtrip_test.c`; include it in the existing `test/run_ble_sensor_test.sh` suite.
- No production files, versions, pins, accepted bytes or manifests changed. No dependency on the separate unpublished T043 test commit.

Coverage: all 101 battery percentages (0 through 100); no source-label/device-name identity on wire; address and address-type separation; presence versus measurement freshness; battery absence in a newer valid measurement; invalid/truncated/encrypted data; two battery fields without assuming which belongs to a user; bounded scan completion.

## Verification

Run from this isolated workspace:

1. `bash RiscRTE-Drivers/test/run_ble_sensor_test.sh`: six suites passed (baseline five also passed before the new test).
2. `ASAN_OPTIONS=detect_leaks=0 SANITIZE=1 bash RiscRTE-Drivers/test/run_ble_sensor_test.sh`: six suites passed with AddressSanitizer and UndefinedBehaviorSanitizer.
3. `python3 RiscRTE-System-Apps/scripts/test_quick_actions.py`: 2 pure-core + 2 session + 104 real-adapter normal/sanitizer/rotation executions passed. This is unchanged existing Quick Actions regression coverage, not the requested remote-battery feature.
4. `git diff --check`: passed.

The first driver sanitizer invocation failed because LeakSanitizer does not work under the executor's ptrace environment. Rerunning with leak detection disabled succeeded; address/undefined checks remained enabled. No leak-check pass is claimed.

No fresh target ELF/product build, current remote CI, UI implementation, physical radio/display/power test, flash, publication, merge or release was performed.

## Decision needed before integration

There is no existing stable Watch binding or acquisition contract to reuse. Two bounded choices are:

1. Explicitly select a currently observed public BLE session in the existing scanner, then explicitly refresh it. Keep the last value when no fresh sample is available and show not-current. A new broadcaster session/address requires explicit reselection. This preserves present advertising privacy and manual radio behavior but is less automatic.
2. Design persistent recognized/paired identity and its acquisition policy. This can survive broadcasting restarts but is broader work and may change privacy/security/consent and radio-lifecycle behavior. It is not implied by the present public broadcast capability.

A design choice also needs to define when a reading counts as currently acquired and what explicit action permits acquisition. No first/nearest/named-device heuristic, background scan interval, stable tracking ID, or secure-pairing claim was invented. The task is incomplete pending that decision; source qualification is not a hardware or end-to-end pass.

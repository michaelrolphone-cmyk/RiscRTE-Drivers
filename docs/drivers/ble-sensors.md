# ble-sensors 0.1.0

Generic experimental `bluetooth.sensors@1` provider. It moves the existing
Utilities BLE Scanner's passive scan into a reusable ABI-v2 ELF. The source was
adapted from Utilities `e6444bb4887d4254a6985eee8e6f9124f1fa7a02`,
`Apps/ble_scan_core.h`; its MIT notice is preserved in the package.

## Ownership and lifecycle

The only dependencies are `bluetooth.hci@1` with the full exclusive-host suffix
and `platform.clock@1`. Activation does no radio I/O. Explicit `open` claims the
same lease used by the existing NimBLE HID provider. Scanning, HID, telemetry
advertising and native radio-IQ ownership cannot run competing controller
owners. This change adds no ATT/GATT, pairing, security-manager or controller
stack. Other boards work only when they provide these exact capability contracts.

A scan is passive, bounded to 15 seconds, and retains at most 32 identities.
Addresses include address type; slots remain stable for a scan. Each `poll`
processes 1..16 events, with one outstanding acknowledged command and a two-second
command/credit timeout. Clock failure and malformed transport bounds fail closed.
`close` restores the prior controller state through the existing lease release.
A failed claim may need cleanup. A failed close/quiesce retains code, token and
dependencies. Callers must poll and close at completion/error; there is no task,
callback or autonomous timer. No app pointer survives a call.

## Readings and trust

Copied result records include raw advertising data, advertised name, RSSI,
16-bit services, manufacturer ID, sample age and supported scalar readings.
[BTHome v2](https://bthome.io/format/) object IDs 01, 02, 03, 04, 05, 0C and 16
map to battery percent, temperature centi-Celsius, humidity centi-percent,
pressure centi-hPa, illumination centi-lux, voltage mV and charging boolean.
Packet ID 00 is skipped. Repeated objects retain their ordered readings.

Malformed known objects discard the whole sample; they cannot leave a plausible
prefix. Unknown future IDs end a supported prefix and mark it partial. Encrypted
or unsupported-version packets clear exposed readings and remain identified as
unreadable. Nothing decrypts or pairs automatically. A broadcast is unverified
input, not proof of ownership, authenticity or suitability for a safety decision.
GATT-only services and other vendor formats are not decoded by this increment.

Result ages are computed in the provider's clock domain when copied, avoiding an
assumption that an app clock has the same epoch. Applications may add their own
elapsed time after copying. Aliases belong to the scanner application; the driver
does not persist remote devices or resolve rotating private addresses.

## Validation

`bash test/run_ble_sensor_test.sh` and `SANITIZE=1 bash test/run_ble_sensor_test.sh`
exercise the production provider and a deterministic 50,000-input parser sweep.
Cases include negative/maximum values, sorted/repeated objects, partial future
objects, truncation, encrypted refresh, table saturation, stale tokens, denied
and partial claims, clock failure, backpressure, retained closure and clean stop.
`python scripts/build_ble_sensors.py` builds the isolated Xtensa ELF and checks
ABI, architecture, entry point and imports. CI runs the same tests.

Physical discovery, RF coexistence, current, power restoration and sensor-device
interoperability remain untested. No Watch profile or frozen firmware payload is
changed by this package.

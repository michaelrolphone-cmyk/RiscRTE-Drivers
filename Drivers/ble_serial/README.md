# BLE serial client 0.1.1

`bluetooth.serial@1` is a reusable, board-independent BLE central/GATT transport.
It uses the established exclusive `bluetooth.hci@1` lease and `platform.clock@1`.
Apache NimBLE 1.9.0 (commit da7e3256da3ba80b232df880f40d8359311cc62e)
implements the real host protocols; the existing bounded HCI port and vendored
NimBLE files are unchanged. It is not an ELM simulator and contains no OBD logic.

The SDK is `sdk/driver/RiscBluetoothSerialV1.h`. Open starts a bounded scan.
Select a copied connectable peer, discover services/characteristics, select a
serial channel, then poll/read/write. READY is published only after the actual
CCC write has succeeded. Only same-service write + notify/indicate combinations
are offered. Recognized FFE0/FFE1, FFF0/FFF1/FFF2 and Nordic UART UUID combinations
are flagged as candidates, not proof of adapter identity. Other discovered
vendor UUIDs require explicit channel selection in the client. The OBD app still
requires a valid ELM initialization and ECU response before showing live data.

Discovery limits are 24 peers, 24 primary services, 64 characteristics and 16
candidate channels. RX has 2048 bytes; overflow invalidates the stream. Writes
are at most 20 bytes (MTU23). Acknowledged writes have backpressure and a deadline;
write-without-response uses NimBLE/controller credits. Notifications and
indications are filtered by connection and selected characteristic. Disconnect
clears received bytes. No callback or parser pointer crosses into an app.

**This version supports unpaired/open BLE serial channels only.** Pairing,
bonding, Classic Bluetooth SPP, multiple simultaneous connections and background
radio coexistence are not implemented. Authentication-required subscriptions or
writes fail explicitly. ESP32-S3 cannot implement a Classic-only adapter link.
HID/Scanner/SDR must relinquish their HCI lease first. Closing stops the host,
then returns the native lease; uncertain lease cleanup is retriable and prevents
unload. An unproven host stop requires provider reload rather than reusing it.

## Build and test

```
python scripts/test_ble_serial.py
python scripts/test_ble_serial.py --sanitize
NATIVE_DRIVER_CC=/path/to/xtensa-esp32s3-elf-gcc python scripts/build_ble_serial.py
```

Python test fixtures require `cryptography`. Eleven cases exercise real NimBLE
through an independent HCI controller/ATT peripheral: FFF0 and FFE0 notifications,
128-bit Nordic UART indications, write modes/credits, scan/connect/discovery,
fragmented RX, overflow, disconnect, subscription authentication refusal, write
acknowledgement timeout, non-vendor rejection, extended-properties TX with
indication-only RX, uncertain close and ten reopen
cycles. Host ASan/UBSan runs disable leak reporting for the ctypes interpreter.
No RF adapter, vehicle, board execution or physical timing is claimed.

Primary sources: Espressif ESP32-S3 BLE overview
(https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/ble/overview.html),
and the pinned Apache NimBLE source and license in `vendor/nimble/`.

## 0.1.1 readiness correction

RX subscription mode is separate from TX characteristic properties. An extended-
properties transmitter can no longer turn an indication subscription into a
notification subscription. See [audit and reproduction](AUDIT-20261012.md).

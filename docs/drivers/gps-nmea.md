# gps-nmea

## Purpose and scope

`gps-nmea` is a legacy driver-ABI-v1 GNSS provider that parses NMEA 0183 GGA and RMC sentences from a runtime-supplied serial endpoint. It is allocation-free, performs no direct board or UART access, and exposes `position.gnss@1` through the driver capability interface.

## Package identity

- Driver ID: `gps-nmea`
- Version: `1.0.0`
- Driver ABI: `1`
- Architecture: `xtensa-esp32s3`
- Output file: `driver.elf`
- Provides: `position.gnss@1`
- Requires: `kernel.serial@1`, `kernel.power@1`, `kernel.clock@1`
- Upstream source tree: `Drivers/gps_nmea`
- Upstream publication status: source-only in the inspected release index; there is no canonical released ELF hash/size for this driver

## Source and build files

- `Drivers/gps_nmea/driver.c`
- `Drivers/gps_nmea/manifest.json`
- `sdk/driver/T5DriverApi.h`
- `sdk/driver/T5GnssProvider.h`
- `sdk/driver/T5GpsApi.h`
- `scripts/build_gps_nmea.py`

The standalone build uses the Xtensa ESP32-S3 GCC toolchain, PIC/shared-object flags, SysV ELF hashing, relocation normalization, exported-symbol validation, and ELF32/Xtensa validation. Because the upstream driver is source-only, the build records the produced size and SHA-256 but cannot claim byte-for-byte parity with a published artifact.

## Driver ABI and exported symbol

The only intended public ELF symbol is `t5_driver_get(uint32_t requested_abi)`. It returns the static `t5_driver_v1` descriptor only when the requested ABI is `T5_DRIVER_ABI_VERSION` (1), otherwise null.

The descriptor identifies `gps-nmea`, advertises `position.gnss` API 1, and provides `start` and `stop`. The capability object is a `t5_gnss_api_v1` containing `api_version`, `struct_size`, and a `read` callback.

## Host interface and resource ownership

`start` requires a non-null `t5_kernel_io_v1` with API version 1, a structure at least as large as the compiled type, and all of these callbacks:

- `millis`
- `sleep_ms`
- `power_acquire`
- `power_release`
- `serial_open`
- `serial_close`
- `serial_read`

The runtime owns the underlying hardware operations. The driver retains the host pointer while active, acquires power once at startup, opens the serial endpoint at one selected baud, and releases/clears those resources in `stop`.

Calling `start` while already active returns true without reacquiring resources. If power acquisition or initial serial opening fails, startup unwinds and returns false.

## Serial probing and work budget

The supported baud candidates are exactly 9,600 and 38,400 baud.

Startup waits 20 ms after acquiring power, then begins at 9,600 baud. `begin_baud` always closes the current serial endpoint, waits 10 ms, clears sentence-collection state, records the selected baud and start timestamp, and opens the endpoint at the selected rate.

Until any checksum-valid sentence has been accepted, if 1,600 ms elapse at the current baud the driver toggles to the other candidate. If reopening at the alternate baud fails, the driver stops and `read` returns false.

Each `read` call performs at most eight `serial_read` operations into a 128-byte stack buffer. A full read can therefore service at most 1,024 bytes per call. If the host reports a count larger than the supplied 128-byte capacity, the driver treats that as a host contract violation, stops, and returns false.

## Sentence buffering

The parser uses one static 128-byte sentence buffer. A dollar sign starts a new sentence and resets the buffered length. Carriage return or line feed terminates the current sentence and triggers parsing.

Bytes received while not collecting are ignored. If a sentence would exceed the buffer, collection is abandoned until the next dollar sign. `chars_processed` increments for every byte fed to the parser, including framing and ignored bytes.

## Checksum handling and receiver detection

Parsing requires an asterisk followed by exactly two hexadecimal checksum characters at the end of the buffered sentence. The checksum is the XOR of all characters from the first byte after the dollar-sign framing event through the byte immediately before the asterisk; the stored sentence itself does not contain the leading dollar sign.

A checksum-valid sentence sets the internal `locked` flag and `receiver_detected = 1` before sentence-type validation. Therefore an otherwise unsupported checksum-valid NMEA sentence is sufficient to establish receiver detection and stop baud alternation.

## Supported NMEA records

Only talker-prefixed five-character sentence identifiers ending in `GGA` or `RMC` are parsed. The parser stores at most 20 comma-separated fields.

### GGA

GGA requires at least 11 fields.

- Fix quality must parse numerically and be between 1 and 8 inclusive for the record to be considered a valid fix.
- Satellites are parsed from field 7, clamped to 255, and stored when non-negative.
- HDOP is parsed from field 8 when non-negative.
- Altitude is parsed from field 9 only when field 10 is exactly `M`.

### RMC

RMC requires at least 10 fields.

- Status field 2 must equal `A` for a valid fix.
- Speed in knots is converted to kilometres per hour using a factor of 1.852.
- Course is accepted only when non-negative and below 360 degrees.

For either supported record, an invalid-fix status clears `have_fix` immediately.

## Coordinate parsing and validation

Latitude and longitude use NMEA degrees/minutes format. Numeric parsing accepts an optional sign and at most one decimal point, rejects non-digits, and applies a magnitude guard at 1e12.

Latitude requires a one-character `N` or `S` hemisphere and longitude requires `E` or `W`. Minutes must be below 60; latitude degrees may not exceed 90 and longitude degrees may not exceed 180. South and west coordinates are negated.

If either coordinate fails validation, the record does not replace the current stored coordinates.

## Fix state and aging

A valid parsed fix stores latitude/longitude and records `fix_at = millis()`. `read` copies the current fix structure to the caller and derives:

- `age_ms = now - fix_at` when a fix has been observed, otherwise `UINT32_MAX`
- `fix_valid = true` only while the stored fix is at most 5,000 ms old
- status `T5_GPS_STATUS_FIX` for a valid non-stale fix, otherwise `T5_GPS_STATUS_SEARCHING`
- latitude and longitude are zeroed in the returned state whenever `fix_valid` is false

When the driver is not started, `read` returns a zeroed state with status `T5_GPS_STATUS_OFF`.

## Failure behavior

Confirmed failure paths include an invalid host API, missing mandatory host callbacks, failure to acquire power, failure to open the serial endpoint, a host `serial_read` count larger than the provided capacity, failure to reopen during baud alternation, and a null output-state pointer.

Malformed, unsupported, checksum-invalid, overlong, or coordinate-invalid NMEA input is ignored without stopping the driver.

## Concurrency and lifecycle assumptions

The ABI comment for `t5_kernel_io_v1` states that calls are synchronous on the owning task. The implementation uses file-static mutable state and contains no locking. The source therefore establishes single-owner/synchronous use, not thread-safe concurrent entry.

`stop` closes serial and releases power when active, nulls the host pointer, and clears fix/lock/collection flags. It does not zero the entire stored fix structure, but subsequent reads while stopped return a freshly zeroed OFF state.

## Validation status

The migrated source and ABI headers are exact copies of the inspected upstream files at T5S3-Reader commit `1ed24d19d226658d375163eab6062faa74a1519e`. The standalone build script validates manifest identity, the sole public `t5_driver_get` export, ELF32 little-endian shared-object type, and Xtensa machine type 94. The integrated CI build is the cross-compilation validation point.

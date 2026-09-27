# usb-cdc-acm

## Purpose and scope

`usb-cdc-acm` is a legacy driver-ABI-v1 USB CDC ACM class/protocol provider. It does not own USB hardware, enumerate devices, submit transfers, allocate memory, or retain device handles. Its implementation provides three deterministic helpers over caller-owned data: configuration-descriptor probing, CDC SET_LINE_CODING payload encoding, and SET_CONTROL_LINE_STATE value encoding.

## Package identity

- Driver ID: `usb-cdc-acm`
- Version: `0.1.0`
- Driver ABI: `1`
- Architecture: `xtensa-esp32s3`
- Output file: `driver.elf`
- Provides: `usb.class.cdc_acm@1`
- Manifest requires: `kernel.usb.host@1`
- Upstream source tree: `Drivers/usb_cdc`
- Upstream source tree SHA: `61609db3a1cbf7a29c1e447fea8d4cfb6417224a`
- Upstream publication status: source-only in the inspected release index; there is no canonical released ELF size or SHA-256 for byte-parity comparison

## Source, ABI, build, and test files

- `Drivers/usb_cdc/driver.c`
- `Drivers/usb_cdc/manifest.json`
- `sdk/driver/T5DriverApi.h`
- `sdk/driver/T5UsbClassDriver.h`
- `scripts/build_usb_cdc_acm.py`
- `test/drivers/usb_cdc_test.c`

The standalone build uses the Xtensa ESP32-S3 GCC toolchain, PIC/shared-object flags, SysV ELF hashing, relocation normalization, sole-export validation, and ELF32/Xtensa validation. Because the upstream package is source-only, the build records its output size and SHA-256 but cannot claim byte-for-byte equality with a canonical published artifact.

## Driver ABI and exported symbol

The only intended public ELF function is `t5_driver_get(uint32_t abi)`. It returns the static `t5_driver_v1` descriptor when `abi == T5_DRIVER_ABI_VERSION` (1), otherwise null.

The descriptor identifies `usb-cdc-acm`, advertises capability `usb.class.cdc_acm` API 1, points at a static `t5_usb_cdc_class_api_v1`, and provides `start` and `stop`.

The capability table contains:

- `probe(configuration, length, vid, pid, out)`
- `line_coding(baud, data_bits, parity, stop_bits, payload)`
- `control_lines(dtr, rts)`

## Runtime dependency and hardware ownership

The manifest declares `kernel.usb.host@1` as a required capability. The current ABI-v1 `start(const t5_kernel_io_v1 *kernel)` implementation does not dereference or retain the supplied host pointer and always returns true. This is an implementation fact, not a statement that the package can be used outside the runtime dependency policy expressed by its manifest.

The source imports no ESP-IDF USB objects and contains no USB transfer, interface-claim, VBUS, task, or heap operations. Its comments explicitly place enumeration, device leases, interface claims, and actual USB operations in the privileged runtime. The class provider consumes a bounded copy of a configuration descriptor and returns plain-data binding information.

## CDC binding structure

`t5_usb_cdc_binding_v1` contains:

- control interface number
- data interface number
- data alternate setting
- bulk IN endpoint address
- bulk OUT endpoint address
- bulk IN maximum packet size
- bulk OUT maximum packet size

The probe initializes candidate interface numbers to `0xff`, endpoint addresses to zero, and both maximum packet sizes to 64 before descriptor scanning. A binding is copied to the caller only after the entire probe succeeds.

## Configuration descriptor validation

`probe` rejects a null configuration pointer, null output pointer, input smaller than 9 bytes, input larger than 4,096 bytes, a first descriptor shorter than 9 bytes, or a first descriptor whose type is not configuration descriptor type 2.

The configuration descriptor's little-endian `wTotalLength` must be at least 9 and no larger than the caller-supplied buffer length. Parsing is bounded by that total rather than by bytes beyond it.

Every descriptor encountered must have at least its two-byte length/type prefix available. A descriptor length below 2 or extending past `wTotalLength` fails the probe.

VID and PID parameters are accepted by the ABI but are deliberately unused by the current implementation; there is no vendor/product allowlist in this driver.

## Interface matching

Interface descriptors are recognized only when descriptor type is 4 and length is at least 9.

The first interface with class `0x02` becomes the control interface.

The first interface with class `0x0a` becomes the data interface, and its alternate setting is retained. Once selected, later data-class interfaces do not replace it.

The current implementation does not parse CDC union, call-management, ACM, or functional descriptors to establish an explicit control/data relationship. It matches the first communications-class interface and first data-class interface present in the configuration.

## Bulk endpoint matching

Endpoint descriptors are considered only when descriptor type is 5, length is at least 7, and they occur while parsing the selected data interface and selected alternate setting.

Only endpoints whose transfer-type bits indicate bulk (`bmAttributes & 3 == 2`) are accepted. Maximum packet size must be greater than zero and at most 512 bytes.

An endpoint address with bit 7 set is treated as IN; otherwise it is OUT. A second qualifying IN endpoint or second qualifying OUT endpoint for the selected data interface makes the probe fail rather than silently choosing one.

A successful binding requires a discovered control interface, data interface, nonzero IN endpoint, nonzero OUT endpoint, and both direction-presence flags. On any failure the caller's output binding is not modified.

## Line coding

`line_coding` produces the seven-byte payload used by CDC SET_LINE_CODING.

Accepted values are exactly constrained by source to:

- baud: 300 through 3,000,000 inclusive
- data bits: 5 through 8 inclusive
- parity: numeric values 0 through 4 inclusive
- stop bits argument: 1 or 2

A null output payload is rejected.

Baud is written little-endian to bytes 0 through 3. A stop-bits argument of 1 writes CDC value 0; an argument of 2 writes CDC value 2. Parity is written unchanged to byte 5 and data bits to byte 6.

The implementation does not accept or emit the CDC 1.5-stop-bit encoding.

## Control-line state

`control_lines` maps DTR to bit 0 and RTS to bit 1:

- neither asserted -> 0
- DTR only -> 1
- RTS only -> 2
- DTR and RTS -> 3

The interface number for the eventual class request is not part of this helper's return value; the ABI comment states that interface selection comes from the binding.

## Lifecycle, concurrency, and resources

`start` is stateless and always succeeds. `stop` is empty. There is no mutable file-static runtime state, no allocation, no acquired hardware resource, and no cleanup sequence in this implementation.

All three capability operations are synchronous pure computations over caller-provided values except for writing their caller-owned output buffers.

## Failure behavior

Probe failure is returned as false for malformed or unsupported descriptor layouts, invalid bounds, missing required CDC interfaces/endpoints, duplicate qualifying bulk endpoints, or invalid endpoint packet sizes.

Line-coding failure is returned as false for a null output, baud outside the supported range, data-bit width outside 5..8, parity above 4, or stop-bit value other than 1 or 2.

There is no error code namespace, retry state, timeout, buffering, asynchronous callback, or recovery state in this provider.

## Validation

The migrated `driver.c`, `manifest.json`, `T5DriverApi.h`, and `T5UsbClassDriver.h` are grounded in T5S3-Reader master commit `99abac00a0ec49e16da0110833f1f51e8d23c6d0`. The driver blob is `33c67a4ed78926b669abfefacd6d1f0cd7fd3d7c`, manifest blob `905576fdcc7e8f439b12cddf6995f8af46e3c885`, USB class ABI header blob `d1b9a8fe2fb61c624645f3122d996d33e5d30c3e`, and the already-migrated driver ABI header matches upstream blob `ff145230b225231cbb53afc597e0d058d606be75`.

A host behavioral test compiled with `cc -std=c11 -Wall -Wextra -Werror -I sdk/driver` and passed with `USB CDC ACM descriptor/protocol provider: PASS`. The test covers ABI lookup/identity, a valid CDC control/data/bulk descriptor topology, non-mutation on a failing total-length check, malformed descriptor rejection, invalid endpoint-size rejection, line-coding boundaries and encoding, and all DTR/RTS combinations.

No local Xtensa ESP32-S3 cross-compiler is available in the automation environment, so independent ELF generation/export validation remains the integrated-CI validation point.

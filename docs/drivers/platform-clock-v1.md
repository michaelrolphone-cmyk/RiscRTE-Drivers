# platform-clock-v1

## Purpose

`platform-clock-v1` is the RiscRTE provider for the `platform.clock` capability. The implementation supplies a monotonic millisecond clock and scheduler-yielding millisecond sleep service through the generic provider ABI. It does not implement a peripheral-specific timer driver.

## Package identity

- Package/driver ID: `platform-clock-v1`
- Version: `0.1.0`
- Driver ABI: `2`
- Architecture: `xtensa-esp32s3`
- ELF filename: `driver.elf`
- Provided capability: `platform.clock`
- Capability API: `1`
- Required capabilities: none
- Source manifest status: `experimental-unpublished`

Source files in this repository:

- `Drivers/platform_clock_v1/driver.c`
- `Drivers/platform_clock_v1/manifest.json`
- `sdk/driver/RiscPlatformClockV1.h`
- `sdk/driver/RiscProviderV2.h`
- `scripts/build_platform_clock_v1.py`

## Provider ABI and exported symbol

The ELF exports only:

`t5_driver_get(uint32_t abi)`

The function returns the static `risc_driver_v2` descriptor only when the requested ABI is `RISC_PROVIDER_DRIVER_ABI_V2` (2); otherwise it returns `NULL`.

The driver descriptor publishes:

- driver ID `platform-clock-v1`
- capability ID `platform.clock`
- capability API `RISC_PLATFORM_CLOCK_API_V1`
- a `risc_platform_clock_api_v1` interface table
- `start`, `stop`, and `quiesce` lifecycle callbacks

## Capability interface

`risc_platform_clock_api_v1` contains:

- `api_version`
- `struct_size`
- `context`
- `monotonic_ms(void *context)`
- `sleep_ms(void *context, uint32_t milliseconds)`

The provider sets `context` to `NULL`.

### monotonic_ms

`monotonic_ms` calls `clock_gettime(CLOCK_MONOTONIC, ...)` and converts the result to whole milliseconds.

It returns `UINT64_MAX` as a fail-closed sentinel when:

- the provider is not running;
- `clock_gettime` fails;
- `tv_sec` is negative;
- `tv_nsec` is outside `0..999999999`;
- converting seconds to milliseconds would overflow `uint64_t`.

The implementation truncates nanoseconds to milliseconds.

### sleep_ms

`sleep_ms` sleeps through `usleep` rather than busy-waiting. Requested delays are split into chunks of at most 999 ms. The loop exits if the provider stops while a multi-chunk sleep is in progress.

## Lifecycle

### start

`start` rejects activation when:

- the provider is already running;
- any dependencies were supplied;
- the monotonic clock cannot be sampled successfully;
- the returned `timespec` contains invalid fields.

On success it sets the internal `running` flag.

### quiesce

`quiesce` returns `true`. The source comments require the generic provider executor to stop new entries and drain synchronous calls before unload; the function itself does not perform that executor synchronization.

### stop

`stop` clears the internal `running` flag.

## Dependencies and imports

The manifest declares no provider dependencies. The implementation relies on libc/runtime imports used by the ELF loader, including:

- `clock_gettime`
- `usleep`

No hardware-specific device capability is required.

## Build and validation

`scripts/build_platform_clock_v1.py` builds the driver with the Xtensa ESP32-S3 GCC toolchain as a PIC shared ELF using:

- C11
- `-D_DEFAULT_SOURCE`
- `-Os`
- `-fPIC`
- `-mtext-section-literals`
- `-mlongcalls`
- hidden default visibility
- `-nostdlib`
- `-nostartfiles`
- SysV ELF hash style

The build script normalizes Xtensa relocations, verifies that the only exported function is `t5_driver_get`, verifies ELF32/Xtensa/shared-object identity, and writes size/SHA-256 metadata into the staged manifest.

## Published artifact parity

The T5S3-Reader release index currently records:

- Release tag: `driver-platform-clock-v1-v0.1.0`
- Asset: `platform-clock-v1--driver.elf`
- Published size: `29,544` bytes
- Published SHA-256: `a5231d3f21931261b10ae82a3e16248d60a678aa6ebc67b61cb702491d6a0ece`

The standalone RiscRTE-Drivers build has reproduced that published ELF size and SHA-256.

The published package metadata also records `.package.json`, `provider-abi.v1`, and `privileged-imports.v1` package files. Those are release-package metadata artifacts rather than additional implementation source files.

## Confirmed limits and implementation status

- The API exposes only monotonic milliseconds and millisecond sleep.
- There is no wall-clock/calendar API in this provider.
- Sleep granularity is expressed in milliseconds and implemented through repeated sub-second `usleep` calls.
- The driver has one process-global `running` state flag.
- The source does not establish asynchronous callbacks, hardware IRQ ownership, DMA, or device discovery for this provider.
- The manifest labels the source package `experimental-unpublished`; separately, T5S3-Reader has published a release artifact for version 0.1.0.

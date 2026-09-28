# usb-controller-esp32s3

## Identity and migration status

`usb-controller-esp32s3` is the ESP32-S3 physical USB controller provider. Current upstream package metadata is version **0.1.18**, driver ABI **2**, architecture **xtensa-esp32s3**, executable `driver.elf`, requiring `board.power.vbus@1` and providing `usb.controller@1`. The exact current upstream driver directory is Git tree `16647df4a2f25a5d07f267a51b4497f1185d12fc`. Its manifest status is `experimental-hardware-port-not-yet-linkable`.

This page documents verified source behavior. The exact upstream source tree is already integrated here. Migration is not complete until the staged repository-independent PIC ESP-IDF build/audit path is integrated and reproduces the canonical published ELF.

## Upstream source tree

The complete upstream directory contains eleven files:

- `driver.cpp`
- `driver_base.cpp`
- `PendingInterrupt.h`
- `HostStartup.h`
- `PhyRoute.h`
- `RoleSwitch.h`
- `StartupDiagnostic.h`
- `EnumerationDiagnostic.h`
- `phy_gpio.c`
- `exports.map`
- `manifest.json`

The required ABI headers already present in RiscRTE-Drivers — `RiscProviderV2.h`, `RiscUsbControllerV1.h`, `RiscUsbInterruptV1.h`, `RiscUsbDiscoveryDiagnosticsV1.h`, and `RiscUsbVbusV1.h` — match current upstream.

## Capability and root interface

The intended sole exported function is `t5_driver_get`, enforced by `exports.map`. It returns an ABI-v2 root only for `RISC_PROVIDER_DRIVER_ABI_V2`. The root is a `risc_driver_diagnostics_v2` identifying driver `usb-controller-esp32s3`, capability `usb.controller`, API 1, with start/stop/quiesce plus a bounded startup-error callback.

The capability object is a prefix-compatible `risc_usb_controller_diagnostics_v1`. The base controller API exposes ordered attach/detach events, active configuration descriptor snapshots with VID/PID, interface/alternate claims and releases, control transfers, bulk IN/OUT, and quiescence. The append-only interrupt extension adds `interrupt_read`; the diagnostics suffix adds a read-only enumeration diagnostic string.

## State and limits

The implementation uses fixed storage: at most `RISC_USB_HOST_MAX_DEVICES` devices (currently 8), `RISC_USB_HOST_MAX_CLAIMS` claims (currently 16), and a 16-entry event queue. Device and claim IDs are monotonically generated 64-bit tokens; overflow faults the provider rather than reusing an ID.

The shared control/bulk DMA allocation is `RISC_USB_CONFIG_LIMIT + 8` bytes. Active configuration descriptors must be at least 9 bytes and no larger than `RISC_USB_CONFIG_LIMIT`. Bulk endpoint max-packet size must be nonzero and no larger than 512 bytes. Control and bulk payload lengths are bounded by `RISC_USB_CONFIG_LIMIT`.

Interrupt reads require an IN endpoint on the claimed interface/alternate and a packet size no larger than `RISC_USB_HID_MAX_REPORT`. The controller keeps one asynchronous interrupt DMA object per used claim slot. `interrupt_read` rejects zero timeouts and values over 100 ms; zero means no completed report is ready and does not cancel the armed transfer.

## Startup and physical ownership

The provider requires exactly one `board.power.vbus@1` dependency and requires its input-monitor extension. USB host hardware ownership stays inside the ELF.

When host mode is safe, source code performs this order: capture the prior internal PHY route; create the ESP32-S3 internal OTG PHY in host mode; force host disconnect; install the IDF USB host with PHY setup skipped; register the asynchronous host client; allocate the shared transfer; delay for the role/pull-down handoff; request a **500 mA** VBUS lease; then allow the PHY connection. This explicitly prepares the host before powering an attached receiver.

ESP-IDF client callbacks enqueue NEW_DEV and DEV_GONE events. Attach handling opens the IDF device, reads VID/PID, and assigns a generation-qualified token. A full event queue faults the provider. Interface claims reject duplicate claims of the same physical interface.

## Transfer behavior

Control transfers build the setup packet in provider-owned DMA. Bulk transfers verify the endpoint against the active descriptor of the claimed interface and alternate setting. Bulk-IN rounds DMA capacity to the endpoint packet size while still rejecting a result longer than the caller request.

A software timeout never frees DMA still owned by IDF. Bulk timeout recovery halts and flushes the endpoint and pumps callbacks until ownership returns. Interrupt completion copies data to caller memory and immediately re-arms controller-owned IN DMA. STALL completion clears the endpoint before reuse. Interface release and quiescence drain matching interrupt DMA first.

## Role switching and cleanup

`RoleSwitch.h` implements Off, Sense, Host, Cleanup and Failed states. External input blocks host startup. `RISC_USB_POWER_SETTLING` is tolerated for a bounded 10-second observation window. Unknown input eventually fails closed. Empty-host source-off probing uses 2 seconds on boards declaring `RISC_USB_POWER_IDLE_PROBE_REQUIRED`, otherwise 500 ms. Cleanup retries after 250 ms, and three failed host starts become a permanent failed state.

Quiescence is fail-closed: it drains DMA, requires interface claims gone, closes device handles, frees the shared transfer, deregisters the client, observes IDF no-client/all-free conditions, uninstalls the host, deletes the PHY, releases VBUS, verifies the power state is not unknown/unsafe, and only then restores the previous USB PHY route. Any failed cleanup retains ownership for retry.

The source comments require provider calls and IDF callbacks to be serialized on one executor. No arbitrary concurrent-call guarantee is established.

## PHY implementation

`phy_gpio.c` implements only the ESP32-S3 D+/D- drive-capability operation needed by the pinned IDF USB PHY. It rejects non-USB pins and invalid drive strengths and writes through the ESP32-S3 GPIO low-level API. The source explicitly avoids importing the firmware's full GPIO ISR/service state.

## Upstream build and audit model

Upstream `scripts/probe_usb_controller_esp32s3.py` derives the target C/C++ flags from the `t5s3-pro` PlatformIO compilation database, rebuilds USB-owned code from pinned **ESP-IDF v4.4.7** as PIC, supplies ESP32-S3 MMIO symbols from the pinned SoC linker definitions, links with `exports.map`, and rejects unresolved USB-internal symbols and unexpected dynamic exports.

Upstream `scripts/audit_usb_controller_elf.py` checks ELF32 little-endian Xtensa ET_DYN identity, relocations, mapped relocation targets, text relocations, exported symbols, firmware/USB import leakage, and the scoped privileged-import contract. The script explicitly treats signed privileged-loader admission, firmware strong-symbol integration and physical-board validation as separate gates.

A repository-independent RiscRTE-Drivers build harness has been derived from those upstream scripts and is preserved in the migration staging area, but it is not yet integrated into this repository. Until that harness is committed and executed in CI, this documentation does not claim canonical build parity.

## Published package metadata

The upstream release tag is `driver-usb-controller-esp32s3-v0.1.18`.

- `usb-controller-esp32s3--driver.elf`: **783,576 bytes**, SHA-256 `f67064a9678a7b048e40cbf411d46653b69006aec07b9f2c95428597cc706e0e`.
- `usb-controller-esp32s3--package.json`: 644 bytes, SHA-256 `f5dbde8f5f9871d182455754edd77e87699f3662623693eeba032bad4e7540f4`.
- `usb-controller-esp32s3--provider-abi.v1`: 43 bytes, SHA-256 `45267b2e246bdb6a0ff9639d3fed88e0be35f54841d191273a6d96312bc2796e`.
- `usb-controller-esp32s3--privileged-imports.v1`: 801 bytes, SHA-256 `c4afa934bdd799046a244e94f82c2d202f72f7010c419c86c6f62095ded168fe`.

Only release metadata for the privileged-import sidecar was inspected here; its individual symbol list is therefore not restated.

## Verified migration evidence and remaining gap

The current upstream directory was enumerated through GitHub and independently verified to hash to tree `16647df4a2f25a5d07f267a51b4497f1185d12fc`. Upstream changes from the prior destination baseline through current master do not touch driver or driver-ABI paths. The source/API behavior above is grounded in the current driver files and ABI headers.

Still pending: integration and CI execution of the staged repository-independent PIC IDF build/audit automation, canonical 783,576-byte ELF reproduction, and migration-complete metadata. The exact source tree is already present. Until those remaining gates pass, `migrated` must remain false.

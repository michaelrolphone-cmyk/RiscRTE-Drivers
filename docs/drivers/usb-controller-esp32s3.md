# usb-controller-esp32s3

## Identity and scope

`usb-controller-esp32s3` is the ESP32-S3 physical USB controller provider. Current upstream source and release metadata are version **0.1.19**, driver ABI **2**, architecture **xtensa-esp32s3**, executable `driver.elf`. It requires `board.power.vbus@1` and provides `usb.controller@1`.

Current upstream source tree: `15c27125c163a50e1d41d7e9b15194ea03680488`.

The provider owns the ESP32-S3 OTG PHY/host-library lifecycle, device/interface claims, control/bulk/interrupt transfer plumbing, host-role switching, startup/enumeration diagnostics, and the transition between boot USB serial and host operation. Version 0.1.19 adds a size-checked optional path for host data while VBUS is independently supplied.

## Release package

Upstream release tag: `driver-usb-controller-esp32s3-v0.1.19`.

| File | Size | SHA-256 |
| --- | ---: | --- |
| `.package.json` | 644 | `6e6c687fac6c5903921aa8b026ac55d6bfeba20c1939fafded7a610b93000205` |
| `driver.elf` | 789,504 | `18c95f4264dfff2b21af13b0f0366327be896c75a0ae2d4fc1c9b0a220424da6` |
| `provider-abi.v1` | 43 | `45267b2e246bdb6a0ff9639d3fed88e0be35f54841d191273a6d96312bc2796e` |
| `privileged-imports.v1` | 801 | `c4afa934bdd799046a244e94f82c2d202f72f7010c419c86c6f62095ded168fe` |

The release metadata establishes the privileged-import sidecar size/hash. This page does not infer individual imports not inspected from the sidecar itself.

## Source files and ABI inputs

The current driver directory contains:

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

Relevant provider headers include `RiscProviderV2.h`, `RiscUsbControllerV1.h`, `RiscUsbInterruptV1.h`, `RiscUsbDiscoveryDiagnosticsV1.h`, and `RiscUsbVbusV1.h`.

Version 0.1.19 changes `HostStartup.h`, `RoleSwitch.h`, `driver_base.cpp`, and the manifest. The external-power ABI extension is defined in the synchronized `RiscUsbVbusV1.h`.

## Exported root and capability

`exports.map` restricts the public entry point to `t5_driver_get`. The driver returns an ABI-v2 diagnostics root only when called with `RISC_PROVIDER_DRIVER_ABI_V2`.

The root identifies `usb-controller-esp32s3`, capability `usb.controller`, API 1, and supplies start/stop/quiesce plus bounded startup-error reporting. The capability object provides device events/configuration, interface claim/release, control transfer, bulk read/write, and quiescence operations. The current controller also supplies the interrupt and discovery-diagnostic extensions used by upper USB providers.

## Required power provider

`start` requires exactly one `board.power.vbus@1` dependency. The dependency must be large enough for the input-monitor extension and provide source acquire/release/quiesce plus `input_status`.

Version 0.1.19 detects the optional `risc_usb_vbus_external_api_v1` suffix only when:

- the base `struct_size` is large enough;
- the monitor flags contain `RISC_USB_POWER_EXTERNAL_HOST_SUPPORTED`;
- `acquire_external_host` and `external_host_valid` are non-null.

Older providers retain their previous behavior and external input keeps the controller parked.

## Host startup ordering

The production startup sequence deliberately obtains host ownership before any power-side effect:

1. Capture the existing ESP32-S3 USB PHY route.
2. Create the internal OTG PHY in host mode.
3. Force the host receive detector disconnected.
4. Install the IDF USB host with PHY setup skipped.
5. Register the asynchronous host client.
6. Allocate shared control/bulk DMA workspace.
7. Delay for the bounded role/pull-down handoff.
8. Acquire a power lease.
9. Allow host connection only after a nonzero lease is returned.

`start_host_controller` now accepts an optional acquisition callback at step 8. The normal path calls `power->acquire_host(..., 500, ...)`. The externally powered path calls the provider's `acquire_external_host` at the same ordered boundary. The numeric 500 argument is the requested milliamp budget, not a timeout.

A failed acquisition can leave provider-owned electrical state pinned even when the caller receives no usable token. The controller cleanup path therefore continues to inspect power status before exposing the boot PHY route.

## External-VBUS host behavior

Version 0.1.19 extends `RoleSwitch` and `RolePort` for a provider that explicitly supports externally powered host data operation.

When the power monitor reports EXTERNAL and the extension is available, the controller performs a bounded passive host trial rather than enabling source power. A successful trial uses the same PHY/client/DMA startup order but obtains an external-host lease. The role reports `USB HOST; EXTERNAL VBUS; BOOST OFF`.

The controller records an external trial for the current incoming-power session. The implementation allows at most **three** external starts without an observed input-power removal. Absence resets the external-attempt state.

An empty externally powered host is parked after the normal empty-host interval; for the T5S3 power provider that interval is **2 seconds** because `RISC_USB_POWER_IDLE_PROBE_REQUIRED` is set. After parking, the boot USB serial route remains available rather than repeatedly probing a connected computer.

Incoming voltage alone is not treated as proof of Qi versus a computer. The source therefore makes no such classification.

## Live power monitoring and mode changes

While in Host state with an external-capable power provider, `RoleSwitch` checks the power condition every **500 ms**.

For an external-host lease it calls `external_host_valid`. For a battery-sourced session it compares the provider's input status with SOURCE. If the current power mode becomes invalid or changes, the controller:

1. marks power lost/changed;
2. forces host receive disconnect through the PHY;
3. waits while attachment/events/claims keep the controller busy;
4. parks/quiesces the current host;
5. returns to Sense before choosing a new power mode.

This sequence prevents a power transition from bypassing outstanding USB ownership. A failed park enters Cleanup and retains ownership for retry.

## Role-state behavior

`RoleSwitch` uses Off, Sense, Host, Cleanup, and Failed states.

- SETTLING is tolerated for a bounded overall observation period instead of counting as an immediate read failure.
- UNKNOWN fails closed after bounded retries.
- ABSENT must be observed/debounced before a battery-sourced host is started.
- EXTERNAL on a legacy provider keeps the host parked.
- EXTERNAL on a supporting provider permits only the bounded passive host trial described above.
- Three failed ordinary host starts enter Failed.
- Cleanup retries after its bounded retry interval and does not declare ownership clean before the actual teardown succeeds.
- Tick arithmetic is written to tolerate unsigned rollover.

## Device, claim, and transfer ownership

IDF client callbacks enqueue new-device and device-gone events. Device tokens and interface claims are generation qualified so stale handles are rejected.

Control transfers use provider-owned DMA for setup/data. Bulk transfers validate the endpoint against the active descriptor/alternate setting. Bulk-IN capacity is rounded to endpoint packet size while a returned result larger than the caller request is rejected.

Interrupt-IN transfers use controller-owned storage and re-arm after completion. STALL handling clears the endpoint before reuse. A software timeout does not free DMA still owned by IDF; cleanup pumps callbacks and drains ownership before release.

The provider's source comments require serialized provider calls and IDF callbacks on the intended executor. It does not claim arbitrary concurrent-call safety.

## Quiescence and resource ownership

Quiescence is fail-closed. It drains in-flight DMA, rejects outstanding claims, closes device handles, frees transfer storage, deregisters the client, waits for IDF no-client/all-free observations, uninstalls the host, deletes the PHY, releases the power lease, verifies the remaining power state is not unsafe/unknown, and only then restores the previous PHY route.

Any failed cleanup step retains ownership for a later retry. A discovery/event fault blocks new work but does not authorize skipping verified cleanup.

`phy_gpio.c` contains only the ESP32-S3 USB D+/D- drive-capability operation needed by the pinned IDF PHY and rejects non-USB pins or invalid drive strengths.

## Build and canonical replay

The repository-independent builder derives ESP32-S3 target flags from the `t5s3-pro` PlatformIO environment, rebuilds the pinned ESP-IDF v4.4.7 USB/PHY/SOC subset as PIC, supplies required ESP32-S3 MMIO symbols, links through `exports.map`, and audits ELF identity, exports, relocations, text relocations, unresolved USB-internal symbols, and scoped hardware ownership.

Canonical release replay uses the exact release commit **491e06131a8e9dc39c7b7dbb9e4b3e7fc128b3a7** and overlays the migrated driver bytes only after proving the driver file set and bytes match that release commit. The target canonical artifact for v0.1.19 is 789,504 bytes with SHA-256 `18c95f4264dfff2b21af13b0f0366327be896c75a0ae2d4fc1c9b0a220424da6`.

## Host tests

The synchronized upstream `controller_host_startup_test.cpp` checks the production startup header against fake IDF/power boundaries. Version 0.1.19 specifically verifies that an external acquisition callback occupies the same ordered acquisition point after PHY/client/DMA preparation and before connection is allowed.

The synchronized `controller_role_switch_test.cpp` covers:

- legacy external-input behavior;
- source-off observation and serial handback;
- attachment/claim ownership preventing idle park;
- failed cleanup retry;
- externally powered host start;
- external session validity loss and disconnect/drain behavior;
- transition from external power to battery source and back;
- at most three unstable external attempts;
- empty external trial returning to serial;
- failed external startup plus retained cleanup;
- UNKNOWN/SETTLING handling;
- independent-detector boards;
- ordinary start failure budget;
- tick rollover and disabled-role behavior.

## Known limits

The software path is validated by host fixtures and canonical release metadata. These tests do not establish physical receiver enumeration while Qi charging, available Qi current margin, or immunity to charging-pad interference. The upstream documentation explicitly leaves those as device-observation limits. Seamless USB continuity across moving onto/off external power is not claimed.

At this migration stage, exact v0.1.19 source, ABI, tests, documentation, and canonical target metadata are synchronized. The released-driver manifest remains `migrated: false` until destination CI executes the new host fixtures, replays the canonical 789,504-byte ELF, and passes the loader-map audit.

# usb-hid

## Purpose and scope

`usb-hid` is the ABI-v2 generic HID class provider above the runtime's `usb.host@1` capability. It discovers HID interfaces from complete USB configuration descriptors, owns interface claims and HID sessions, retrieves report descriptors, selects HID boot/report protocol where supported, and forwards interrupt-IN input reports. Keyboard and gamepad interpretation remain in the separate `usb-hid-keyboard` and `usb-hid-gamepad` providers.

## Package identity

- Driver ID/version: `usb-hid` 0.1.2
- Driver ABI/architecture: ABI 2, `xtensa-esp32s3`
- Source path/tree: `Drivers/usb_hid`, tree `56fc7707ffa7896ea5a0ea0ebc2e755c20edb520`
- Driver blob: `f404a73dcea13d4a01d51efa4205e64644fb78d2`
- Manifest blob: `62764e45e75d40623ae2bbce1268c8f0c6188d3b`
- Interrupt-extension ABI blob: `ace65e02ed10daa9d452cbd6e789c6e0a6b01135`
- Integration-fixture blob: `7c6eedc522d536042fa4e01b338f2defe3a41229`
- Requires: `usb.host@1`
- Provides: `usb.hid@1`
- Source status string: `experimental-unpublished`
- Published tag: `driver-usb-hid-v0.1.2`
- Canonical ELF: 7,380 bytes
- Canonical SHA-256: `b50db48b09c71a1f4a0f5885526657ba8f4324e5ccef4a28e0e2ed7908b12ffb`

The source manifest status and observed release-index publication are recorded separately; the release index publishes v0.1.2 despite the literal source status string.

## Source and ABI files

- `Drivers/usb_hid/driver.c` — exact upstream implementation.
- `Drivers/usb_hid/manifest.json` — exact upstream package manifest.
- `sdk/driver/RiscUsbHidV1.h` — existing HID composition ABI, already byte-identical to current upstream.
- `sdk/driver/RiscUsbInterruptV1.h` — append-only host/controller interrupt-read extension migrated with this driver.
- `sdk/driver/RiscUsbControllerV1.h` and `RiscUsbProviderV1.h` — lower USB host/controller ABI prefixes used by the extension.
- `test/drivers/usb_hid_test.c` — exact upstream three-provider integration fixture.
- `scripts/build_usb_hid.py` — standalone Xtensa build/export/import/canonical-parity validator.

## Exported root and dependency contract

The only intended public function is `t5_driver_get(uint32_t version)`; it returns the static `risc_driver_v2` only for provider ABI 2. The advertised capability table is `risc_usb_hid_api_v1`.

`start` accepts exactly one dependency named `usb.host`. The dependency declaration may report API version 1 or later, but the embedded host prefix must itself report `RISC_USB_HOST_API_V1`. The provider requires `struct_size >= sizeof(risc_usb_host_interrupt_v1)`, so this driver specifically depends on the append-only interrupt-read host extension while preserving the deployed `usb.host@1` prefix ABI.

Required callbacks are discovery `poll` and `devices`, host `configuration`, `claim`, `release`, and `control`, plus extension `interrupt_read`. Bulk callbacks are not used by this provider.

## Static resources and execution model

The implementation performs no dynamic allocation. Static capacities come from the shared ABI:

- 16 discovered HID interfaces.
- 8 live HID sessions.
- 8 currently present USB host device tokens.
- 512-byte maximum HID report descriptor.
- 64-byte maximum HID input report / interrupt endpoint packet.
- 4,096-byte USB configuration buffer inherited from the host ABI.

The source states that provider-executor calls, including calls into dependencies, are serialized. The provider contains no additional locking.

A session stores a monotonically generated token, a copy of the discovered interface identity, and the lower host claim token. Session tokens are not reset by `stop`; allocation fails if the 64-bit serial reaches `UINT64_MAX`.

## Configuration discovery and descriptor parsing

`scan(context,max_events)` requires a started, non-faulted provider and a work bound from 1 through 16. It asks the host discovery layer to process at most that many events and rejects a host result that reports more work than requested. It then snapshots at most `RISC_USB_HOST_MAX_DEVICES` generation-qualified device tokens and fetches each complete configuration descriptor.

A configuration must be at least nine bytes, use descriptor type 2, fit the 4,096-byte buffer, and have `wTotalLength` exactly equal to the returned length. The parser walks descriptor boundaries using each `bLength`; unknown descriptor types are skipped only after their framing has been validated.

Each interface descriptor starts a new scope. Only class 3 (HID) scopes are selected. For each selected interface/alternate the provider records device token, VID, PID, interface number, alternate setting, subclass, and protocol.

A selected scope must contain one usable HID descriptor and exactly one report-descriptor entry (descriptor type `0x22`) with a nonzero length no greater than 512 bytes. Multiple report-descriptor entries invalidate the scope. A selected scope must also contain exactly one interrupt-IN endpoint with nonzero endpoint number, no reserved address bits, nonzero packet size, and packet size no greater than 64 bytes. The endpoint interval is recorded without further policy.

Interfaces lacking a report descriptor, interrupt-IN endpoint, or packet size are treated as non-input and are not appended. Duplicate device/interface/alternate identities are rejected. More than 16 valid HID interfaces causes the parse/scan to fail.

Malformed framing deliberately does not byte-resynchronize. If corruption occurs after earlier scopes were completed, the parser can retain those completed interfaces while discarding the current scope. The exact upstream fixture exercises sibling corruption and framing-loss cases.

## Snapshot replacement and transient failure

A successful scan builds a complete candidate interface array and only then replaces the public interface snapshot. If host configuration acquisition or a hard parser failure returns false, `scan` returns false without replacing the last coherent snapshot. The upstream fixture explicitly verifies this behavior using a simulated transport failure.

`interfaces` copies the current snapshot into caller memory. If capacity is too small, or a nonempty snapshot is requested with a null output pointer, it writes the required count and returns false.

## Claims and session lifecycle

`open(device,interface,alternate)` succeeds only for an identity in the current snapshot. The same HID identity cannot be opened twice simultaneously. The provider requires a free one of eight session slots, claims that exact interface/alternate through the host, and assigns a new monotonically increasing session token.

Subsequent operations revalidate that the session identity is still present in the current discovery snapshot. `present` reports the same condition.

`close` calls the host release callback for the lower claim and clears the session slot. The source comment assigns failed-claim quarantine responsibility to the host layer; the HID API's host release callback is void and this provider does not attempt to recycle a lower physical claim itself.

## HID requests

`report_descriptor` requires a valid/current session and a capacity pointer. If the caller buffer is absent or too small, it reports the exact required report-descriptor length and returns false. Retrieval uses USB control request:

- `bmRequestType = 0x81`
- `bRequest = 0x06` (GET_DESCRIPTOR)
- `wValue = 0x2200` (HID Report descriptor)
- `wIndex = interface_number`
- timeout 100 ms

Success requires the control transfer to return exactly the expected descriptor length.

`set_boot_protocol` is permitted only for HID subclass 1 interfaces. It sends class request `0x21/0x0b` to the interface with value 0 for boot protocol and 1 for report protocol, no payload, and a 100 ms timeout. Success requires control return 0.

## Interrupt report input

`read` requires a valid/current session, non-null output, caller capacity at least the endpoint's advertised max packet and no more than 64 bytes, and timeout from 1 through 100 ms. It forwards the host claim, interrupt-IN endpoint, caller buffer/capacity, and timeout to the host extension's `interrupt_read` callback without rewriting its return value.

The interrupt ABI defines positive returns as copied completed reports, zero as no ready report, and negative values as errors. A zero result does not cancel controller-owned receive DMA; consumers are expected to use bounded polling and scheduler cooperation.

## Quiescence and stop

`quiesce` returns false while any of the eight HID session slots is live. `stop` refuses to clear the dependency until quiescence succeeds. After successful quiescence it clears the host pointer and discovered-interface count and resets the local `fault` flag. The current implementation checks `fault` on public operations but contains no assignment that sets it true.

## Integration validation

The exact upstream host fixture loads three ELF/shared-library providers: generic `usb-hid`, `usb-hid-keyboard`, and `usb-hid-gamepad`. Its synthetic composite device contains a boot keyboard HID interface, a report-protocol gamepad HID interface, and an unrelated vendor interface.

The fixture covers valid two-interface discovery plus seven malformed sibling/scope cases: missing HID descriptor entries, reserved endpoint bits, shortened/broken descriptor framing, invalid first-interface HID metadata with second-interface survival, framing loss before the sibling, descriptor length beyond the buffer, and endpoint packet size above the 64-byte limit. It also verifies that a configuration transport failure retains the last coherent snapshot.

After generic discovery, the same fixture composes keyboard and gamepad providers above `usb.hid`. It validates empty-bus steady state, reconnect discovery, keyboard connected/key events, gamepad connected/current-state output, snapshots, four-report keyboard transition ordering, bounded two-report gamepad work, gamepad state coalescing and stop-on-idle behavior, disconnect notifications, release of both lower claims, subscriber-gated class quiescence, generic-session quiescence, and clean unload.

This is deterministic provider simulation. It does not establish behavior on a physical USB controller/device.

## Standalone build and package validation

The standalone Xtensa builder validates the exact source manifest, produces ELF32 little-endian Xtensa `ET_DYN`, normalizes supported Xtensa relocations, requires `t5_driver_get` as the sole global function export, and requires exactly `memcpy` and `memset` as unresolved runtime imports. It records produced size/SHA-256 and whether they exactly match the canonical upstream release.

Published v0.1.2 files:

- `.package.json`: 617 bytes, SHA-256 `db748cf938b70e41e1238150dfea2403efa1297021fb1c5a7d587e99f274349d`
- `driver.elf`: 7,380 bytes, SHA-256 `b50db48b09c71a1f4a0f5885526657ba8f4324e5ccef4a28e0e2ed7908b12ffb`
- `provider-abi.v1`: 36 bytes, SHA-256 `1c7cb1cf375e55ae85b5c9cb3981969fb89fe5f36678609df90640c48a3fe53b`
- `privileged-imports.v1`: 14 bytes, SHA-256 `a46fff766cb063bc348b3e7a6670e4a48c5c9201f0da92e73a67670229be7cca`

Independent byte-for-byte parity is not recorded until integrated CI reproduces the canonical ELF.

## Established limitations

This provider only exposes HID input functions with one report descriptor and one interrupt-IN endpoint per interface/alternate. It does not implement HID interrupt-OUT, output/feature report transport, SET_IDLE/GET_IDLE, or arbitrary class-specific requests. Report interpretation remains intentionally outside this generic layer. Static limits are 16 discovered interfaces, 8 sessions, 512 descriptor bytes, and 64 report bytes.

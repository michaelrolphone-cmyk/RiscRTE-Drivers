# usb-host-v2

## Purpose and package identity

`usb-host-v2` v0.1.3 is the ABI-v2 `usb.host@1` provider between USB class drivers and the physical `usb.controller@1` provider. It is built for `xtensa-esp32s3`, requires `usb.controller@1`, and provides `usb.host@1`. The source manifest status is `experimental-unpublished`; the inspected upstream release index nevertheless publishes tag `driver-usb-host-v2-v0.1.3`.

Current upstream facts:
- source path: `Drivers/usb_host_v2`
- source tree SHA: `8465756ed140fc537fe991089a3f305833dc30bd`
- `driver.c` blob: `5cdddf061b454d514f237ffcdc9a77341b6e238c`
- manifest blob: `d8a99460b5d70e63ab61f5e891500458052922ce`
- host-test blob: `572c18f0d1c8328cb970ec8657fc03fd65aa2e67`
- `RiscUsbInterruptV1.h` blob: `ace65e02ed10daa9d452cbd6e789c6e0a6b01135`
- `RiscUsbDiscoveryDiagnosticsV1.h` blob: `48b879d8dc0cc01e22ba647db274513e2e3c76f8`

The only intended public ELF function is `t5_driver_get(uint32_t abi)`, which returns the static provider descriptor only for driver ABI 2.

## Source and ABI files

The migrated implementation consists of `Drivers/usb_host_v2/driver.c`, its manifest, the exact upstream host fixture `test/drivers/usb_host_v2_test.c`, and two append-only USB ABI extension headers: `RiscUsbInterruptV1.h` and `RiscUsbDiscoveryDiagnosticsV1.h`. Existing `RiscProviderV2.h`, `RiscUsbProviderV1.h`, and `RiscUsbControllerV1.h` in this repository already match the current upstream ABI used by the driver.

The published capability pointer is the base `risc_usb_host_api_v1` prefix inside a larger extension chain: `risc_usb_host_discovery_v1`, then `risc_usb_host_interrupt_v1`, then `risc_usb_host_diagnostics_v1`. Existing consumers can use the base prefix; extension-aware consumers use `struct_size` to determine whether discovery, interrupt-IN, or diagnostics are present.

## Startup and dependency validation

`start` accepts exactly one dependency: `usb.controller@1`. It rejects missing or extra dependencies, a wrong capability/API version, null API, a second start, or a prior terminal event fault.

The lower controller must be at least `sizeof(risc_usb_controller_interrupt_v1)` and must provide `next_event`, `configuration`, `claim`, `release`, `control`, `bulk_read`, `bulk_write`, `quiesce`, and `interrupt_read`. Controller diagnostics are optional and are used only when the controller's `struct_size` reaches `risc_usb_controller_diagnostics_v1` and its callback is non-null.

## State, limits, and ownership

The implementation is allocation-free. Static state contains eight device slots, sixteen interface-claim slots, one 4,096-byte configuration-descriptor scratch buffer, a monotonic 64-bit logical-token sequence, and an `event_fault` flag.

Device slots hold a logical token, a physical controller device token, and presence state. Claim slots hold a logical token, physical claim token, owning logical device, interface/alternate setting, descriptor-derived endpoint masks, and a `closing` quarantine flag.

Source comments state that calls execute on the serialized provider executor. No additional mutexes are present, so arbitrary concurrent invocation outside that execution contract is not established.

The host does not own VBUS, root-port enumeration hardware, controller DMA, or physical transfers. Those resources remain owned by `usb.controller@1`.

## Discovery and generation-safe device tokens

`poll` accepts from 1 through 64 lower controller events. Controller event kind 1 is attach and kind 2 is detach; physical device token zero, malformed return codes, unknown detach, and duplicate attach fail closed.

An attach allocates a free logical device slot and assigns a fresh monotonic token. A detach immediately marks the logical device absent, but the slot remains pinned while claims refer to it. Logical token sequence is deliberately not reset across a clean provider stop/restart, preventing stale handles from becoming valid again.

Malformed event state, slot exhaustion, or token overflow sets `event_fault`. Once set, normal lookups and quiescence fail, and this source provides no path that clears the fault.

`devices()` copies only currently present logical device tokens. If the caller buffer is too small, it reports the required count and returns false.

## Configuration descriptors and matching

`configuration()` requires a live logical device and a caller buffer from 9 through 4,096 bytes. It forwards the read to the lower controller and then verifies the returned descriptor is a configuration descriptor and that `wTotalLength` exactly equals the returned size.

Before claiming an interface, the host re-reads the configuration descriptor into its static scratch buffer. Descriptor lengths are validated before traversal. The requested interface is selected only when both interface number and alternate setting match.

For that selected interface the provider records endpoint-number masks for:
- bulk IN and bulk OUT endpoints with packet size from 1 through 512 bytes;
- interrupt IN endpoints with packet size from 1 through 64 bytes.

Endpoint zero, reserved endpoint-address bits, duplicate endpoints in the same transfer/direction mask, malformed descriptor sizes, and invalid packet sizes are rejected. Once a following interface descriptor is reached, unrelated composite-interface descriptors no longer affect the selected interface.

## Claims and resource lifecycle

Only one host claim may exist for the same logical device/interface pair. Claim creation fails if no claim slot is free, descriptor validation fails, or the lower controller refuses the physical claim.

After a physical claim succeeds, the host assigns a new logical claim token. If token generation fails after physical ownership has already been acquired, the physical claim is retained in a quarantined closing slot for later release rather than silently leaked or reused.

`release()` is void by ABI. It marks the claim `closing` and asks the lower controller to release its physical claim. The slot is cleared only after the lower release returns true. Failed releases therefore remain pinned and are retried by `quiesce`.

## Control, bulk, and interrupt transfers

Control transfers require a live logical device, nonzero timeout, a payload when length is nonzero, and length no greater than 4,096 bytes. Interface-recipient requests additionally require an active non-closing claim on the same logical device whose interface number equals `wIndex`; interface indices above 255 fail.

Bulk reads/writes require a live non-closing claim, a still-present owning device, nonzero timeout, length/capacity no greater than 4,096 bytes, correct endpoint direction, and membership in the descriptor-derived bulk endpoint mask. Lower results larger than caller bounds are converted to failure.

Interrupt reads require a live claim, present device, descriptor-authorized IN endpoint, capacity from 1 through 64 bytes, and timeout from 1 through 100 ms. The lower controller's result is accepted only when it fits the supplied capacity. By ABI contract, zero means no completed report is ready and does not cancel controller-owned receive work.

A detached device invalidates subsequent class transfers immediately even though its physical claim may remain pinned until release.

## Diagnostics

The optional `risc_usb_host_diagnostics_v1::diagnostic` callback forwards the lower controller's diagnostic snapshot into a caller-owned bounded character buffer. It does not interpret that text, consume events, or perform USB I/O.

## Quiesce and stop

Quiescence fails while a non-closing claim exists. Closing claims are retried through the lower release callback and remain retained if release still fails. After all claims drain, quiescence additionally requires no terminal event fault and successful lower controller quiescence.

`stop` returns without clearing dependencies if quiescence fails. On successful stop it clears device slots and the controller pointer while preserving the monotonic token sequence.

## Failure handling

The implementation fails closed on malformed controller event streams, inconsistent configuration descriptors, stale or detached device tokens, stale/closing claims, unauthorized endpoints, oversized lower transfer results, and failed lower release. Event-stream corruption is terminal for the currently loaded provider state.

## Tests and build validation

The exact upstream host fixture builds the provider as a shared object and validates ABI identity, dependency binding, attach/detach discovery, capacity-query behavior, generation-qualified tokens, configuration copying, interface matching, duplicate-claim rejection, interface-recipient control authorization, descriptor-gated bulk access, rejection of wrong/unadvertised endpoints, detach invalidation, failed-release quarantine/retry, lower-controller quiescence gating, clean restart without token reuse, stale-token rejection, and optional diagnostics forwarding.

The fixture was executed against the exact staged upstream source under `-Wall -Wextra -Werror` and passed with:

`USB host ELF discovery/generation, bulk and interrupt claim gates: PASS`

Repository CI compiles the exact source independently with the Xtensa ESP32-S3 toolchain, normalizes relocations, validates the ELF architecture/export/import surface, and checks the produced bytes against the canonical published artifact before uploading `dist/usb-host-v2`.

CI run `36329594203` completed successfully and reproduced the canonical `driver.elf` byte-for-byte: 8,580 bytes, SHA-256 `c94dea393a6ddae1e9fcf4ae309654b8f9a0d26df6da7d222a1f8c85f93ea183`. Published-byte parity is therefore confirmed for the migrated source/toolchain path.

## Published package metadata

Upstream release-index metadata for v0.1.3:
- `.package.json`: 626 bytes, SHA-256 `9db06b67fa7d33a4673a1702ea6bea921ed8fc77b84be36a47f275dec8b73412`
- `driver.elf`: 8,580 bytes, SHA-256 `c94dea393a6ddae1e9fcf4ae309654b8f9a0d26df6da7d222a1f8c85f93ea183`
- `provider-abi.v1`: 37 bytes, SHA-256 `270e93fd7526e0193fa8670e18410c96b2b081b202823448bba14bdf5ef5faa9`
- `privileged-imports.v1`: 7 bytes, SHA-256 `1591d47dab39b502bcc6e3e65dc1d466fb29f53433054fe5046f432ab4a6d16d`

The privileged-import inventory corresponds to the single unresolved symbol `memset`.

## Established limitations

The current source supports at most eight tracked devices and sixteen host claims. It exposes bulk and interrupt-IN transfers, not isochronous or interrupt-OUT. There is one shared static descriptor buffer. The source assumes serialized provider-executor calls. Event-fault recovery is not implemented.

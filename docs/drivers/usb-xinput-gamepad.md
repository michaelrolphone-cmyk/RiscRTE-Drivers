# usb-xinput-gamepad

## Purpose and scope

`usb-xinput-gamepad` is the ABI-v2 `usb.xinput.gamepad@1` input provider for Xbox 360 wired-format devices and Xbox 360 wireless-format receiver interfaces. It depends on `usb.host@1` with the interrupt-read extension and on `platform.clock@1`.

USB enumeration, physical claims, and interrupt transfers remain owned by the lower `usb.host` provider. This driver owns XInput-compatible interface matching, bounded discovery/retry state, controller-slot ownership, Xbox 360 packet decoding, normalized gamepad state, compatibility subscriptions, snapshots, disconnect handling, and provider diagnostics. It does not issue HID requests, USB resets, output/rumble effects, LEDs, or controller-specific power changes.

## Package identity and observed release state

- Driver ID: `usb-xinput-gamepad`
- Version: `0.1.3`
- Driver ABI: `2`
- Architecture: `xtensa-esp32s3`
- Source path: `Drivers/usb_xinput_gamepad`
- Upstream source tree: `b84a41ad08114f0a6e0aa744582056d1a13879c4`
- `driver.c` blob: `7f68505443ffafe3ac3f2cb6f24e3ca5315b98b3`
- `manifest.json` blob: `54566d90d5b4899601e1b32f267e42b0d30159d8`
- Exact upstream host-test blob: `f2d02e3189754387a46ce840894b17de30571da5`
- Requires: `usb.host@1`, `platform.clock@1`
- Provides: `usb.xinput.gamepad@1`
- Source manifest status: `experimental-unpublished`
- Published tag: `driver-usb-xinput-gamepad-v0.1.3`
- Canonical `driver.elf`: 11,088 bytes
- Canonical ELF SHA-256: `e2cee3355937d7815fb5685598cd2411c7d508bb36424f00636ec7495f654c26`

The source status and release-index publication are separate observations: the current source manifest says `experimental-unpublished`, while the current inspected release index publishes v0.1.3.

## Source, shared ABI, and helper files

The implementation includes:
- `RiscUsbInterruptV1.h` for the append-only interrupt-read extension over `usb.host@1`;
- `RiscUsbGamepadDiagnosticsV1.h` for the gamepad API plus diagnostic callback;
- `RiscPlatformClockV1.h` for monotonic discovery timing;
- `Drivers/usb_hid_gamepad/StateMailbox.h` for coalesced compatibility subscription delivery.

At migration time all four destination files are byte-identical to current upstream:
- `RiscUsbInterruptV1.h`: `ace65e02ed10daa9d452cbd6e789c6e0a6b01135`
- `RiscUsbGamepadDiagnosticsV1.h`: `75e2f396069663d9d0be21440681a4810aca9e61`
- `RiscPlatformClockV1.h`: `33a5b27dcea7e30698d0177ad5db065166082194`
- `StateMailbox.h`: `9b6dc1e4c8ecf8ddc2d64da4e862f86b3a530793`

No duplicate copies are introduced by this migration.

## Exported root and capability table

The only intended global function export is `t5_driver_get(uint32_t abi)`. It returns the static driver descriptor only for provider ABI 2.

The published capability table is `risc_usb_gamepad_diagnostics_v1`. Its base `risc_usb_gamepad_api_v1` supplies:
- `subscribe(context, device_or_zero)`;
- `unsubscribe(context, subscription)`;
- `poll(context, max_reports)`;
- `next(context, subscription, out_event)`;
- `snapshot(context, out_states, inout_count)`.

The diagnostics extension supplies a bounded `diagnostic(context, out, capacity)` callback that copies the current human-readable provider status and NUL-terminates when capacity is nonzero.

## Dependency binding and startup

`start` requires exactly two dependencies. They may arrive in either order:
- `usb.host@1`, cast to `risc_usb_host_interrupt_v1`;
- `platform.clock@1`, cast to `risc_platform_clock_api_v1`.

Duplicate, missing, unknown, or wrong-version dependencies fail startup. The host API must expose the complete interrupt extension structure, bounded host `poll`, `devices`, `configuration`, `claim`, `release`, and `interrupt_read`. The clock API must expose `monotonic_ms`.

A second successful start while already bound is rejected. Startup sets the diagnostic text to `WAITING FOR XINPUT DISCOVERY`; physical discovery remains incremental in later `poll` calls.

## Matching policy

The source does not gate on a specific VID/PID. It intentionally binds from the proven Xbox 360 vendor-interface descriptor shape so compatible clone receivers can work.

A candidate interface must be:
- USB interface class `0xff`;
- subclass `0x5d`;
- protocol `0x01` for wired-format input or `0x81` for wireless receiver input.

The selected input endpoint must be:
- interrupt type;
- IN direction;
- endpoint number nonzero with reserved address bits clear;
- packet size from 20 through 64 bytes;
- nonzero polling interval.

The parser requires a valid complete USB configuration descriptor with exact `wTotalLength` and structurally bounded subdescriptors. The first complete qualifying interface/endpoint is returned; unsupported descriptor layouts are marked nonretryable because static descriptors are not expected to become valid later.

The test explicitly demonstrates binding a clone VID/PID after matching the `ff/5d` interface protocol, while rejecting an unknown vendor protocol and a bulk endpoint substituted for interrupt input.

## Controller and discovery capacity

The implementation has four gamepad slots (`PADS = 4`) and inspection state for at most `RISC_USB_HOST_MAX_DEVICES` current host devices.

Discovery is incremental. Each call:
1. asks host discovery to process at most eight host events;
2. snapshots current generation-qualified device tokens;
3. releases pad slots whose device disappeared;
4. drops inspection records for detached devices;
5. performs at most one new/retry inspection/claim attempt before report draining.

A device that already has a successfully cached descriptor/interface does not repeatedly reread configuration while retrying its claim.

## Discovery backoff and failure state

Configuration reads and interface claims use finite retry state:
- maximum attempts: 8;
- total retry deadline: 10,000 ms after discovery attempts begin;
- first delay: 100 ms;
- exponential doubling by attempt;
- delay cap: 2,000 ms.

Polling frequency cannot shorten the elapsed-time backoff. A clock value of `UINT64_MAX` pauses discovery and reports `XINPUT DISCOVERY CLOCK FAILED`. If monotonic time moves backward after attempts begin, retry work is also suppressed rather than performing repeated USB operations against an invalid elapsed-time calculation.

If all four pad slots are occupied, an otherwise configured device enters `XINPUT WAITING FOR FREE PAD SLOT`. Its attempt counter is reset when capacity becomes available so capacity starvation is not charged against USB configuration/claim retry limits.

Examples of diagnostic states explicitly assigned in source include:
- `XINPUT USB DISCOVERY FAILED`
- `NO XINPUT DEVICE DISCOVERED`
- `XINPUT CONFIGURATION READ RETRYING`
- `XINPUT CONFIGURATION READ RETRY LIMIT`
- `XINPUT INTERFACE CLAIM RETRYING`
- `XINPUT INTERFACE CLAIM RETRY LIMIT`
- `XINPUT DISCOVERY RETRY DEADLINE`
- `XINPUT WAITING FOR FREE PAD SLOT`
- `XINPUT DISCOVERY CLOCK FAILED`
- `XINPUT INTERRUPT READ FAILED`
- `XINPUT WAITING FOR REPORT`
- `XINPUT GAMEPAD CONNECTED`.

## Claim and resource ownership

When a qualifying interface is discovered and a pad slot is free, the driver calls the host `claim` function with the generation-qualified device token plus selected interface/alternate setting. A zero or failed claim is retried according to the bounded discovery policy.

A successful slot stores:
- device token;
- host claim token;
- interrupt-IN endpoint;
- wired/wireless-format flag;
- normalized `risc_usb_gamepad_state_v1`.

The host remains the owner of actual USB resources. On detach/quiescence the driver calls host `release` and clears its local slot. The source comment explicitly relies on the lower host provider to retain failed/pending physical releases and pin its controller until those resources drain; no completion callback points into this class ELF.

## Polling and interrupt-read bounds

The public gamepad `poll` requires `max_reports` from 1 through 16. It rejects zero and values above 16.

After one bounded discovery pass, it drains already-completed controller input in round-robin style. Each interrupt read:
- uses the pad's host claim and selected endpoint;
- provides a 64-byte destination buffer;
- uses a 10 ms timeout;
- counts against `max_reports`.

A zero-length read marks that pad quiet for the current call so it is not retried again in the same polling invocation. This prevents one idle endpoint from consuming the full report budget. A negative or greater-than-64 return is treated as transport failure.

On transport failure, a previously connected normalized state is first cleared and a disconnect event is emitted. The host claim is deliberately retained so a later valid packet on the same attachment may reconnect; `poll` then returns false and reports `XINPUT INTERRUPT READ FAILED`.

## Wired-format packet acceptance

For a non-wireless pad the decoder ignores:
- null data;
- transfers longer than 64 bytes;
- transfers shorter than 20 bytes;
- packets where byte 0 is not zero;
- packets whose header length byte is less than 14.

This intentionally ignores status/LED messages and incomplete/corrupt input without clearing held state. The upstream fixture proves that a complete 20-byte transfer with header length 14 is accepted.

## Wireless receiver framing

Wireless-format interfaces use protocol `0x81`.

For short receiver messages:
- a presence message with `data[0] & 8` and `data[1] & 0x80` can emit a connected notification if not already connected;
- the corresponding absence form clears normalized state and emits disconnect;
- other non-input message types are ignored.

Actual wireless controller input requires at least 24 received bytes. The first four receiver bytes are then skipped and the remaining 20-byte Xbox 360 input frame is decoded with the same normalization as wired input.

## Button normalization

The normalized button field preserves Xbox semantic names rather than swapping by physical position.

The source maps:
- A/B/X/Y from Xbox packet bits into the gamepad API's semantic button bit ordering;
- left/right shoulders;
- Back and Start;
- left/right stick clicks;
- Guide;
- analog trigger threshold bits for button compatibility.

The exact upstream fixture checks all four face buttons independently and proves that Xbox A remains semantic A rather than becoming B.

## D-pad / HAT normalization

D-pad bits are converted to HAT values using 8 as neutral:
- 0 = north;
- 1 = northeast;
- 2 = east;
- 3 = southeast;
- 4 = south;
- 5 = southwest;
- 6 = west;
- 7 = northwest;
- 8 = centered.

The fixture iterates all 16 combinations of the four d-pad bits and verifies the implementation's conflict resolution/hat output.

## Axis and trigger normalization

The four stick axes are decoded as little-endian signed 16-bit values:
- left X → `x`;
- left Y → `y` after `invert_y`;
- right X → `rx`;
- right Y → `ry` after `invert_y`.

`invert_y` uses bitwise complement on the signed 16-bit value. This establishes the exact endpoint behavior verified by the fixture: input `-32768` becomes `32767`, while input `32767` becomes `-32768`.

Left and right trigger bytes are expanded into signed 16-bit ranges:
`trigger * 257 - 32768`, stored in `z` and `rz`.

## State-change and event semantics

A decoded pad starts with device token, `connected = 1`, and neutral HAT 8. The implementation compares buttons, HAT, four stick axes, and two trigger axes against the previous state.

First valid input emits a connect event and a state event internally. The shared `StateMailbox` coalesces a same-device state event immediately following connect while retaining event kind 1, so compatibility subscribers receive one latest-state connect notification.

Unchanged repeated packets emit no new event.

Disconnect clears buttons/axes, preserves the device token, sets HAT 8, and emits kind 2.

## Compatibility subscriptions and mailbox behavior

Up to `RISC_USB_INPUT_MAX_SUBSCRIBERS` = 4 subscriptions are supported. A subscription may filter to one device token or use zero for all devices.

The shared mailbox stores only the latest event for each of at most four pad attachment generations. It is not a button-transition FIFO. Repeated state updates for one device replace earlier state in that subscriber's mailbox. If more than four distinct device generations would be outstanding, the mailbox enters a gap state; the next `next` returns -1 and resets the mailbox.

Events are returned in sequence order across distinct current device entries. Subscription tokens are monotonically generated and not reset by clean stop/restart, preventing an old token from intentionally becoming valid again.

The fixture performs thousands of alternating updates without draining and verifies latest-state coalescing rather than queue growth.

## Snapshot semantics

`snapshot` reports every currently claimed pad slot, including pads that are attached but still waiting for their first valid controller report. The caller supplies capacity; insufficient capacity or null output for a nonempty snapshot returns false after writing the required count. Successful calls set the actual count.

A pad that is claimed but has not received valid input has its device token set, HAT 8, and `connected = 0`.

## Quiescence and stop

`quiesce` fails while any compatibility subscription token remains active. After subscribers are gone, it releases every remaining claimed pad slot. If a connected pad is being released, a disconnect event is generated before the local slot is cleared.

`stop` calls `quiesce`; if quiescence fails, dependencies remain bound. After successful quiescence, host/clock pointers and inspection state are cleared and diagnostic text becomes `XINPUT DRIVER NOT STARTED`.

## Concurrency assumptions

The implementation uses file-static mutable arrays/counters and contains no internal mutex or atomic coordination. It therefore establishes no independent multi-thread safety guarantee beyond the serialized provider-executor assumptions of the surrounding RiscRTE provider model.

## Exact upstream validation

The exact upstream fixture `test/drivers/usb_xinput_gamepad_test.c` covers:
- ABI-v1 rejection and ABI-v2 lookup;
- missing/undersized dependencies and duplicate start;
- a device already present before provider start;
- repeated configuration-read failures with elapsed-time backoff;
- cached successful descriptors during claim retries;
- finite eight-attempt claim failure;
- 10-second retry deadline;
- broken-clock pause and later recovery;
- detach during retry and generation-safe reconsideration;
- unsupported descriptor caching;
- report-budget validation and one-idle-read behavior;
- truncated/status/header-length packet rejection;
- named Xbox face-button mapping;
- all d-pad combinations;
- stick/trigger endpoint normalization;
- subscriber filtering and latest-state mailbox behavior;
- detach/disconnect cleanup and snapshots;
- clone VID/PID interface matching;
- wireless presence, input, and disconnect frames;
- unknown protocol, wrong endpoint type, and truncated configuration rejection;
- claim failure followed by recovery without unplug/replug;
- input transport failure followed by reconnect on the retained claim;
- subscriber-gated quiescence;
- restart with stale subscription rejection.

Its success string is:
`XInput discovery, current state, bounded startup retries, hotplug and shutdown: PASS`.

This is deterministic provider simulation. It does not establish physical radio quality, USB-controller timing, rumble/LED output behavior, or compatibility with protocols outside the matched Xbox 360 wired/wireless descriptor formats.

## Standalone Xtensa build validation

`scripts/build_usb_xinput_gamepad.py` validates the exact source manifest, builds a 32-bit little-endian Xtensa ET_DYN ELF with the ESP32-S3 toolchain, normalizes supported Xtensa relocations, requires `t5_driver_get` as the sole global function export, and requires exactly `memcpy` and `memset` as unresolved runtime imports.

The build records its produced size/SHA-256 plus exact canonical byte parity.

## Integrated migration validation

CI run `36345187504` executed the exact upstream host fixture and the standalone Xtensa builder from this repository. The fixture completed with:

`XInput discovery, current state, bounded startup retries, hotplug and shutdown: PASS`

The independent Xtensa build produced `driver.elf` at exactly 11,088 bytes with SHA-256 `e2cee3355937d7815fb5685598cd2411c7d508bb36424f00636ec7495f654c26` and reported `byte_parity=True`. The run uploaded the resulting package as artifact `usb-xinput-gamepad` (artifact ID `10939434367`). This establishes byte-for-byte parity with the observed upstream v0.1.3 release artifact for the inspected source baseline.

## Published package metadata

Observed v0.1.3 release files:
- `.package.json`: 673 bytes, SHA-256 `d330f0114ee5d6023a74b994afb1a55af60f73c62a63fde61b3c13e6edd51ccc`
- `driver.elf`: 11,088 bytes, SHA-256 `e2cee3355937d7815fb5685598cd2411c7d508bb36424f00636ec7495f654c26`
- `provider-abi.v1`: 47 bytes, SHA-256 `34a80c48f2d2cef60c5578a4a1f1ec14906876a54cda5196a81aea39838c237c`
- `privileged-imports.v1`: 14 bytes, SHA-256 `a46fff766cb063bc348b3e7a6670e4a48c5c9201f0da92e73a67670229be7cca`

## Established limitations

- Matching is descriptor/protocol based and is limited to Xbox 360 wired protocol 1 and wireless protocol 0x81 interface forms.
- Output effects, rumble, LED control, and controller-specific USB power management are not implemented.
- At most four pads and four compatibility subscribers are represented at once.
- Interrupt input reports are capped at 64 bytes.
- Public poll work is capped at 16 report attempts per call.
- Discovery retries are capped at eight attempts and 10 seconds per attachment.
- The compatibility event path coalesces current device state rather than preserving a full transition FIFO.
- Internal multi-thread synchronization is not implemented.

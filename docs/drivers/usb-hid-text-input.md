# usb-hid-text-input

## Purpose and scope

`usb-hid-text-input` is a provider-ABI-v2 transport adapter that converts ordered `usb.hid.keyboard@1` events into the transport-neutral `input.text@1` capability. The implementation contains no USB host or physical keyboard handling of its own; it depends on the keyboard provider and publishes copied semantic text/key events to its own subscribers.

## Package identity

- Driver ID: `usb-hid-text-input`
- Version: `0.1.0`
- Driver ABI: `2`
- Architecture: `xtensa-esp32s3`
- Output file: `driver.elf`
- Provides: `input.text@1`
- Requires: `usb.hid.keyboard@1`
- Source status: `experimental-unpublished`
- Upstream release status: published as `driver-usb-hid-text-input-v0.1.0`; canonical ELF size 8,692 bytes, SHA-256 `796cd1b754de33f3c12cd5e29a54036766d081b71a5f05ff2d66a37beeb3ef3d`
- Upstream source tree SHA: `4eccdb8fb91295ff905f92142301e999819d4805`

## Source, ABI, build and test files

- `Drivers/usb_hid_text_input/driver.c`
- `Drivers/usb_hid_text_input/manifest.json`
- `sdk/driver/RiscTextInputV1.h`
- `sdk/driver/RiscUsbHidV1.h`
- `sdk/driver/RiscUsbControllerV1.h`
- `sdk/driver/RiscUsbProviderV1.h`
- `sdk/driver/RiscProviderV2.h`
- `scripts/build_usb_hid_text_input.py`
- `test/drivers/usb_hid_text_input_test.c`

The standalone Xtensa build validates the exact manifest identity, compiles as a PIC shared ELF, normalizes Xtensa relocations, verifies that `t5_driver_get` is the sole global defined function export, and verifies ELF32/little-endian/shared-object/Xtensa machine metadata. Its produced size and SHA-256 can now be compared directly with the canonical published upstream ELF; byte-for-byte parity is claimed only after an exact match is observed.

The migrated host test is the upstream behavioral test. It compiles the driver directly into a C test executable with strict warnings and exercises translation and lifecycle behavior without USB hardware.

## Provider ABI and exported symbol

The ELF exports `t5_driver_get(uint32_t abi)`. It returns the static `risc_driver_v2` descriptor only when `abi == RISC_PROVIDER_DRIVER_ABI_V2` (2).

The descriptor identifies `usb-hid-text-input`, advertises `input.text` API 1, and provides `start`, `stop`, and `quiesce`. Its capability table is a `risc_text_input_api_v1` with `subscribe`, `unsubscribe`, `poll`, and `next`.

## Text input interface

`RiscTextInputV1.h` defines four subscribers and 32 queued events per subscriber.

Each `risc_text_input_event_v1` contains:

- monotonically increasing `sequence`
- transport/source identity in `source`
- Unicode codepoint value in `codepoint` when the key has a printable mapping
- semantic key identifier in `key` for supported non-printing/special keys
- event kind
- semantic modifier bits

Event kinds are CONNECTED, DISCONNECTED, KEY_DOWN, KEY_UP, and GAP. The implementation reports gaps through a negative `next` result rather than emitting a normal queued GAP record itself.

A subscription filter of zero accepts every source. A nonzero filter accepts only that source identity.

## Source tracking and Caps Lock state

The driver tracks at most four physical keyboard source identities. Each tracked source stores only its source token and Caps Lock state.

CONNECT events create source state when a slot is available and emit a semantic CONNECTED event. DISCONNECT removes the source state and emits DISCONNECTED.

If a normal key event arrives for an untracked source, the driver attempts to allocate a source slot. If all four source slots are occupied, it marks every active semantic subscription as gapped and drops the event rather than mis-associating Caps Lock state.

Caps Lock toggles only on raw key-down usage `0x39`. Its state is tracked independently per source.

## Modifier translation

Raw USB keyboard modifier bits are translated as follows:

- raw mask `0x22` -> `RISC_TEXT_MOD_SHIFT`
- raw mask `0x11` -> `RISC_TEXT_MOD_CTRL`
- raw mask `0x44` -> `RISC_TEXT_MOD_ALT`
- raw mask `0x88` -> `RISC_TEXT_MOD_META`
- per-source Caps Lock state -> `RISC_TEXT_MOD_CAPS`

Modifier usages `0xe0` through `0xe7` do not create standalone semantic key events. Their state is carried on subsequent ordinary key events.

## Printable translation

Alphabetic HID usages 4 through 29 map to `a` through `z`, with case selected by Shift XOR Caps Lock.

Number-row usages 30 through 39 map to `1234567890`; with Shift they map to `!@#$%^&*()`.

The source also maps space, minus/underscore, equals/plus, brackets/braces, backslash/pipe, semicolon/colon, quote/double quote, grave/tilde, comma/less-than, period/greater-than, slash/question-mark, and the implemented keypad operator/digit usages. Unsupported printable usages return codepoint zero.

The driver does not implement locale layouts, dead keys, composed Unicode, or arbitrary HID usage pages; those features are not present in the current source.

## Semantic special keys

The implementation maps the following HID usages to semantic keys:

- Enter and keypad Enter
- Escape
- Backspace
- Tab
- Caps Lock
- Home
- Page Up
- Delete
- End
- Page Down
- Right, Left, Down, Up

A raw key-down or key-up is published when it has either a semantic special-key mapping or a printable codepoint. Unsupported ordinary usages are ignored.

## Polling and raw-event drain bounds

`poll` requires an active underlying keyboard subscription and a `max_reports` value from 1 through 16. It first calls the underlying keyboard provider's `poll(context, max_reports)` and fails immediately if that call returns false.

After a successful lower-provider poll, it calls lower-provider `next` at most `RISC_USB_INPUT_QUEUE_LENGTH` (32) times. It stops on an empty result.

A negative lower-provider `next` result marks every semantic subscriber as gapped, clears all four source/Caps-state slots, and stops draining for that call. This is treated as a recoverable input-stream discontinuity rather than a provider failure.

## Semantic subscriber queues and gap handling

There are at most four semantic subscribers. Subscription tokens are monotonically incremented from a static 64-bit counter; subscription creation fails when the counter reaches `UINT64_MAX` or all four slots are occupied.

Each subscriber owns a copied 32-event ring buffer. When an event arrives for a full subscriber queue, that subscriber's queue is cleared and marked gapped. While gapped, further events are not copied into that queue.

For a matching subscription, `next` returns:

- `1` and one copied event when data is queued
- `0` when the queue is empty
- `-1` for an invalid call/token or when acknowledging a gap

When a gap is reported, `next` clears that subscriber's gap flag and queue so subsequent polling can resume from a clean semantic stream.

The global event sequence refuses to wrap: if it reaches `UINT64_MAX`, `emit` fails and the translating `poll` call returns false for events that require emission.

## Dependency validation and startup

`start` accepts exactly one dependency. It must be capability `usb.hid.keyboard`, API version 1, with a non-null `risc_usb_keyboard_api_v1`.

The lower interface must itself report API 1, have at least the compiled structure size, and provide all of `subscribe`, `unsubscribe`, `poll`, `next`, and `snapshot`.

Startup subscribes to the lower keyboard provider with source filter zero. If that subscription cannot be acquired, startup fails without publishing the semantic provider.

Calling `start` while `keyboard` is already set fails; the implementation does not silently replace an active dependency.

## Quiescence, stop and ownership

`quiesce` refuses to unload while any semantic subscriber token remains active.

Once semantic subscribers are gone, `quiesce` unsubscribes the single lower keyboard subscription. If the lower provider refuses that unsubscribe, `quiesce` returns false and retains the dependency state.

`stop` calls `quiesce` and returns without clearing `keyboard` when quiescence fails. After successful quiescence, `stop` clears the lower-provider pointer and all source/Caps state.

This preserves dependency ownership when cleanup is uncertain instead of claiming a clean unload.

## Failure behavior and limits

Confirmed failure conditions include invalid dependency count or capability identity, API mismatch, undersized/missing dependency methods, lower subscription failure, invalid poll bound, lower-provider poll failure, semantic token exhaustion, semantic subscriber exhaustion, null/invalid `next` arguments, semantic sequence exhaustion, and lower unsubscribe failure during quiescence.

A lower raw stream gap or source-table exhaustion causes semantic subscribers to receive a gap on their next read rather than returning stale translated state.

The implementation is allocation-free and uses file-static state. The provider ABI states calls are serialized by the provider executor; the implementation contains no internal locking and does not establish concurrent-call safety beyond that execution model.

## Validation status

The driver source, manifest, `RiscTextInputV1.h`, `RiscUsbHidV1.h`, `RiscUsbControllerV1.h`, and upstream host test were verified unchanged at T5S3-Reader master commit `99abac00a0ec49e16da0110833f1f51e8d23c6d0`; their recorded Git blob SHAs still match the migrated copies. Existing `RiscUsbProviderV1.h` already matches upstream byte-for-byte by Git blob SHA `7e6a7512c332fd2014ecf4b44fa0c56de18a1e07`.

The upstream driver tree remains `4eccdb8fb91295ff905f92142301e999819d4805`. The release index now publishes version 0.1.0 as `driver-usb-hid-text-input-v0.1.0`; its canonical `driver.elf` is 8,692 bytes with SHA-256 `796cd1b754de33f3c12cd5e29a54036766d081b71a5f05ff2d66a37beeb3ef3d`. The upstream source manifest still carries the literal status `experimental-unpublished`; that source metadata is recorded separately from the observed release-index publication state. The host behavioral test is preserved unchanged from upstream. Cross-compilation and byte-for-byte comparison with the canonical ELF remain integrated-CI validation points because there is no local Xtensa toolchain in the automation execution environment.

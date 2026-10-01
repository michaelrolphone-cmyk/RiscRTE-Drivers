# display-epd-video

## Purpose and package identity

`display-epd-video` 0.1.3 exposes the installable provider-v2 `display.output@1` interface. It currently delegates its scan engine to the firmware's `T5VideoApi`; the app-facing capability remains `RiscDisplayOutputV1`.

- Driver ABI: 2; architecture: `xtensa-esp32s3`.
- Source path/tree: `Drivers/display_epd_video`, tree `b8cd15436e6354c0e871b74ef0ee1d1feb04bba6`.
- Source manifest status: `experimental-unpublished`.
- Release index: Reader `release-index` commit `d6a81a3b33cc76b6cff7e03db8bfb7765e316776`, tag `driver-display-epd-video-v0.1.3`.
- Canonical `driver.elf`: 5,852 bytes, SHA-256 `7691d63e729d69aad3486a58bf913ddafff5af3cb156a3bcaed59270dcb71f7a`.

The source manifest's experimental status and the observed published release are recorded separately. This mirror matches current Reader master `fa517fea3a882505f10bb432c0c08864b73516c8` and its release index. The published identity is retained as-is.

## Source and interface files

- `Drivers/display_epd_video/driver.c` and `manifest.json` — exact current Reader source.
- `sdk/driver/RiscDisplayOutputV1.h` — display output capability contract.
- `sdk/driver/T5VideoApi.h` — firmware video bridge contract copied from Reader's `lib/NativeApps/include`.
- `sdk/driver/RiscProviderV2.h` — provider-v2 root contract, already byte-identical to Reader at the source commit.
- `test/drivers/display_epd_video_test.c` — upstream host fixture for surface, frame, format, asynchronous completion and failed teardown behavior.
- `scripts/build_display_epd_video.py` — independent Xtensa build, symbol/ELF validation, relocation normalization and canonical release hash check.

## Capability behavior

The driver requires no provider dependencies and advertises a 960×540 output supporting `MONO1` and `GRAY2`, preferring `MONO1`, at nominal 24 Hz. It accepts only rotation zero. Damage is bounded to eight rectangles and translated to one vertical scan interval. An empty damage list requests the full surface. Acquired frames are single-owner; stale frame IDs, invalid rectangles and a second acquire while a frame is held are rejected.

The provider delays backend start until the display owner acquires a surface. It verifies the video API version, structure size, required callbacks, dimensions, format, stride and black-pixel flag. It keeps one pending present token and reports queued or completed status from the backend. Waiting is bounded to at most 250 ms.

Brightness requests return false. Quiescence refuses while a frame is held. If backend shutdown fails, the provider retains the backend pointer and the active lease, rejects restart and new surface operations, and retries shutdown through `quiesce`. The legacy void `stop` callback cannot claim successful release on its own.

## Build and validation

CI compiles and runs the host fixture with warnings treated as errors, then builds the Xtensa ELF with the pinned public toolchain. The builder permits only the two observed video bridge imports (`t5_video_get_api`, `usleep`) and the required C memory helpers, requires `t5_driver_get` as the only global function export, validates the ELF32 little-endian Xtensa shared-object header, normalizes relocations, and compares the result to the published 5,852-byte release digest.

The fixture covers deferred start, capability information, acquisition, damage submission, pending/completed status, quiescence, format changes, malformed surface geometry and failure to stop after normal, failed-start and incompatible-surface paths. It checks that a failed shutdown keeps the provider pinned and blocks a second start until cleanup succeeds.

This establishes host simulation and current-master build/package byte parity. It does not establish panel image quality, timing, refresh behavior, brightness control, physical backend failure recovery, target runtime installation or prospective U1 compatibility. The source remains marked experimental.

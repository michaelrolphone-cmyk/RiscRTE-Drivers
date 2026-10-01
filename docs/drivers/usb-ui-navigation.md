# usb-ui-navigation

## Purpose and scope
`usb-ui-navigation` is an ABI-v2 composite `input.navigation@1` provider. It combines transport-neutral `input.text@1`, `usb.hid.gamepad@1`, and `usb.xinput.gamepad@1` into one UI navigation frame. Lower provider ELFs retain physical transport ownership.

## Package identity
- ID/version: `usb-ui-navigation` 0.1.2
- ABI/architecture: driver ABI 2, `xtensa-esp32s3`
- Source path/tree: `Drivers/usb_ui_navigation`, tree `3ed2c8aa90ea25a052ef914c09b6a913fa5b4d8c`
- Source blob: `79221497c5d1ec1eae471b4b602124bd8530b04c`
- Manifest blob: `d62e276cf0f479e3ab4218153f050c7ebdc821d5`
- Requires: `input.text@1`, `usb.hid.gamepad@1`, `usb.xinput.gamepad@1`
- Provides: `input.navigation@1`
- Source status string: `experimental-unpublished`
- Published tag: `driver-usb-ui-navigation-v0.1.2`
- Canonical ELF: 7,212 bytes; SHA-256 `c907a608f746444831cd31638182d119b198c2e70188177a8cca3d9e4a42e2c7`

## ABI and startup
The driver exports `t5_driver_get` and returns its `risc_driver_v2` only for ABI 2. `start` requires exactly three dependencies in fixed order: semantic text, HID gamepad, XInput gamepad. Text must provide `subscribe`, `unsubscribe`, `poll`, and `next`; both gamepad APIs must provide `poll` and `snapshot`. A second start fails.

`RiscInputNavigationV1.h` defines the provided interface with `poll`, `foreground`, and `reset`, nine action bits, and a maximum of four foreground claims. This header is migrated with the driver because the destination did not previously contain it.

## State and limits
No dynamic allocation is used. The provider tracks four semantic text sources and four device identities for each of the two gamepad sources. Mutable state includes dependency pointers, one text subscription, foreground suppression/gating masks, per-source navigation states, previous combined state, and synchronization-frame state.

## Keyboard policy
Escape maps to Back; Enter and unmodified Space map to Confirm; arrows map to directional actions; Page Up/Down map to page back/forward; Home maps to Home. Held keys are combined across tracked text sources. Disconnect clears a source. A text GAP or negative queue result clears keyboard state and unsubscribes.

## Gamepad policy
HID Button 1 confirms; HID Button 3 (X, mask `0x04`) goes back. Normalized XInput A (mask `0x02`) confirms and X (mask `0x08`) goes back. Neither protocol aliases A/B to Back. Bits `0x10`/`0x20` map to page back/forward. X/Y thresholds of ±16000 and HAT values provide directions. Lost device identities trigger a synchronization frame.

## Foreground handoff
Foreground claims suppress overlapping sources: `input.text`/`usb.hid.keyboard` suppress keyboard; `usb.hid.gamepad` suppresses HID pads; `usb.xinput.gamepad` suppresses XInput; `usb.hid` suppresses keyboard and HID pads; `usb.host`, `usb.controller`, `board.power.vbus`, or `input.navigation` suppress all three. Changed sources are cleared and gated. Keyboard suppression requires successful semantic-text unsubscribe.

## Neutral rearm and frames
After reset or foreground changes, sources are gated to prevent held input from generating synthetic transitions. Keyboard rearming starts from a fresh semantic subscription. Gamepads remain gated until a neutral snapshot. `poll` returns current buttons plus edge-derived `pressed` and `released`; synchronization frames intentionally suppress edges.

## Lifecycle
`reset` clears all sources, marks a synchronization frame, zeroes history, and unsubscribes text if active. `quiesce` delegates to reset and can fail if unsubscribe fails. `stop` only clears dependency pointers after quiescence succeeds.

## Validation
The exact current upstream host fixture checks X-only Back press/hold/release and excludes adjacent A/B Back aliases in both protocols. It also exercises HID/XInput button and direction mapping, Enter/Space semantics, semantic-text foreground suppression while gamepads continue, raw-keyboard suppression, HID-gamepad gating, disconnect synchronization, isolation of a failed gamepad poll, and unsubscribe/quiesce failure/retry behavior. This validates provider logic, not physical USB transport.

## Build/release validation
The standalone Xtensa builder validates the exact manifest, ELF32 little-endian Xtensa ET_DYN format, sole exported function `t5_driver_get`, and exactly one unresolved runtime import, `memset`. The 0.1.2 canonical published target is 7,212 bytes with SHA-256 `c907a608f746444831cd31638182d119b198c2e70188177a8cca3d9e4a42e2c7`. The builder now fails closed if the rebuilt bytes differ.

Published package metadata:
- `.package.json`: 721 bytes, SHA-256 `7363c1e637925f7492dc6e06ce97300d861228bb86f9f77a4abc2062473af1ac`
- `driver.elf`: 7,212 bytes, SHA-256 `c907a608f746444831cd31638182d119b198c2e70188177a8cca3d9e4a42e2c7`
- `provider-abi.v1`: 45 bytes, SHA-256 `550d6acf2cfc1f4a90fe32f3190d532714ec6b245ad4547cd24823c7df2e219c`
- `privileged-imports.v1`: 7 bytes, SHA-256 `1591d47dab39b502bcc6e3e65dc1d466fb29f53433054fe5046f432ab4a6d16d`

## Established limitations
Mapping is fixed in source; there is no remapping API. Capacity is four semantic text sources and four devices per gamepad provider. Dependency order is fixed. The provider exposes only normalized UI navigation, not raw keyboard/gamepad data.

## Current parity provenance

Reader master `1e0188c1ff0234dd33fe054c9a6fb4fde36596df`; immutable release-index `572746f4fcf3fde19947a066b7e5c8028cd76d21`. Source and fixture are byte-identical to this master. Version 0.1.1 → 0.1.2 copies the published update without an additional bump. This refresh does not establish prospective U1 ZIP compatibility, runtime cutover or new hardware qualification.

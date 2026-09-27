# usb-ui-navigation

## Purpose and scope
`usb-ui-navigation` is an ABI-v2 composite `input.navigation@1` provider. It combines transport-neutral `input.text@1`, `usb.hid.gamepad@1`, and `usb.xinput.gamepad@1` into one UI navigation frame. Lower provider ELFs retain physical transport ownership.

## Package identity
- ID/version: `usb-ui-navigation` 0.1.1
- ABI/architecture: driver ABI 2, `xtensa-esp32s3`
- Source path/tree: `Drivers/usb_ui_navigation`, tree `29e1bab462d41429736e3bcc324a1bc7c8aa3d27`
- Source blob: `9489a1aff999bb7cfea335d22b3635d9596f3de5`
- Manifest blob: `13586c3b360376dc05de0cb8e09c85fea3ba7a93`
- Requires: `input.text@1`, `usb.hid.gamepad@1`, `usb.xinput.gamepad@1`
- Provides: `input.navigation@1`
- Source status string: `experimental-unpublished`
- Published tag: `driver-usb-ui-navigation-v0.1.1`
- Canonical ELF: 7,208 bytes; SHA-256 `b7b38cf7d0769081e0e88c75091f59d9a1c3fda45e498a915ce0655d8c2e8631`

## ABI and startup
The driver exports `t5_driver_get` and returns its `risc_driver_v2` only for ABI 2. `start` requires exactly three dependencies in fixed order: semantic text, HID gamepad, XInput gamepad. Text must provide `subscribe`, `unsubscribe`, `poll`, and `next`; both gamepad APIs must provide `poll` and `snapshot`. A second start fails.

`RiscInputNavigationV1.h` defines the provided interface with `poll`, `foreground`, and `reset`, nine action bits, and a maximum of four foreground claims. This header is migrated with the driver because the destination did not previously contain it.

## State and limits
No dynamic allocation is used. The provider tracks four semantic text sources and four device identities for each of the two gamepad sources. Mutable state includes dependency pointers, one text subscription, foreground suppression/gating masks, per-source navigation states, previous combined state, and synchronization-frame state.

## Keyboard policy
Escape maps to Back; Enter and unmodified Space map to Confirm; arrows map to directional actions; Page Up/Down map to page back/forward; Home maps to Home. Held keys are combined across tracked text sources. Disconnect clears a source. A text GAP or negative queue result clears keyboard state and unsubscribes.

## Gamepad policy
HID Button 1 confirms and Button 2 goes back; XInput reverses those two bit meanings. Bits `0x10`/`0x20` map to page back/forward. X/Y thresholds of ±16000 and HAT values provide directions. Lost device identities trigger a synchronization frame.

## Foreground handoff
Foreground claims suppress overlapping sources: `input.text`/`usb.hid.keyboard` suppress keyboard; `usb.hid.gamepad` suppresses HID pads; `usb.xinput.gamepad` suppresses XInput; `usb.hid` suppresses keyboard and HID pads; `usb.host`, `usb.controller`, `board.power.vbus`, or `input.navigation` suppress all three. Changed sources are cleared and gated. Keyboard suppression requires successful semantic-text unsubscribe.

## Neutral rearm and frames
After reset or foreground changes, sources are gated to prevent held input from generating synthetic transitions. Keyboard rearming starts from a fresh semantic subscription. Gamepads remain gated until a neutral snapshot. `poll` returns current buttons plus edge-derived `pressed` and `released`; synchronization frames intentionally suppress edges.

## Lifecycle
`reset` clears all sources, marks a synchronization frame, zeroes history, and unsubscribes text if active. `quiesce` delegates to reset and can fail if unsubscribe fails. `stop` only clears dependency pointers after quiescence succeeds.

## Validation
The exact upstream host fixture exercises HID/XInput button and direction mapping, Enter/Space semantics, semantic-text foreground suppression while gamepads continue, raw-keyboard suppression, HID-gamepad gating, disconnect synchronization, isolation of a failed gamepad poll, and unsubscribe/quiesce failure/retry behavior. This validates provider logic, not physical USB transport.

## Build/release validation
The standalone Xtensa builder validates the exact manifest, ELF32 little-endian Xtensa ET_DYN format, sole exported function `t5_driver_get`, and no unresolved imports, then records canonical size/hash parity.

Published package metadata:
- `.package.json`: 721 bytes, SHA-256 `8ae27aec16e769aab7e6b17ef425d985d2ca8b8985524b2994484a1de343b675`
- `driver.elf`: 7,208 bytes, SHA-256 `b7b38cf7d0769081e0e88c75091f59d9a1c3fda45e498a915ce0655d8c2e8631`
- `provider-abi.v1`: 45 bytes, SHA-256 `550d6acf2cfc1f4a90fe32f3190d532714ec6b245ad4547cd24823c7df2e219c`
- `privileged-imports.v1`: 7 bytes, SHA-256 `1591d47dab39b502bcc6e3e65dc1d466fb29f53433054fe5046f432ab4a6d16d`

## Established limitations
Mapping is fixed in source; there is no remapping API. Capacity is four semantic text sources and four devices per gamepad provider. Dependency order is fixed. The provider exposes only normalized UI navigation, not raw keyboard/gamepad data.

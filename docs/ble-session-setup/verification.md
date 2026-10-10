# Local verification checkpoint, 2026-10-10 UTC

This is an experimental, unpublished provider. No RF/device action, Reader change, Runtime change, frozen-package edit, signing, release publication, or installation was performed.

## Source/version checks

- Reused source base: Drivers `58ad37140bd86bd2247bfa3244c4acc8942b2bd8`, fetched branch `feat/ble-hid-two-axis-scroll`.
- Pinned Apache NimBLE source: `da7e3256da3ba80b232df880f40d8359311cc62e`, complete vendored file/patch inventory checked by the builder.
- Live Drivers refs were read with `git ls-remote --heads --tags origin` on 2026-10-10. Main was `179979b94cc5a1d4787c0ad99515c5c07edd92d8`.
- The live GitHub contents API returned no `Drivers/ble_session_setup/manifest.json`. The readable current `manifest/released-drivers.json` (blob `fc59495eb96f8147e578467dcb7f575764729dcf`) had no `ble-session-setup` entry. Accordingly `0.1.0` is a new experimental source identity, not a claim that a released package exists.
- Existing live main HID manifest is `0.1.2`; the intentionally selected reusable HID feature branch is `0.1.4`. Neither was restamped or published.

## Normal and sanitizer results

Both the normal build and ASan/UBSan build run the production NimBLE stack against an independent deterministic HCI central. The central's Secure Connections ECDH and AES-CMAC calculations use OpenSSL through `cryptography`. Fixtures operate entirely in process and never contact a Bluetooth device.

Passing scenarios cover:

- Numeric-comparison pairing with independently verified f4/f5/f6/g2; copied immutable descriptor; authenticated ATT reads and full long-read reconstruction
- Maximum-size descriptor, invalid long-read offsets, write denial, reads denied before numeric acceptance and before encrypted authentication
- Wrong pairing generation/number, stale native handles/tokens, wrong DHKey check, invalid public key, short-key pairing and legacy downgrade rejection
- Completed Secure Connections Just Works rejected because it lacks explicit authenticated numeric comparison
- Setup deadline independent of longer WebDAV deadline, WebDAV shorter deadline cap, 32-bit clock wrap, expiry during a long read, and explicit zeroing of all provider-owned descriptor bytes
- Disconnect ends the session; the old LTK cannot resume a new session; new pairing succeeds
- Native receive failure, zero/retained failed claim, retained close blocking every ordinary operation, checked-close retry, quiescence and 20 immediate close/reopen cycles
- Reference-central decoder rejects malformed lengths, unsafe endpoint URLs and expired/inconsistent validity snapshots

Commands: `bash test/run_ble_session_setup_test.sh`, `SANITIZE=1 bash test/run_ble_session_setup_test.sh`, `python scripts/check_driver_docs.py`, `git diff --check`.

## Xtensa target and preservation evidence

Compiler: `xtensa-esp32s3-elf-gcc (crosstool-NG esp-2021r2-patch5) 8.4.0`.

The target builder validates an ELF32 little-endian Xtensa ET_DYN file and records source revision, dirty state, pinned upstream source, port file hashes, output hash and section sizes in `dist/ble-session-setup/build-record.json`.

- ELF file: 215880 bytes
- SHA-256: `22d6e37fb39ad3f29a64ed898503a747fb29cff0965c7c17291acdc288c3f625`
- `.text`: 73048 bytes; `.rodata`: 920 bytes; `.bss`: 40004 bytes
- Sum of ELF SHF_ALLOC section sizes: 151444 bytes. This includes loader metadata and relocations; it is not a claim about actual combined-product peak memory or a graph-capacity admission result.
- The existing fixed 24576-byte cooperative allocation arena is part of BSS. Descriptor storage is a fixed 464 bytes.
- Dynamic imports: `memcmp`, `memcpy`, `memmove`, `memset`, `strcmp`, `strlen`
- Sole dynamic export: `t5_driver_get`

The same compiler rebuilt HID from this worktree and from the pinned source without modifying any HID, telemetry, sensor, port, vendor, or existing HID-builder file. Both HID target artifacts are 233272 bytes with SHA-256 `5db0e3dd59a12cfd33f0934777d8ed32e6a24768ca6bb667212aa3649aeb59f7`.

The CI workflow added for the provider runs normal/sanitizer scenarios and the target/import check. It has not been published or run on GitHub at this local checkpoint. OS pairing UI, real central interoperability, RF behavior and combined WebDAV/Wi-Fi/BLE target performance remain unverified.

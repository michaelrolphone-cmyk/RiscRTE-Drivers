# Bounded PR49 source integration

This correction preserves PR49 `73ef8caefbfe8aff0ec1aa52b1e410257562001f` provider sources, original release inventories and source locks. It integrates only the exact recovery and applicable preservation/custody mechanisms from PR46 `c1221ed4a609adc610f77166b0e3bb2d38232922`.

- Badge recovery restores verified Git object packs and original commit/tree identities, including Runtime `046f4c22b263739f3bf3f96f888617d72eb3d498`. No latest-ref replacement is allowed. Existing workspace roles are refused before mutation.
- The separate Home System role uses the existing original System pin. Existing selected product pins and workflow flags are unchanged.
- SDK preservation permits only three exact published additive headers and the reviewed round-display enum extension. Historical HID/host source, SDK baseline and inventory locks remain unchanged. Unknown SDK additions, byte/mode changes and symlinks are rejected.
- Nine existing legacy, PCM, native-CDC and time-sample directories have exact local custody declarations. Their immutable original provenance and complete current trees are validated. These are not upstream-parity or released-package claims.
- The seven other prototype directories, BLE sensor 0.2 source migration and unrelated upstream inventory/source/version conflicts remain visible. No absent PR46 providers or incompatible qualification receipts are imported.

## Reproduction and limits

Run the recovery pack/role and existing delivery tests in `tests/`, SDK/CST/local-custody tests in `test/parity/`, and `test/test_custody_provenance_recovery.py`. The owning workflows run exact fresh recovery and custody hydration before their consumers.

The retained failing parity observation uses Reader `9c8783d6f20659a514bfaeb3847a77c7213dc4f6` and release index `19bb3dcb5cb763c4eacd884ad5fc0bcab57ff2ed`. The nine custody declarations remove only their exact untracked-directory diagnostics: 74 baseline errors become 65. All other diagnostics and source/release differences remain failures.

Host tests and source restoration do not establish target build, firmware activation or physical hardware qualification. The complete owning Linux parity suite remains an exact-head CI gate; its non-UTF8 filename fixture cannot be represented by the available macOS filesystem. No fixture weakening or Linux-equivalence claim is made.

# Actual Settings timer execution qualification

This four-case suite is additive to, not a replacement for, original42 execution, staged8 timing, Springboard4 gap recovery and target admission. Use `scripts/test_badge_settings_execution.py` with the original explicit source/store arguments plus `--foreground --settings-ui`, adding `--sanitize` for ASan/UBSan.

Ordered actual case0 routes:

1. `automatic`: visible Idle Save10s and Deep Save120s; reopen each; Home reads both actual records and automatic idle enters Light120s then timer-only Deep.
2. `manual`: visible Idle Save15s and Deep Save60s; reopen; actual physical short button enters Light60s then Deep.
3. `cancel-time`: visible initial saves, idle draft Cancel, Deep draft physical Back, reopens unchanged values; Set Time year draft Cancel then actual year2027 Save and native readback; completed returned Home.
4. `io-retry`: one actual storage-medium IO refusal during visible idle Save, no durable mutation; completed unconfirmed/error frame, explicit physical adjustment/retry saves10s; Deep120s saved separately and manual Home follows it.

Neither successful timer value nor a sleep mode is preseeded. Source-defined controller rectangles and observed viewport dimensions translate physical input; logical240 coordinates are not scaled. Namespace1 timer records are decoded and writes observed in actual Settings. Home must read both exact records after return. Automatic timing bounds and native timer-arm requests must match the visible saves.

Before each real timer write, read-only instrumentation observes actual pending-text pixel replay (not record-only early state), then a completed native frame after the Save attempt. Scenario/op/phase raster names distinguish pending/error/reopened timer/Home frames. Known IO is recoverable: the selected profile reloads the old/default value with unconfirmed feedback, and the retry is explicit. Do not call IO terminal custody loss. Actual retained-fault and no-later-callback contracts remain separately covered by owning tests; this suite does not fabricate uncertain-custody callback results.

Receipts use schema1 / qualification `badge-settings-timers`, exactly four ordered results with real return codes/stdout/stderr, canonical target build digest, store/pin identity, full input source hashes and five relative fixture hashes (runner, fixture, helper, reused alarm/quick includes). Require settings_ui=true, expected_regression=false, both normal/sanitized lanes and exact matching inputs. Isolated development receipts are preserved but must not be relabeled as owning-run proof.

Hardware/time/input/storage are modeled. No target-instruction execution, physical device operations, physical sleep current or boot reliability is established. Test-only copies do not alter selected app/provider behavior; no successful UI save is replaced by a backend assignment outside its actual storage callback.

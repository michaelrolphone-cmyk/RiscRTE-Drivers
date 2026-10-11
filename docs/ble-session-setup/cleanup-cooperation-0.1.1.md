# Scheduler-only cleanup successor, 0.1.1

This focused source successor leaves the published 0.1.0 snapshot and its [original verification receipt](verification.md) unchanged. It has not been published or selected into a product. No device action was performed.

## Cause and required native contract

Repeated negative native releases in 0.1.0 retained the correct token/dependencies but could return immediately after NimBLE stopped, creating a tight cleanup retry loop. The owning app cannot use ordinary Runtime yield in that state because it polls unrelated providers.

The selected pre-successor Runtime `1be501bd69b0ab21a3acaeacaa382796f284e0e1` was inspected directly. `CpuPort.cpp` binds legacy `platform.clock.sleep_ms` to `Hardware.sleep`; `NativeHardware.cpp` runs `RiscDiagnostics::poll()` before `vTaskDelay` when `RISC_DIAGNOSTIC_ADAPTER` is enabled. `SleepDiagnostics::poll` guards task ownership/outputting and optional USB fencing, not retained HCI custody. That operation is not scheduler-only and was not used as a fallback. Existing diagnostic settings and behavior remain intact.

Provider 0.1.1 requires the additive [RiscPlatformClockWaitV1.h](../../sdk/driver/RiscPlatformClockWaitV1.h) suffix. Its original `risc_platform_clock_api_v1` base is unchanged. Size, API version, tag `0x43575431`, suffix version 1 and a nonnull callback must all validate before provider admission. Prefix-only or malformed clocks fail before any native claim or clock call.

The shared header's SHA-256 is `c09d95c418f930f56c6e66004e07e38cb5726d8c7e5cd49b579bafbeebbe831c`. The coordinated Runtime 0.2.6 implementation binds the suffix to a separate owner-checked function performing one direct `vTaskDelay`, rounded up to at least one tick, for 1–50 ms. It performs no diagnostic, provider, USB, storage or radio work. Its native code is in `src/ports/esp32s3/NativeSchedulerWait.inc`; the provider uses the exact shared header. Native target qualification is recorded separately by the Runtime task, not inferred from this provider fixture.

## Resulting cleanup behavior

- The existing bounded host-stop wait loop uses the new one-millisecond scheduler-only operation.
- Each negative native release is followed by exactly one such wait before returning retryable `CLEANUP_PENDING`.
- Once native release has been attempted, later cleanup calls never restart NimBLE, HCI receive/send, timers or ordinary polls, even if the earlier host stop was incomplete. They retry native release and cooperate only through the new wait.
- Tokens, module and dependency custody remain retained until checked native release succeeds.
- Explicit rejection of a valid one-millisecond scheduler wait latches terminal `RETAINED=-3`. All subsequent APIs reject without host/clock access; `quiesce` stays false and `stop` cannot unload. This is based on the owner-checked callback's actual rejection, never inferred from generic BUSY or a transport failure.

## Proof

The original 0.1.0 implementation fails the new deterministic regression: 64 refused cleanup retries produce 64 release calls and no scheduler calls. The failure is reproduced both normally and under ASan/UBSan.

On 0.1.1, all 25 production NimBLE protocol scenarios and the reference-central decoder check pass normally and under ASan/UBSan. The focused additions prove:

1. Prefix-only/truncated/wrong-tag/wrong-version/null-callback clock tables reject before radio claim or clock work.
2. Sixty-four consecutive native refusals and a refused quiesce each produce exactly `release(-1), scheduler_wait(1)`, with no HCI/ordinary clock callbacks. A later successful native release ends custody without an extra wait.
3. An incomplete host stop uses scheduler-only waits and cannot restart the host pump after native refusal.
4. Explicit scheduler rejection latches terminal retention; repeated close/poll/status/confirm/open/start/stop/quiesce calls remain inert and cannot unload, even if fixture ownership is subsequently restored.

Commands: `bash test/run_ble_session_setup_test.sh`, `SANITIZE=1 bash test/run_ble_session_setup_test.sh`, `python scripts/build_ble_session_setup.py`, `python scripts/check_driver_docs.py`, `git diff --check`.

Fresh version check on 2026-10-10: live `work/ble-session-setup` was `9db618272b9cc79af643c342f0d4fffd4d9e75e7`, whose readable manifest claimed 0.1.0 (blob `9fcf2b1f59f693b1bce06691cae6b42b4079e7e3`); main still had no provider manifest. The live ref listing contained no separately named 0.1.1 claim. Accordingly this source reserves the new 0.1.1 identity without changing the original receipt.

## Target artifact

Compiler: `xtensa-esp32s3-elf-gcc (crosstool-NG esp-2021r2-patch5) 8.4.0`.

- ELF32 little-endian Xtensa ET_DYN: 216316 bytes
- SHA-256: `3f1c59c281d5303e7395b64bcd1eb32ee8020fc471227b1f43a2e4dc12159011`
- `.text`: 73432 bytes; `.bss`: 40012 bytes
- SHF_ALLOC section-size sum: 152036 bytes, not a combined-product peak-memory claim
- Dynamic imports remain `memcmp`, `memcpy`, `memmove`, `memset`, `strcmp`, `strlen`
- Sole dynamic export remains `t5_driver_get`

HID source, its shared port, NimBLE source, legacy clock header, Reader, Runtime source in this repository and frozen products are unchanged. The build record beside the ELF identifies the final clean source commit and source hashes. Physical/native integration remains the responsibility of the separate Runtime/product qualification.

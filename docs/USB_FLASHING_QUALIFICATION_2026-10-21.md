# USB flashing qualification, 2026-10-21

## Result

A bounded software repair is prepared in RiscRTE-Drivers, branch
`tasker/usb-flashing-cleanup-20261021`, based on
`a7ae39994603c4545a3c014f09051ecfc12084d2`. `program-msp` changes from 0.1.0 to
unpublished 0.1.1. Its failed-open cleanup now preserves transport ownership
until the underlying close succeeds. No public ABI or target programming
sequence changes.

This is not delivery or physical acceptance of an X4 flashing tool. No USB
port/device was opened, target programmed, radio operated, product pin changed,
accepted artifact overwritten, source pushed, PR opened, or release published.

## Exact composition separation

- X4 source inspected: `6b7d1abe9b2dcc236f6086d07c0f619244c907cb`.
- `migration-source.json` pins Reader
  `34d8e694d89a1e72d8854403d8592c289fae3ddc`, tree
  `e7dc2a45c3553f0102a7c2e2c2c9fe5bc51b00a2`. The X4 README explicitly labels
  that build **historical migration**, separate from the native candidate.
- Native 0.1.123 source inputs instead pin Runtime
  `6a3b3d46f5b2857eacc6e9863799dec1f466b036`, Drivers
  `5890721073ebabd781d5d278e447c761947a5c58`, System
  `0651a55513b0aa0dff1b6a9072303def08a97378`, Utilities
  `e80172353fcb9e6519ca9d9b2a4d43c69c1bc91d`, and Reader
  `c98e4222048244b1e729869ed47aa99130025fd3`. No mixed-source build is claimed.
- The historical Reader contains Firmware Flasher 1.1.2: merged ESP image
  programming through the resident `NativeEspRomBridge` and optional addressed
  MSP430FR TI-TXT programming through `program.msp`. This is reusable historical
  implementation evidence, not proof the current native runtime selects it.
- The inspected native baseline inventory has 43 ELFs, including USB mass-storage
  / SD transfer, but no identified Firmware Flasher/program-msp/USB-host stack.
  Device-side file transfer does not imply USB-host target programming.
- Current Drivers has `program-msp` FRAM programming and `usb-msp` transport.
  `usb-stlink` is a transport, not a complete STM32 flash algorithm. No broader
  target-family support is inferred.
- The `program_msp/driver.c` baseline blob
  `465bfc6d0dff743d49c4cda730aee1b71fd8fc68` is identical in native X4's Drivers
  pin and inspected Drivers main. This repair is not inferred from a serial ABI
  difference between unrelated compositions.

The X4 host stack is not selected and its board-specific VBUS/role electrical
contract remains unestablished. This work does not invent a pin, power source,
current limit, electrical sequence, source-off behavior, or safe test topology.

## Reproduced defect and correction

`RiscMspFetV1.h` explicitly says a failed `close` retains the transport session.
Previously the programmer ignored failed close after rejected acquisition,
forgot the token, and reported `quiesce=true`; unload could therefore discard
ownership of a live transport.

The new negative control fails on the baseline shared object at
`assert(!driver->quiesce())` (exit 134), and passes on the corrected object.
The correction reserves a token-zero, transport-nonzero slot until close
succeeds. Allocation excludes retained slots. Each quiesce call tries at most
one close per retained slot (two slots maximum). `stop` does not discard the
bound dependency while cleanup fails. Successful/public sessions keep their
existing close behavior. There is no new target operation or destructive retry.

The committed C regression includes six failed-open paths: protocol, identity,
unsupported target, configuration, fuse check, and synchronization. It also
checks distinct probe-7/probe-8 transport handles, per-token close failures,
partial cleanup, no second close after successful release, reclaimed-slot reuse,
stop/dependency retention, and stale public-token rejection across restart.

## Verification and limits

Passed:

- Eleven baseline host fixture executions: four historical ESP ROM codec/session/
  production-provider/context fixtures, historical Flasher pagination, historical
  MSP programmer/MSP transport/ST-LINK transport, and the three corresponding
  Drivers provider fixtures. These are simulations; deterministic digest shims
  do not test MD5 cryptography or physical flash contents.
- Corrected MSP regression and sole-export check, normally and with ASan/UBSan.
- Independent reviewer reproduction, corrected fixture replay, and distinct-token
  fault injection; the stronger ownership assertions are now in the C regression.
- Parity tooling: 30 passed, 3 skipped (33 discovered). These include two new
  compiler-free source-version/historical-replay route contracts.
- Compiler discovery tests: 21 passed, 2 real-toolchain tests skipped.
- Driver-document check (21 indexed migrated packages), workflow YAML parse,
  builder Python compilation, and `git diff --check`.

Not passed / not run:

- The target builder was invoked and stopped before compilation: no Xtensa
  compiler is installed. No target ELF, firmware, or package-byte qualification.
- LeakSanitizer aborts under this environment's ptrace. ASan/UBSan ran with
  `detect_leaks=0`; no LeakSanitizer pass is claimed.
- No full firmware build, whole-product qualification, remote CI, hardware
  programming, reset/readback acceptance, or end-to-end current-X4 flashing.

## Unresolved transport recovery

`usb_msp/driver.c` clears detached sessions in `poll` (lines 223–229 in the
baseline), then rejects `close(old_token)` (lines 664–665). The actual transport
fixture reproduces false close repeatedly after its own quiescence succeeds.
The corrected programmer can therefore remain safely quarantined indefinitely
when the transport has already erased the token. Normal open-session cleanup
has the same underlying recovery limitation. Host tests here establish retry
recovery only when `close` later succeeds, not full detach/replug recovery.

The transport blob `a64cf0e2748e83897be96fcaabf2559e27c36463` is identical in
Reader `9c8783d6f20659a514bfaeb3847a77c7213dc4f6` and `34d8e694`, Drivers
`a7ae399`, and X4's Drivers pin `5890721`. No coherent newer fix was found in
those inspected snapshots. Retired-token and host-claim lifecycle reconciliation
is a separate unresolved issue, not an excuse to declare failed close successful
or redesign the ABI speculatively in this patch.

## Version, source custody, and release replay

The accepted 0.1.0 9,128-byte digest remains
`19b0999f41e45522e877097addf3cfd55651b2fd00ae925fa6084b769b66527f`.
The builder expects source 0.1.1 but labels its historical comparison as 0.1.0.
Historical CI now checks out immutable Drivers `a7ae399` under
`.source/legacy-program-msp` and invokes that unchanged producer with
`--require-byte-parity`; its output is separate from the new source build.
Neither target route ran locally; compiler-free workflow checks are not byte
replay evidence.

The retained release inventory and upstream baseline are unchanged. Original
source tree `fe046f4c538efbd003ddd176b4d4b44b5d3de281` differs from candidate
`9737500af54250498a7b901d5162f6d8d5a166dc`: deliberate `external-only` drift.
The whole-source parity gate is not claimed green and must be reconciled under
source-custody/version policy before publication. Do not merely refresh a hash
or overwrite the accepted 0.1.0 artifact to hide the difference.

## Reproduction and evidence

Run from the Drivers repository:

```sh
bash test/run_program_msp_test.sh
SANITIZE=1 ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 \
  UBSAN_OPTIONS=halt_on_error=1 bash test/run_program_msp_test.sh
python3 -m unittest discover -s test/parity -v
python3 -m unittest discover -s test -p test_native_driver_toolchain.py -v
python3 scripts/check_driver_docs.py
```

`docs/evidence/usb-flashing-2026-10-21/` contains baseline/final logs, original
negative-control source and failure, the detached-token observation fixture,
composition and source-drift receipt, missing-compiler output, and the initial
unsupported LeakSanitizer result. The negative control can be compiled against
`sdk/driver` and run against a host shared library built from
`git show a7ae39994603c4545a3c014f09051ecfc12084d2:Drivers/program_msp/driver.c`.
The same control must pass against this branch's shared library.

Remaining work: reconcile the lower transport lifecycle contract and native
product composition; establish the X4 root-host/VBUS contract; qualify the exact
versioned target build and source custody; then separately obtain authorized
publication and actual supported-target programming/readback evidence. This
local source increment does not mark the overall flashing outcome completed.

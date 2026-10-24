# USB host HID: local qualification and X4 composition boundary

## Result

Eight distinct existing host fixture bodies pass normally and with AddressSanitizer
and UndefinedBehaviorSanitizer. No production driver, SDK header, manifest,
product selection, accepted artifact or firmware byte changed. This checkpoint is
**offline source qualification, not an X4 USB host delivery or physical acceptance**.

The outcome criterion is: supported USB host HID peripherals enumerate and deliver
input through shared interfaces. Existing keyboard/gamepad source and tests satisfy
the simulated class/provider portion, not the full product or hardware criterion.

## Exact source identity

Read-only `git ls-remote ... refs/heads/main` confirmed these heads during this pass:

- Drivers: `a7ae39994603c4545a3c014f09051ecfc12084d2`.
- X4 product: `6b7d1abe9b2dcc236f6086d07c0f619244c907cb`.
- Runtime: `13eca114cb58469cf6004fafc3da84fbd9d29beb`.

The isolated qualification checkout starts at the exact Drivers head, not another
task's local BLE changes. [Source evidence](evidence/usb-hid-host-20261021/source-composition.json)
records Git trees, blobs, byte lengths and SHA-256 for 68 driver/ABI/fixture inputs
and 10 product files, plus the Runtime native-controller policy document.

Existing packages are `usb-host-v2` 0.1.3, `usb-hid` 0.1.2,
`usb-hid-keyboard` 0.1.1, `usb-hid-gamepad` 0.1.4,
`usb-hid-text-input` 0.1.0, `usb-xinput-gamepad` 0.1.3 and
`usb-ui-navigation` 0.1.2. Their per-driver documents and released-drivers manifest
record historical upstream publication/parity. This pass did not redownload those
artifacts or reproduce target bytes; historical publication is not new delivery.

## Reproduce the bounded checks

From this repository, using a C compiler and Bash:

```sh
bash test/run_usb_hid_host_qualification.sh plain /tmp/usb-hid-plain
ASAN_OPTIONS=detect_leaks=0 bash test/run_usb_hid_host_qualification.sh sanitized /tmp/usb-hid-sanitized
bash -n test/run_usb_hid_host_qualification.sh
git diff --check
```

The runner prints exact compiler commands and compiler version; logs are retained
in [plain.log](evidence/usb-hid-host-20261021/plain.log) and
[asan-ubsan.log](evidence/usb-hid-host-20261021/asan-ubsan.log). GCC 14.2.0 compiled
with C11, `-Wall -Wextra -Werror`; sanitized runs additionally use
`-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer`.

Coverage:

1. Host generation-qualified discovery, endpoint gates, failed-release retention,
   quiescence and stale tokens against a fake controller.
2. Generic HID plus real keyboard/gamepad shared objects, with fake host I/O:
   descriptor parsing, input subscriptions, snapshots and disconnect.
3. Still-attached shutdown: release class claims before dependent ELFs unload.
4. Keyboard boot reports, modifiers, subscriber order, gap and snapshot handling.
5. Gamepad descriptor/report IDs, unrelated inputs, collections, delivery and detach.
6. Keyboard-to-transport-neutral text translation and lifecycle.
7. Navigation mapping, semantic-text focus and gamepad coexistence/cleanup.
8. XInput discovery, bounded startup retry, current state and hotplug/shutdown.

The initial exploratory pass executed nine test paths. The gamepad descriptor
fixture is byte-identical to the gamepad test and is not independent coverage;
the final runner executes eight distinct bodies in each mode (16 successful runs).
These remain separate fake-boundary fixtures, not one real-controller-to-UI test.

The first sanitizer attempt stopped because LeakSanitizer cannot operate under
this environment's ptrace. Its [failure log](evidence/usb-hid-host-20261021/lsan-environment-failure.log)
is preserved. The explicit `detect_leaks=0` rerun keeps ASan and UBSan enabled.
No leak-check pass is claimed. A read-only shell launch also failed before process
creation with an exec-server transport disconnect; subsequent local reads succeeded.

## Delivered and current X4 composition

X4's committed `docs/native-idf61/released-baseline.json` records release
`test-t064-crash-0.1.121`: factory SHA-256
`48145adc023a334885207100ce29596edef3f02c07a6d830ca53e3ab1fbf5993`,
Runtime `f734d91780cdb4f1ba36fe82dcbd8fa28f25bb2b` / 0.2.28,
107 internal files / 43 ELFs and 77 SD paths / 49 ELFs. The historical native ELF
and complete composition JSON are explicitly unrecovered. These are recorded
source receipts, not fresh binary inspection in this pass.

The misleadingly short internal `hid/driver.elf` path is the **BLE HID** package:
its SHA-256 `6290d9cdfae051b9f2f2c83060f1c4000a62f727d47b680c67cd77b0d9e0c555`
matches the SD `Drivers/ble-hid/` entry. `usb-msc/driver.elf` is
`usb-device-msc-esp32s3`, used for X4 acting as a device toward a computer.
Neither establishes host HID support. The recorded internal/SD inventories contain
no selected host/HID class stack.

The current product's SDK6 packaging recipe explicitly preserves device MSC and
forbids enabling a private USB host-controller policy. Its simple board profile,
profile generator and Drivers X4 load-order inventory also omit host/HID providers.
Runtime's separately pinned native controller policy is opt-in and not proof that
the product selected it. Existing canonical SDK6 work is unchanged by this check.

## Exact blocker to safe X4 integration

The shared controller requires `board.power.vbus@1`. X4's board provider exports
`board.power.ready@1` for peripheral enable; its power-key driver exports
`x4.power@1`. Neither is a VBUS provider. No compatible X4 VBUS implementation was
found in the inspected board source.

`sdk/driver/RiscUsbVbusV1.h` requires board-proven source-current limits,
external/absent/source observation, hardware-verified source-off and restored
state, and retained leases after partial/failed transitions. External-powered
hosting additionally requires a verified route, voltage and source-off state.
X4's battery source explicitly says GPIO21 is **charging only**, not VBUS presence;
its documented source evidence says VBUS detection is not conclusively identified.
GPIO1 peripheral-enable readiness cannot substitute for this electrical contract.

Before implementation, authoritative X4 board source/schematic or measured evidence
must establish the VBUS detector and its states, host/source switch or external-only
route, safe current limit, source-off readback, transition/settling behavior and
ownership/cleanup contract. No pin mapping, permissive fake power provider or
unverified electrical policy is invented here. Without those facts, a local
production adapter would not be a correct integration, even if a target linked.

Then a separately reviewed product composition must select the compatible
controller/cohort, host/HID providers and application bindings; run exact-source
target/admission checks; and obtain authorized physical enumeration/input,
detach/reconnect and cleanup evidence. No such device operation was performed.

## Original mouse scope and bounded source search

The original request was subsequently recovered, message
`Sentinel_69c5b163d0ec81918e99e3f855dcbcf7`, 2026-10-09T07:24:32.259417+00:00:
the owner explicitly requested X4 mouse, keyboard and game-controller USB support,
reusing existing drivers and supplying the necessary X4 root drivers. Mouse is
therefore requested scope, not an inferred extra requirement. Generic raw HID
reports alone do not establish that semantic mouse input has been delivered.

A separate read-only source copy was fetched at canonical Reader master
`9c8783d6f20659a514bfaeb3847a77c7213dc4f6`, tree
`66a1bb85173858d3630e5e081abd608288ceff90`. Complete tracked-source searches for
mouse identifiers and mouse-related filenames did not locate a host mouse class.
`docs/USB_HID_ELF.md` and `sdk/driver/RiscUsbHidV1.h` define generic HID,
keyboard and gamepad, with no mouse-specific interface. Legacy architecture
mentions refer to candidate **device-mode** mouse emulation, not a host provider.
The existing locally inspected Utilities mouse code sends BLE reports; it is not
host USB parsing. No `input.mouse`/`input.pointer` contract was found in the
inspected Drivers, Utilities and System Apps interfaces.

This establishes a bounded source-recovery gap, not proof that no implementation
ever existed on another branch or hardware project. The exact earlier mouse
provider and consumer contract have not been recovered. A generic mouse class
could be developed and host-tested above `usb.hid@1` independently of X4 VBUS,
but without that source it would require a new explicitly versioned semantic
consumer contract, not simply copy a known provider. No new port was started.

Serial monitoring, flashing, Bluetooth changes and accepted Game Boy behavior
remain outside this pass. The original request explicitly keeps multitouch from
blocking Game Boy binary delivery.

## Independent replay

An independent review at 10:55 UTC reran the final runner: all eight distinct
fixtures passed normally and under ASan/UBSan with leak detection disabled.
Its exact commands/output are retained in
[independent-plain.log](evidence/usb-hid-host-20261021/independent-plain.log) and
[independent-sanitized.log](evidence/usb-hid-host-20261021/independent-sanitized.log).
These are a replay of the same coverage, not additional test scenarios.

## Disposition

Local qualification deliverable finished; the outcome remains awaiting integration
and hardware qualification. No push, PR, merge, tag, release, catalog change,
product-pin change, target flashing or physical USB operation occurred.

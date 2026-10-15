# Native USB producer qualification (2026-10-15)

Status: bounded source/ELF candidate passes compilation and host fault injection;
**not hardware-qualified or packaged for release**. The existing 0.1.19 manifest,
release files and legacy private ABI v1 remain unchanged. The new candidate must
receive a separately versioned native private-import contract and SDK-cohort
admission before deployment. Public driver ABI 2 and USB capability APIs do not
change; public driver ABI 2 is distinct from that private import-contract version.
Qualification metadata identifies the candidate as experimental **0.1.25**;
the existing source manifest remains 0.1.19. A prerelease suffix such as
`0.1.25-idf61` is deliberately avoided because Runtime `safeVersion` accepts
only numeric major.minor.patch.

The fixed native cohort identifier is
`esp32s3-idf6.1-gcc15.2.0-20251204-v1`. It denotes the exact SDK/toolchain
contract below, not a version range. `os_cpu_abi: 2` selects the new private
resident import ABI; `provider_abi: 2` continues to identify the public driver
root layout.

## Exact inputs

- Drivers baseline `422e1e81a498fb6bdab9abf086325afc05a532dc`, selected by the
  released Wave 0.1.48 manifest. Released USB 0.1.19 source subtree:
  `15c27125c163a50e1d41d7e9b15194ea03680488`.
- Official [ESP-IDF source](https://github.com/espressif/esp-idf/tree/fff9895c82d744c7237be8847347bdd1b07c6643):
  `fff9895c82d744c7237be8847347bdd1b07c6643` (6.1).
- Official [esp-usb source](https://github.com/espressif/esp-usb/tree/0f6820940f9e8075c0cc1483d9c2f5e89cc5b97a/host/usb):
  `0f6820940f9e8075c0cc1483d9c2f5e89cc5b97a`, `host/usb` version 1.6.0.
- Xtensa GCC 15.2.0, tool distribution `esp-15.2.0_20251204`.
- Release-size optimization `-Os`; debug `-Og` is a different import closure
  and is rejected by this qualification builder.
- Exact S3 target arguments come from the native Runtime compile database;
  its compiler response file, including `-mdisable-hardware-atomics`, is expanded
  unchanged. `qualification.json` records compile-database, sdkconfig, source,
  adapter and builder hashes; `commands.json` records each object invocation.

The builder rejects different/modified SDK and component revisions, incompatible
USB config, a different compiler version, changed transformation anchors,
unexpected imports/exports and unsupported loader mappings. It performs no
download or full firmware build.

### Trusted-policy generator inputs

The generated `qualification.json` schema is version 1. A native policy generator
must compare its fixed expected identity and contract against these fields:

- `schema_version: 1`, `driver_id: usb-controller-esp32s3`, `version: 0.1.25`,
  `file_name: driver.elf`, `architecture: xtensa-esp32s3`, `target: esp32s3`.
- `provider_abi: 2`, `os_cpu_abi: 2`, `native_cohort` exactly as above;
  `idf_commit`, `usb_commit`, `usb_version`, `compiler: 15.2.0` and
  `compiler_distribution: esp-15.2.0_20251204` exactly matching these pins.
- `requires: [{capability: board.power.vbus, api: 1}]` and
  `provides: [{capability: usb.controller, api: 1}]`, each with exactly one entry.
- `imports`: the exact sorted 44-name set; `size` and `sha256`: independently
  recomputed from the ELF; sole export and loader-map success independently
  rechecked. Qualification metadata alone is not authority to admit a binary.
- `sdkconfig_sha256`, `compile_commands_sha256` and `source_hashes`: recompute
  against the supplied configuration/source inputs. `usb_config` records the
  reviewed host configuration; reject extra capabilities or config changes.
- `optimization: -Os`; source/config/response-file hashes are captured before
  compilation and checked again afterwards to reject mixed-input builds.
- `abi_profile`: the exact 33-field macro/layout contract in the builder's
  `ABI_PROFILE`, independently asserted by generated target C++ assertions
  against `ABI_EXPRESSIONS`. `abi_profile_sha256` hashes its canonical JSON
  (`json.dumps(profile, sort_keys=True, separators=(',', ':')).encode()`).
  The fixed digest is
  `c880590cd8c5ba64f091d5bef7b2499353755bca582f15533509c638e8efb544`.
  This contract is the ABI-sensitive gate; the whole SDK config hash records
  product build provenance and must not substitute for these field checks.

`status` stays `experimental-native-candidate-not-hardware-qualified`.
`normalized_iram_sections` and `loader_map` record the normalization and mapping
evidence. The builder verifies the unchanged source manifest's identity and
dependency set before building, and checks the compiler distribution banner.

## Private implementation and lifecycle

IDF 6.1 moves USB host out of IDF. The provider links seven pinned
`esp-usb/host/usb/src` files (`hcd_dwc`, `enum`, `hub`, `usb_helpers`, `usb_host`,
`usb_private`, `usbh`) plus IDF's private PHY, USB DWC/WRAP/Serial-JTAG HAL,
S3 peripheral descriptors and GPIO table. The legacy `phy_gpio.c` shim is not
part of this closure. No USB host, HAL, PHY or GPIO implementation is imported
from Runtime. SDK structures stay private to the provider.

- `NativePhyGate.h` replaces removed `usb_phy_action` with the exact S3 WRAP
  receive-detector test sequence from IDF 4.4.7. It does not change VBUS, pulls,
  or PHY routing. The legacy 4.4.7 branch still calls `usb_phy_action`.
- `HostStartup.h` explicitly selects native OTG peripheral bit 0 and preserves
  PHY hold, host/client/DMA readiness, role-settle delay, power acquisition,
  then receiver-enable order. Only `board.power.vbus@1` is required;
  `platform.usb.phy.resource` has not been added.
- `NativeIrqLifecycle.cpp` requires one initially disabled, non-shared,
  non-IRAM USB IRQ. Allocation, enable, disable and free run on the original
  task pinned to its allocation core. Every native return is checked. An
  allocation remains owned until `esp_intr_free` actually succeeds.
- `NativeHcdLifecycle.inc`, `NativeHubLifecycle.inc` and
  `NativeHostLifecycle.inc` retain each callback argument and parent object
  until its child is released. Successful stages are consumed once; retries
  resume the retained stages. Power-off errors do not advance hub state.
- Failed installation can acquire an IRQ before publishing a normal host
  handle. The private `risc_usb_cleanup_pending` latch survives that failure.
  `driver_base.cpp` consults it before pumping partially dismantled state and
  before declaring provider quiescence. A failed cleanup retains the provider
  mapping, PHY and VBUS. The original install error remains the return value.
- The new upstream auto-suspend timer is removed from this private closure:
  this provider exposes no auto-suspend control. Its setter returns
  `ESP_ERR_NOT_SUPPORTED`, and no timer allocation, callback or import remains.
  Existing explicit power/role behavior is preserved.
- Enumeration instrumentation records stages and the fatal state-machine
  `FAILED` transition, without promoting optional string-descriptor errors.

`scripts/native_usb_idf61_sources.py` transforms generated copies, leaving
official source checkouts immutable. These changes intentionally specialize
the host to one S3 root port, owned PHY, internal DMA, no external hub and no
automatic power management.

## Exact imports and native ABI additions

`Drivers/usb_controller_esp32s3/idf61-imports.txt` is the complete **44-name**
undefined-symbol set. All six additions below must stay outside private
OS/CPU ABI v1. Object names refer to `commands.json`, not stale objects from a
previous output-directory build.

| New import | Exact signature | Source and object evidence |
| --- | --- | --- |
| `_frxt_setup_switch` | `void(void)` | `esp-usb/.../hcd_dwc.c:1095`, `portYIELD_FROM_ISR`; `02-hcd_dwc.c.o`. IDF `freertos/FreeRTOS-Kernel/portable/xtensa/include/freertos/portmacro.h:459-468` and `portasm.S:84-107`. |
| `esp_log` | `void(esp_log_config_t, const char *, const char *, ...)` | `ESP_LOG*` in HCD, enum, hub, host, USBH and PHY; objects 02, 03, 04, 06, 08, 10. IDF `log/include/esp_log.h:38`. |
| `esp_rom_delay_us` | `void(uint32_t)` | `esp-usb/.../hcd_dwc.c:458,1279` (1000/1 us); `02-hcd_dwc.c.o`. IDF `esp_rom/include/esp_rom_sys.h:91`. |
| `periph_rcc_enter` | `void(void)` | IDF `esp_hw_support/usb_phy/usb_phy.c:302,373`, `PERIPH_RCC_ATOMIC`; `10-usb_phy.c.o`. Declaration `esp_hw_support/include/esp_private/periph_ctrl.h:64`. |
| `periph_rcc_exit` | `void(void)` | Same PHY callsites/object; `periph_ctrl.h:65`. |
| `xTaskGetCoreID` | `BaseType_t(TaskHandle_t)` | `NativeIrqLifecycle.cpp:26`; `01-NativeIrqLifecycle.cpp.o`. IDF `freertos/esp_additions/include/freertos/idf_additions.h:132`. |

Target-only `test/drivers/native_usb_sdk_abi_probe.cpp` checks the C header
signatures and that `esp_log_config_t`, `BaseType_t`, `UBaseType_t`, task handles
and `eNotifyAction` are all four bytes. It also checks the critical-section
boundary: `portMUX_TYPE` size 8/alignment 4, owner/count offsets 0/4,
unlocked sentinel `0xB33FFFFF`, two cores, 32-bit ticks at 1000 Hz,
one notification slot and dynamic allocation/mutex/notification support.
Queue handles are four bytes, stack elements one byte and stack alignment 16.
It gates log format version 1, level 3 and RTOS timestamps. The assembly
no-argument scheduler handoff, verified against its macro and implementation.

The scheduler/notification/task-affinity calls access resident scheduler state;
the provider must not duplicate their implementation. RCC enter/exit protect
the resident global peripheral lock and reference counts: linking another
`periph_ctrl.c` copy would be incorrect. ROM microsecond delays preserve hardware
timing; scheduler sleeps are not equivalent. Native logging keeps the resident
configuration and the SDK's four-byte configuration value. No safe provider-local
replacement is established for these six calls.

Two initially observed additions were eliminated safely:

1. `esp_cache_get_alignment`: `NativeDmaAlignment.h` specializes IDF
   `esp_mm/esp_cache_msync.c:164-181` → `hal/cache_hal.c:315-329` to S3 uncached
   internal DMA memory, whose alignment result is zero. Compile gates require
   exact S3/6.1 and internal/external cache levels 0/1. Runtime checks require
   DMA+INTERNAL, reject SPIRAM/null output and leave output untouched on failure.
2. `esp_err_to_name`: compile the pinned SDK's immutable lookup table privately
   from `esp_common/src/esp_err_to_name.c`. Remove its unused `_r` function rather
   than introducing unrelated `strerror_r`, `strlcpy` and `snprintf` imports.

The earlier debug build also imported `xTaskGenericNotify` with signature
`BaseType_t(TaskHandle_t, UBaseType_t, uint32_t, eNotifyAction, uint32_t *)`.
The final release-size build removes it without a source behavior change:
`hcd_dwc.c:1034` is the only caller of `_internal_pipe_event_notify`, always
with `from_isr=true`, so the non-ISR branch at line 872 is unreachable.
It is absent from both the final HCD object and ELF. The prior 45-import debug
qualification is superseded; do not retain this name in the minimal native ABI.

Legacy SDK4 private-layout consumers include the USB host `portMUX_TYPE`, queue,
task-notification and PHY/HAL code. Matching symbol names cannot establish
cross-SDK ABI compatibility. SDK6 native packages need a distinct admitted
cohort; do not relabel retained SDK4 binaries or broaden ABI v1.

## Loader-map evidence

Input `.iram1.N` code and literal sections are normalized into loader-supported
`.text`/literal sections before linking. This is safe only under the enforced
non-IRAM interrupt contract: IDF masks that IRQ while caches are unavailable.
It is not an IRAM residency guarantee.

Official S3 MMIO addresses come from `soc/esp32s3/ld/esp32s3.peripherals.ld`.
GCC 15 emits a host-register base literal at `USB_DWC + 0x400`; a private
absolute `RISC_USB_DWC_HOST` symbol supplies exact mapping evidence, backed by
the target `offsetof(usb_dwc_dev_t, hcfg_reg) == 0x400` assertion. The existing
strict loader-map audit has not been weakened.

Qualified candidate: **792,696 bytes**, SHA-256
`9de7e76d83bd984c76263b863de715cc5058d6fc21387fb728c5aa3a1441e294`.
The only exported root is `t5_driver_get`; there is no TEXTREL, unresolved USB
implementation/timer import, unmapped relocation, unmapped relative pointer or
orphan executable section. All **1,078 relocations** were audited.

## Reproduction and tests

From the Drivers checkout, with the sibling pinned SDK/component and native
Runtime configuration present:

```sh
python3 scripts/build_usb_controller_esp32s3_idf61.py \
  --compile-commands ../runtime/idf/build/compile_commands.json \
  --idf ../esp-idf --usb ../esp-usb --output ../usb-native-check
python3 test/drivers/native_usb_source_adapter_test.py --usb ../esp-usb
for runner in test/run_native_usb_irq_lifecycle_test.sh \
              test/run_native_usb_hcd_hub_lifecycle_test.sh \
              test/drivers/run_native_usb_host_lifecycle_test.sh \
              test/run_native_usb_adapter_test.sh \
              test/run_native_usb_startup_test.sh; do
  bash "$runner"
  ASAN_OPTIONS=detect_leaks=0 SANITIZE=1 bash "$runner"
done
```

All pass. The C/C++ fault tests include allocation/enable/disable/free failures,
wrong task/core/affinity/ISR callers, every retained HCD/hub/host boundary,
failed installation before publication, repeated failed retries, consumed
stages freed once, power-off failures, SDK4/SDK6 PHY/startup ordering, native
configuration rejection and unchanged role switching. Source mutation tests
check exact anchors, the explicit unsupported setter, no timer/raw IRQ calls,
fatal-only enumeration instrumentation and unsupported configuration gates.
Address/undefined-behavior sanitizers pass; leak detection is disabled because
the execution environment cannot run LeakSanitizer under its tracing setup.

The builder automatically compiles `native_usb_sdk_abi_probe.cpp` and its
generated profile assertions. Its verified invocation is recorded as
`native-usb-sdk-abi-probe-command.json` in the qualification output directory.
A legacy GCC8/IDF4.4.7 affected-object compile also passes; its command is
`legacy-driver-command.json`. This is compile compatibility, not released-byte
parity. Existing USB-host-v2 discovery/generation and bulk/interrupt-claim host
tests pass using the commands in `.github/workflows/driver-parity.yml`.

## Remaining gates

Runtime native private ABI/admission and new package identity are pending.
No product manifest, catalog, release artifact or remote branch was updated.
Electrical behavior still needs physical S3 tests: cold/hot attach,
external-VBUS transitions, source-off verification, unplug during enumeration,
busy DMA/EP0 timeout, IRQ contention and repeated load/unload. An expired EP0
deadline does not itself cancel SDK-owned DMA; retained ownership remains the
safe outcome when completion cannot be established. This qualification does
not establish concurrent ownership with another private PHY consumer.

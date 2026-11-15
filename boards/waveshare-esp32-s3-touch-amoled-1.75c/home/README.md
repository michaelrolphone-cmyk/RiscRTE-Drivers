# Badge scoped Home boot graph

This Drivers-owned composition extends the shared CO5300 source unit in PR44.
It creates neither a second Watch application nor a new product repository.
The maintained Watch `apps/clock/crown.c` and `scripts/build.py` are unchanged.

## Actual graph and supported contract

`board.json` declares physical devices and dependency bindings. `boot.json`
selects demand-retained activation of four production providers:

- instance2: shared scoped `i2c.bus@1`, controller0, SDA15/SCL14, 400kHz;
- instance13: passive AXP2101 DCDC1 observer, bound to that I2C bus;
- instance5: shared CO5300 `display.output@1`, native QSPI backend and rail13;
- instance6: CST9217 `input.touch.raw@1`, bound to bus2 and rail13.

AXP2101 instance4 is physical parent metadata, not a battery/power-key provider.
All physical providers require `hardware.device@1` and exact compatibility data.
Runtime materializes `touch.i2c@2`, injects device-scoped GPIO and synchronization,
and rejects cross-bus bindings and duplicate ownership. Its I2C boundary enforces
bus-instance matching, not an address ACL: the actual CST source only claims
0x5a; the separate observer claims0x34. Neither changes PMIC registers.

Home receives exactly display, touch, native realtime-control and provider-
promotion grants. All twenty shared declarations remain present; unsupported
battery, external RTC, key, sleep, radio, storage and related services become
explicit optional declarations, not fabricated providers. The native profile
sets declaration capacity24 while retaining live-grant capacity16. Runtime graph
admission, real acquisition/refusal behavior and actual Home execution qualify
this packaging decision; it is not a requirements-stripping workaround.

Civil time remains UNSET without an external time source, so Home renders its
existing TIME UNSET/--:-- and unavailable battery presentation. First checked
frame and raw touch work without those optional services. Unsupported quick
controls remain disabled. This minimal store does not include springboard or
other apps: a swipe can request launch, encounter Runtime's real missing-app
failure and reload Home with the existing visible failure status. No new launch
stub or fake application is provided.

## Ownership and failure safety

CST9217 observes the bound read-only3.3V rail before claiming GPIO2, checks LOW,
waits10ms, checks HIGH, waits50ms, then acquires its touch address. It retains reset
until consumers drain and checked bus/GPIO/sync teardown succeeds. A live touch
subscriber prevents unloading. GPIO1 remains separate: the facade clears any old
hold with its scoped claim, releases it, then native display owns it through
checked close. Historical `touch-startup.c` and board preparation helpers remain
unselected and unlinked; no duplicate reset hook is introduced.

I2C, touch and the shared display facade use Runtime's existing `platform.sync`
service. Their ordinary ELF data may reside in PSRAM, where native S32C1I is not
safe. The build disassembles original linked ELFs with `.xt.prop` retained and
rejects hardware CAS, then validates compacted targets and native imports. It
neither imports private FreeRTOS atomics nor changes Runtime ABI. Checked cleanup
failures retain tokens, borrowed dependencies and mapped provider code; return
values are not permission to forget failed-release custody.

DCDC1 is shared with the CPU. The observer reads identity, enable and voltage-code
registers and reports valid voltage codes and enable state. Display and touch
refuse anything other than verified enabled3300mV before reset/open. The
observer setter always refuses. This confirms register configuration, never measured voltage
under load. Charging, cutoff, power key and unused rails are not modified.

## Exact source and target build

`sources.lock.json` pins Drivers base6503f9ae, Watch605d5e7d, Runtime13eca114,
controller contracts851b8998, System Appsbb0cadf3, Utilities4fdcad24 and
IDFfff9895c. The compiler is Espressif GCC15.2.0 esp-15.2.0_20251204. Builders
bind actual dependency bytes and local source hashes; a revision label alone
is insufficient. The Home ELF is built through the maintained shared route,
without board-specific application defines, and copied byte for byte.

`../native/` supplies a linked ESP32-S3 native profile using existing Runtime
hooks and the same CO5300 geometry. It selects32MiB flash and octal PSRAM,
with8MiB physical capacity expected, a3MiB factory slot and remaining-flash SPIFFS bootfs. The native ELF/binary and
loadable store are distinct outputs, not a generated combined flash image.

From the repository root, with exact checkouts at the paths supplied:

```sh
python scripts/build_badge_native.py --runtime RUNTIME --idf IDF \
  --cc XTENSA_GCC --python IDF_PYTHON --output NATIVE_OUT
python scripts/build_badge_home.py --runtime RUNTIME --watch WATCH \
  --controllers CONTROLLERS --system SYSTEM --utilities UTILITIES \
  --cc XTENSA_GCC --output STORE_OUT
python scripts/validate_badge_home.py --runtime RUNTIME --controllers CONTROLLERS \
  --store STORE_OUT/store --native-elf NATIVE_OUT/build/riscrte_native.elf \
  --output ADMISSION_OUT
python scripts/test_badge_runtime_execution.py --runtime RUNTIME --watch WATCH \
  --controllers CONTROLLERS --system SYSTEM --utilities UTILITIES \
  --store STORE_OUT/store --output runtime-execution.json
```

Use an empty build output, the pinned IDF Python environment with pyelftools,
and preserve the `xtensa-esp32s3-elf-gcc` frontend filename. Admission must use
the freshly generated store/build receipt and current matching source bytes.
`--graph-only` explicitly omits image/import verification and is not equivalent
to the full command. The consolidated Badge workflow runs all qualification
steps, including sanitizer repeats, and retains evidence without publishing a
release or operating a device.

## Evidence layers and limits

- Native target: full IDF link, Runtime native authority/time/IRAM proof, and
  Badge profile/partition/identity negative checks.
- Target store: unchanged shared Home plus four real Xtensa providers, Runtime
  structural ELF validation, exact imports against linked native exports, and
  original-instruction PSRAM safety checks.
- Actual Runtime graph: positive boot preparation and fifteen failing graph/
  target-image mutations, with zero hardware calls during admission.
- Actual Runtime execution: target JSON with host-mapped production providers
  and unchanged Home; real graph, CpuPort, lifecycle, promotion, first-frame,
  touch and fallback. Lowest hardware, host ELF admission/relocation and
  instruction mapping, owner/health
  lifetime bounds, confirmation hook, diagnostic sink and host file-source
  fixture are test boundaries, not an actual firmware boot/confirmation.
  Separately labeled probe apps exercise retry/retention, not a fake Home.
- Direct Home behavior: production renderer/CST/facade against four available
  capability classes, gestures, missing-service degradation and failure cleanup.
- Provider regression: generic display geometry, native transport, CST protocol/
  reset lifecycle, read-only supply and generalized I2C controller mappings.

ASan/UBSan results are separate from physical behavior. LeakSanitizer is disabled
on ptrace-based executors and is not claimed. Host execution does not execute
Xtensa instructions or prove an on-device boot. No physical first-frame, touch
orientation, voltage/current, BOOT/PWR, persistence wear or sleep result is
claimed. Combined-image assembly, device validation, merge and release remain
outside this source qualification; no flashing or device control is performed.

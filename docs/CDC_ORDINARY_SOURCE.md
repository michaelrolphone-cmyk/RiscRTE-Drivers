# Ordinary canonical CDC source candidate

`Drivers/usb_cdc_stream` is a separate, unselected ordinary ABI2 source candidate:
`usb-cdc-acm`0.1.8, `usb.host@1` → `serial.port@1`. It recovers Reader
`9c8783d6f20659a514bfaeb3847a77c7213dc4f6:Drivers/usb_cdc_v2`, adding only an
explicit include of the already-present generic `RiscSerialPortV1.h`. The retained
upstream `u1-class-functional` manifest status is not new physical acceptance.

It does not replace or select over any existing source:

* `usb_cdc`: historical0.1.0 ABI1, kernel.usb.host → usb.class.cdc_acm.
* `usb_cdc_v2`: historical separate usb-cdc-acm-v2 0.1.0 ABI2 fork.
* `usb_cdc_native`: canonical0.1.9 native candidate with additional clock,
  session/queue/deadline contracts.

The old sources, builders, accepted constants and historical inventory remain
unchanged. Equal canonical id is lineage, not proof these variants are equivalent
or that a package resolver should install/select all of them. Use path-keyed
custody/current observations; existing CDC inventory/path/tree diagnostics remain.

## Source and target production

`python scripts/build_usb_cdc_stream.py` writes only
`dist/usb-cdc-acm-ordinary-candidate` by default. It refuses output inside the three
existing CDC output directories. It cannot request historical byte replay. Output
is a candidate manifest, ELF and content-addressed build receipt: source/shared
pump/all available SDK/build helpers, compiler/libgcc/readelf hashes, invocation
and exact artifact hashes. The SDK snapshot is an over-approximation, not a
consumed-header trace. Compiler subprocess/system closure is not exhaustive.
Inputs and tools must remain unchanged through the build; failures do not replace
retained output. A separate staging directory is published only after ELF and
symbol validation. This receipt does not imply firmware selection or device proof.

## Actual ordinary consumer qualification

```
python test/run_cdc_ordinary.py --reader-root /path/to/pinned-reader --mode normal
python test/run_cdc_ordinary.py --reader-root /path/to/pinned-reader --mode sanitized
```

`test/drivers/cdc_ordinary_consumer_inputs.json` binds60 exact files from Reader
9c8783. The actual installed inventory, serial bridge, stream registry and provider
graph are compiled with the current Drivers checked-host body and this candidate.
Only the physical controller is simulated. The mixed consumer-only SDK uses eight
explicit current headers plus exactly the declared Reader
`RiscPackageResourcesV1.h` addition; no global SDK replacement or native Runtime
port occurs. Dependency-only scans cover **all12 compiled C/C++ translation
units**, refusing undeclared non-system includes. These scans are not object-build
traces or exhaustive system/compiler closure. Optional `--receipt` records exact
inputs, selected dependency scan and commands.

The copied CDC fixtures use generic serial types explicitly rather than aliases
that could hide contract differences. Tests retain claim-scoped controls,
read-only inventory, failed-open/close custody, descriptor association, generation
and stale-token behavior. Added admission negatives reject truncated host tables,
missing checked release/control and wrong dependency API. The actual generic
consumer exercises queue exhaustion, backpressure, pipe failure, checked close
retry, detach and stale endpoints; the separate actual inventory fixture covers
uncertain identity and retained grants. Normal and undefined-behavior-sanitized
execution are host qualification, not target timing or electrical acceptance.

CI checks out only declared pinned Reader files, runs the actual composition and
builds/uploads the separate target candidate. Existing ABI1/-v2/native and frozen
historical gates remain present. Any target or hosted result must be attributed to
its exact commit and artifact; this document alone does not assert that they ran.

## Exact source custody

The later path-keyed custody manifest records this candidate tree
`e3f0169dc0717566faecb82c147c811bf55df6a6` from actual Drivers producer commit
`dbf925cf85f62cb0027dabe9f2b4f2359ff01160`. This proves source custody only,
not selected replacement, upstream tree equality, released acceptance or device
qualification. The original historical custody manifest remains byte-for-byte
unchanged. Existing current/historical inventory and divergence diagnostics remain.

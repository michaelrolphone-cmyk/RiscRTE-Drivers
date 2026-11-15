# Native owner candidate: pre-emission admission only

This additive, explicit candidate producer is not an ordinary builder replacement.
Existing driver builders, native toolchain selection, normalization and parity paths
are unchanged. Nothing invokes this producer by default.

## Fixed source contract

The producer binds to RiscRTE Core commit
`ec86b26b2f06271c62405dc8976b07dae19b713f`, not a caller-nominated
success receipt or mutable source manifest. Its adjacent `core-source.json`
contains exactly thirteen hashed C/header/inventory members. The binding parser
rejects unknown fields, duplicate keys, bool-as-int, other repositories/commits,
extra or missing members, and ABI3/4.

A fresh private build copies only that thirteen-member closure and the owned C
wrapper. It uses an explicit host compiler hash, SDK, empty sdkconfig and fixed
environment. No live repository include tree or untracked adjacent header is
compiled. This metadata-only gate does not qualify the runtime's target layout.

The actual C structural validator runs before Python ELF extraction. Actual Core
candidate5 import authorization and exact manifest-set matching run before mapped
section/relocation checks. Mapping requires the sole public getter
`t5_driver_get` in ordinary `.text`, plus the three v5 worker imports.
Private unresolved implementation leaves and mixed worker revisions are refused.
The original ELF is snapshotted once. Source, tooling, Core closure and output
bytes are rechecked across C, mapping and emission boundaries.

## Producer CLI

Use Python3.11 with pyelftools0.32 and an explicit existing Apple Clang/macOS SDK:
```sh
python3 -B scripts/native_owner_candidate/package_candidate.py \
  --elf linked-owner.elf --composition COMPOSITION.json \
  --composition-sha256 COMPOSITION_SHA256 \
  --source owner-source --source-manifest SOURCE.json \
  --source-manifest-sha256 SOURCE_SHA256 --manifest manifest.json \
  --admission-source /path/to/exact-core-checkout \
  --compiler /path/to/clang --compiler-sha256 COMPILER_SHA256 \
  --host-sdk /path/to/MacOSX.sdk --output /fresh/candidate-directory
```

Inputs must describe the real linked image and exact source composition. The
producer does not link an owner. It emits a candidate package only after all
gates pass. It never emits selected policy or boot bindings, grants authority,
starts a worker, or deploys. Passing import compatibility is not universal ABI5
admission or permission to access hardware. Full-owner behavior, selected policy,
runtime caller, internal port/cache-off, target and physical-retirement gates
remain pending.

## Reproduce the38 hosted cases

Check out the exact Core commit above into a separate clean directory. Supply the
official `pyelftools-0.32-py3-none-any.whl` (SHA256
`013df952a006db5e138b1edf6d8a68ecc50630adbd0d83a2d41e7f846163d738`).
The runner verifies that wheel and unpacks it only into its fresh output:
```sh
python3 -B test/run_native_owner_candidate.py \
  --repo "$PWD" --core /path/to/exact-core-checkout \
  --cc "$(xcrun --find clang)" --sdk "$(xcrun --sdk macosx --show-sdk-path)" \
  --wheel /path/to/pyelftools-0.32-py3-none-any.whl --output /fresh/test-output
```

This preserved C/SDK recipe is Darwin-only. It authenticates source, the exact
Core commit/closure, all four translation units' consumed header closure and
effective macros, then runs exactly38 named cases with actual C admission.
Constructed ELF cases exercise malformed data, import policy, immutable-byte
ordering, source drift, mapping/exports and explicit candidate-mode refusal.
They are not a linked full owner or target execution. Logs, selected case names,
failures and source/tool hashes remain in RESULT.json and CASES.json.

The dedicated macOS workflow runs this same entry with Core pinned immutably.
CI has not run until its exact published commit receives a measured result.

## Measured bounded evidence

The repository-native entry passed all38 cases with actual Core C admission on
Darwin. RESULT SHA256: `3c93e8d19f276e19c2e834b62f610057a0ddc8c4f28a24ecf67b3dff57b022ed`.
The earlier fixed-Core hosted reconstruction also passed38; that remains a
separate source snapshot, not a substitute for this portable run.

The actual producer CLI accepted the already-linked2136-byte Xtensa ET_DYN
fixture `16e219441ee8e72ed97abaea44a0979dab211133970cac7245a5f00d28e92105` and emitted identical
driver bytes plus envelope `c95eecae19edb6e62a8348283b7fbd867cafbbae1846820eaf56f392debc4b8a`.
CLI RESULT SHA256: `ffa79b79f0a7cbf895b648a2264113a5a73e14131b2448b26e7fde19b9e6f580`.
There were zero fresh target compiles/links and no stripping or normalization.
This tiny fixture retains exactly the three v5 worker import addresses and a
getter; it is not a complete native owner or real worker execution test.
The producer's runtime/physical/activation flags all remained false.

Only this document and status provenance were updated after those measurements.
The new dedicated workflow and all target/physical/full-owner gates remain
unexecuted or pending as explicitly described above.

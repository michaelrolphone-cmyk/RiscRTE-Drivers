# Reproduce the complete prototype cohort

Use a clean Drivers candidate and the exact `sources.lock.json` pins. Keep the
historical shared Home dependencies separately: `home/sources.lock.json`.
Workspace directory names are `runtime`, `watch`, `controllers`, `system`,
`utilities` (historical pins), plus `runtime-prototype`, `watch-prototype`,
`system-prototype`, `utilities-prototype` (selected candidate pins).
Fetch the immutable Runtime SDK object274bc66f193cbe29018d2a85c9400cb0dce8aacc
into the historical Runtime checkout for the maintained native Settings builder.
Do not replace its checked-out source or edit bundled SDK headers.

Use clean ESP-IDFfff9895c82d744c7237be8847347bdd1b07c6643 with all recursive
submodules and its official Python environment; GCC15.2.0
esp-15.2.0_20251204, preserving the xtensa-esp32s3-elf-gcc frontend symlink.
The existing native CI container and new Badge prototype CI lane pin the exact
SDK/toolchain image. Managed component discovery is disabled because all
components are already exact source-owned pins; no remote component resolution
is needed. The local executor's component-manager psutil PID probe was unusable.

From the Drivers checkout, with an empty output path outside any source tree:

```sh
IDF_COMPONENT_MANAGER=0 ASAN_OPTIONS=detect_leaks=0 python scripts/run_badge_prototype_ci.py --foreground-idle --workspace WORKSPACE --idf IDF --cc XTENSA_GCC --output EMPTY_OUTPUT
```

This is the deterministic composition entry point. It builds native with one
job,builds shared roles/providers/apps,checks native exports and original-ELF
PSRAM instructions,admits every targetELF with positive/negative tests,executes
host-recompiled production navigation in normal/sanitized lanes,then generates
and independently replays both filesystems and seals the matching32MiB image.
It never opens a serial port,flashes,deploys or provisions infrastructure.
Commands and raw logs are retained in OUTPUT. Host ELF instruction mapping,
SDK/hardware and storage media are models; target import/admission is separate.

Additional owning-repository tests are documented in Runtime
`docs/NATIVE_APPDATA_GEOMETRY.md`, System `docs/NATIVE_TIME_TOUCH.md`, Watch
`scripts/test_bringup_native.py` and Utilities
`scripts/test_native_standalone_stopwatch.py`. LSan is excluded on the ptrace
executor; ASan/UBSan remain enabled. Keep the exact observed sanitizer scope.

## Restore the incremental source bundles

The handoff's incremental bundles advertise the candidate as `HEAD`; the
branch names in `sources/manifest.json` are publication targets, not refs
inside those bundles. In a repository containing the manifest's prerequisite
base commit, verify the bundle and fetch its HEAD into a NEW local review
branch, then create a new worktree. For example:

```sh
git -C DRIVERS bundle verify /absolute/path/sources/drivers.bundle
git -C DRIVERS fetch /absolute/path/sources/drivers.bundle HEAD:refs/heads/review/badge-prototype
git -C DRIVERS worktree add /new/clean/drivers review/badge-prototype
```

Repeat for the five bundle files in `sources/manifest.json`. Verify each
resulting commit and tree against that manifest. Do not reset existing work
or assume the bundles are standalone repositories. Fetching a named
`refs/heads/codex/...` from a HEAD-only bundle fails and is not the recovery
procedure. Source restoration and offline checks do not publish anything.

## Configurable sleep profile

The prototype builder selects the same shared Watch source with generic
`--default-sleep-mode hybrid --default-idle-seconds 15` defaults. Pass
`--idle-seconds 5..3600` to `build_badge_prototype.py` to choose another build
fallback. Valid saved preferences take precedence. Settings exposes mode and
idle editing with the matching fallback; neither build option seeds wall time.
The historical Home dependency cohort remains separately reproducible; current
Home is byte-identical to its configured shared build, not to the earlier
awake-only package. Run the full native/store/host qualification after changing
a policy, source pin, or package member.

## Explicit foreground cohort

The command above selects the returning-Light foreground cohort. Omitting
`--foreground-idle` preserves the earlier Home-only composition. The selected
System/Utilities builders stage the canonical Runtime SDK and exact generic
helper inputs; do not replace bundled SDK headers. Package admission requires
all 21 production-path cases in normal and sanitized receipts for this cohort,
not the earlier 11-case receipt. See FOREGROUND-IDLE.md for scope and remaining
hardware/diagnostic/durable-recovery boundaries.

The selected Watch-native Springboard requires its exact recorded compilation
recipe for host execution. The foreground build emits this recipe. An explicitly
non-foreground Watch build currently fails closed in the execution verifier
until its builder emits equivalent recipe metadata; do not substitute the old
generic Springboard fixture or claim that profile qualified from this cohort.

## Optional native Files increment

Add `--include-files` alongside `--foreground-idle` to the composition entry
point to include the restricted installed-files browser and seventh launcher
entry. This requires 23 production execution cases in each lane, including
Files navigation and returning Light. The generated APPLICATION-INVENTORY.json
compares actual packaged roles with maintained source profiles and lists
missing capabilities; static capability closure is not product qualification.
Files has only Runtime's admitted manifest/ELF allowlist, not arbitrary private
AppData, SD, an updater, or writable storage. Native safe-content geometry and
shared time preference are selected explicitly; legacy profiles remain intact.

A separately recorded native-binary reuse is valid only if the packager verifies
every exact native source hash, Runtime/IDF pin, binary hash and SDK configuration
against the new store admission. It is not a claim of a fresh native build.

## Optional native daily-app presentation

Add `--native-daily` with `--foreground-idle` (and optionally `--include-files`)
to select native-safe Calculator, Battery and Stopwatch. The shared native
renderer uses original glyph faces and capability-safe pixel rectangles, without
resizing a completed legacy bitmap. Exact daily metadata and all three compiler
recipes must agree on selection; both complete execution receipts must match.
Old profile paths remain available and retain their original source branches.
The app ABI has no circular aperture query: truthful conservative safe insets
remain a provider contract. This does not claim full-aperture daily-app layouts.


## Exact source-pack consumer (continuation candidate)

The candidate recovery path consumes immutable owning publication commits in
`docs/badge-source-recovery/sources.json`, validates every object pack, and checks
out original source pins and SDK anchors. The publication identity is recorded
separately. It also restores the exact SPIFFS override and checks the shared
clock helper blob hash. No latest-ref fallback is permitted.

Start with only a clean candidate Drivers checkout at WORKSPACE/drivers and no
other role directories. Existing roles are refused to preserve custody; keep
failed attempts and use another fresh workspace. After the candidate commits
are published, the supported invocation is:

```sh
python WORKSPACE/drivers/scripts/recover_badge_ci_sources.py --workspace WORKSPACE --include-timecard
```

Use the `selected-full` matrix recipe in `.github/workflows/badge-prototype.yml`
with the pinned ESP-IDF container, all accepted app/rendering/time-sync flags,
16-row queued native pixels, column/unchanged-stripe reuse, Timecard, and
`--spiffs-source WORKSPACE/spiffs`. This is the maintained complete selection;
the earlier foreground-only example above is historical. Native/app selections
must match. The runner executes both normal and ASan+UBSan lanes independently,
qualifies local Quick receipts, and applies artifact-dependent time-sync gates
only after native/target/execution outputs exist. Do not call them without inputs.

Current boundary: original Runtime recovery publication e125ae9 is stopped by
automatic approval review. A locally supplied exact commit passes source
restoration but is not GitHub-only reproduction. The prepared full recipe has
not completed target/package/hosted qualification. See
`docs/qualification/badge-continuation-2026-10-28/REVIEW.md` for exact tests,
original/publication identities, pending drafts, current-main reconciliation
and remaining applications. The accepted public prototype remains unchanged.


The Runtime publication hold above is resolved by explicit approval: published
`318d77283ba707b6f2820d2833e5d7ea3ff3f7e8` preserves the approved e125ae9
tree. The current sources.json contains its actual GitHub identity. Historical
failed attempts remain preserved; rerun the current consumer from clean GitHub
inputs before claiming reproducibility.

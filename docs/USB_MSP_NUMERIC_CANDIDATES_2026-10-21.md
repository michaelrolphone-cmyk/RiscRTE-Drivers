# MSP candidate version admission correction

## Result

The earlier `ed7886d0b2c7f1ac0d235f440d52756782d1b807` cleanup source and
ownership tests remain valid, but its `0.1.2-checked-consumer.1` package versions
were **not admissible** to pinned Runtime. This descendant corrects that bounded
packaging defect. No MSP, programmer, host or controller production behavior,
SDK, product pin, accepted artifact or publication state changes.

Both distinct package identities, `usb-msp` and `program-msp`, now use numeric
**0.1.2**, with `experimental-local-checked-consumer` status and their existing
isolated `dist/*-checked-consumer` candidate output paths. Numeric validity is
not evidence of release, trust, native selection, ELF admission or execution.
The original ed7886d patch and bundle remain byte-for-byte unchanged.

## Version inventory

The inspected Drivers release inventory/source history records usb-msp 0.1.1
and program-msp 0.1.0. The preserved local programmer safety checkpoint adds
program-msp 0.1.1. Native Reader c98e422, historical Reader 34d8e694, and canonical
Reader 9c8783d6 source manifests retain usb-msp 0.1.1/program-msp 0.1.0.
Reachable Drivers manifest history was also scanned. Neither package had
numeric 0.1.2 in these inspected records. This is a bounded source/release
inventory check, **not a global catalog version reservation**.

The exact commits, paths, manifest hashes, observed versions and chosen values
are preserved in `docs/evidence/usb-msp-numeric-versions-20261021/inventory-and-overlay-lock.json`.
No catalog record or released/source inventory was modified.

## Actual pinned Runtime regression

The runner compiles the unchanged full `src/bootstrap/Runtime.cpp` from Runtime
`6a3b3d46f5b2857eacc6e9863799dec1f466b036`, with its real JSON, board, stream,
graph and module implementation dependencies. The test invokes its actual
private `Runtime::manifest` method using the host compiler's
`-fno-access-control` test option. It also invokes actual
`RuntimePackages::makeIdentity` and `RiscUpdate::parseVersion`. No substitute
regex, copied parser, Runtime patch or device operation is used.

Both numeric MSP manifests pass all three checks. Version-only mutations with
the exact old prerelease suffix, build metadata, missing patch component and
uint32 overflow fail. Actual `Runtime::manifest` reports `invalid driver
version`. This proves syntax/schema acceptance, not ELF loading or target
installation. Normal and ASan/UBSan runs pass; LeakSanitizer remains disabled
under the environment's ptrace limitation.

```sh
bash test/run_msp_numeric_manifest_test.sh /path/to/pinned/RiscRTE
SANITIZE=1 ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 \
  UBSAN_OPTIONS=halt_on_error=1 \
  bash test/run_msp_numeric_manifest_test.sh /path/to/pinned/RiscRTE
```

The runner verifies the exact Runtime commit and clean source/header/library
inputs. Optional additional manifest paths are tested through the same real
parser. This qualification also passed the independently owned host **0.1.7**
and controller **0.1.27** overlays at:

- `candidates/usb-native-stream/usb-host-v2.json`
- `candidates/usb-native-stream/usb-controller-esp32s3.json`

Those files and their separate candidate builders belong to the native CDC
owner's isolated clone. They were read and hash-locked, not copied, edited or
integrated into this branch. The original prerequisite host/controller manifests
remain untouched here and still carry inadmissible suffixes; a native package
composition must deliberately select the corrected overlays. The sibling also
has additional host custody hardening, which is not silently incorporated here.

The MSP composed regression is rerun normally and sanitized, eight processes
per mode; all pass. Three candidate/immutable-history route tests and Python
compile checks pass. The earlier source lock under `usb-msp-checked-consumer-20261021`
describes ed7886d, including its now-corrected packaging; this increment has a
separate lock and evidence directory rather than rewriting historical results.

## Preserved boundaries

The earlier physical-cleanup/target-success distinction is unchanged. Legacy
void-release hosts stay conservatively pinned, and detach before successful
programmer target-close completion still retains its session. This work does
not supply native flasher UI/package selection, target compilation, full CI,
X4 root/VBUS electrical proof, publication or physical programming acceptance.
The whole-workflow historical host builder gate identified in the earlier
report remains outside this packaging correction.

No publication, push, PR, merge, tag, release, catalog mutation, product pin
change, accepted-byte overwrite, hardware request, USB/device access, flashing
or radio action occurred.

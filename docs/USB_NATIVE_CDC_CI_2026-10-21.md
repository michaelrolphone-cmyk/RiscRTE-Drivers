# Native CDC bounded CI integration

This separate qualification branch starts at the captured native CDC PR34 head
`df8b5175f57447ec15b41819f5b04bb5ba6a8600`. It does not amend that branch or its
original tests, recovery archive, accepted artifacts, versions or SDK pins.

## Scope

- Route the checked host through `build_usb_host_checked_cleanup.py` and the
  numeric native host/CDC overlays through `build_usb_host_native_stream.py`
  and `build_usb_cdc_native.py`. Their separate output paths and version checks
  remain unchanged. The legacy host 0.1.3 builder is not compatible with the
  checked source manifest and is not used to build this candidate.
- Run existing native CDC/actual-host tests, negative mutations and checked
  custody/source-adapter suites in normal and ASan/UBSan modes. Leak detection
  is disabled; no leak pass is claimed.
- The prerequisite-host negative control uses published checked-cleanup commit
  `ad943a4a514c910c777a427c8cad6d6841f48fd1` instead of requiring an unpublished
  original commit object. Its host Git blob is exactly
  `ed93499c915ba86f2764d82df6f69ea219d32319`, identical to original checkpoint
  `b4334b76124f80d1252eb7dda9e903fd1b7b4d39`. The test checks that blob before
  compilation; its counterexample and assertions are unchanged.
- Historical GCC8 replay runs with complete immutable source, SDK and builders
  from `a7ae39994603c4545a3c014f09051ecfc12084d2`, not the candidate SDK.
  Controller historical replay keeps its existing separate producer pin
  `179979b94cc5a1d4787c0ad99515c5c07edd92d8`. All frozen hashes and byte-parity
  assertions are unchanged. Historical replay is not native CDC acceptance.

The first exact-head run (`d739fca7987b6e8b7868b927277e4600a1c6d28e`,
Actions run `39651992594`) passed both native modes and all three Xtensa package
builds. The broader build exposed two stale consumer fixtures after the host
SDK grew: mass-storage's positional initializer lacked optional suffix slots;
XInput's truncated-prefix negative control used the enlarged discovery type.
This integration uses designated fields (remaining suffix slots stay zero) and
the explicit deployed discovery-prefix type. No assertion is removed, warning
is suppressed, or production behavior changed.

## Explicit limits

The dedicated target job covers Xtensa host/CDC packages only. Full native
controller translation units, import and loader closure require the pinned
ESP-IDF compile database and remain unqualified here. Existing Runtime/client
host evidence is preserved but is not rerun by this bounded workflow. No SDK
pin change, dependency expansion, driver-semantic change, firmware selection,
hardware/electrical action, merge or release is performed.

The separate upstream source/documentation parity check remains authoritative
and may reject deliberately changed candidate source. It is not bypassed or
made green by updating baselines. Exact-head CI results must be recorded before
calling any covered stage passed.

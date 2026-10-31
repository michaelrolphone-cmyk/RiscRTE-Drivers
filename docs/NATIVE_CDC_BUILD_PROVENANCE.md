# Native CDC candidate build provenance

The existing native CDC builder still builds the separate candidate, not accepted
historical CDC releases. Source, version0.1.9, object-build flags, export/import
checks, output manifest fields and output directory are unchanged. The existing
workflow already uploads this directory; no new pipeline or Runtime qualification
is introduced. A successful build additionally writes a hash-named JSON receipt.

The receipt distinguishes three scopes:

- Explicit compiled translation unit and separate compiler dependency scan. The
  dependency-only pass uses matching preprocessing options and records its exact
  command/output plus hashes of reported files. It is not instrumentation of the
  object-build invocation and cannot prove all implicit compiler dependencies.
- Complete native source-directory and available SDK/driver snapshot, plus the
  builder, toolchain selector, normalizer and reused clock hash helper. Available
  SDK inventory is an over-approximation, not a claim all headers were consumed.
- Selected compiler executable/version, libgcc archive, readelf binary and output
  ELF/manifest hashes. Compiler subprocesses, environment and implicit system
  closure are not exhaustively enumerated. Paths are invocation-specific, so the
  receipt is run provenance rather than a universal deterministic build identity.

The builder checks input/tool/dependency hashes again before issuing success.
Compiler, dependency, ELF, import/export and changed-input failures issue no new
success receipt. Existing output-writing semantics are retained: a failed later
run can leave partial output and earlier hash-named receipts. Consumers must verify
both output hashes against a particular receipt, never infer latest success merely
from a receipt's presence. Prior receipts are not relabeled or silently deleted.

Verified hosted bea07858 native artifact12946435482 already contains0.1.9 ELF
13264B/SHA256752632d8f783b337a8242baf86d49dd01e4b688de0db87dac5a625c8653034da.
That evidence remains unchanged and predates this receipt extension. A new hosted
run is needed to qualify the new dependency-scan/receipt path. No current product
selection, full controller SDK closure, USB hardware or behavior equivalence is
inferred from a source hash or this existing successful build. Native candidate,
ABI1 canonical release, ABI2 -v2 fork and Reader canonical source remain distinct.

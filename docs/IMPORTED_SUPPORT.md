# Imported support subsets

The existing parity validator optionally reads qualification/imported-support.json.
This is separate from own-repository local-source-custody, whose provenance rules
and frozen bytes are unchanged. Imported support must come from the same explicitly
configured upstream repository, an exact commit, and the same Drivers directory.
No package manifest is allowed. The existing Source reader resolves the commit and
full upstream tree; truncated, symlink and other nonregular entries are rejected.

Each declaration pins full upstream tree, exact selected relative paths/modes/blob
IDs, and actual local subset tree. All actual files must equal the selected subset;
unlisted files, changed content/modes and missing files fail. Only successfully
validated directories enter tracked paths. Invalid declarations retain both the
specific rejection and original untracked-directory diagnostic. Occupied package
and own-custody paths cannot be covered again. No historical release or source-tree
baseline is replaced. No source-version or recorded-tree discrepancy is waived.

The first instance imports only SerialStreamPump.inc from Reader commit
9c8783d6f20659a514bfaeb3847a77c7213dc4f6, common tree
5e45fd2944a203711b8a604a11ebc0d45abd6bc8. The unrelated pcf8563_rtc_ops.h is omitted
and reported explicitly. This is not whole-directory parity, build qualification,
behavioral equivalence or permission to overwrite local variants. CH34x's historical
accepted package remains separate. Current observation binds declaration, successful
rows and errors so freshness checks detect changed support evidence.

This incremental checker patch is based on the published observation implementation.
The independently held source-qualification patch must be mechanically integrated
and retested by the coordinator if both are published; neither silently supersedes
the other. Existing custody and historical preservation consumers stay unchanged.

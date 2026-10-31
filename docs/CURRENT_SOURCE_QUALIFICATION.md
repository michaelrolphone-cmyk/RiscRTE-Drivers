# Current source qualification linkage

The existing parity validator automatically reads optional
`qualification/current-sources.json` (schema1, sources array). This is a narrow
extension to its current-source observation, not a replacement inventory, source
baseline, release manifest, custody engine or behavioral-equivalence classifier.
Local-only custody remains path-scoped in its existing file; imported source paths
must not be forced into that support/adapter/candidate schema.

Each source declaration supplies id, path, current version/tree, exact upstream
repository/commit/path/version/tree, exact release-index commit, immutable historical
version/tree/accepted ELF SHA256/size, attribution/rationale/limits, and four
repository-relative SHA256-bound JSON evidence references: current_build,
current_manifest, historical_replay, historical_manifest. Additional documentary
provenance remains visible. The first instance is the separately hosted-qualified
clock source; implementation does not special-case its identity or filename.

Evidence paths must not escape or follow symlinks. Current source file hashes
must cover the complete directory and artifact/source manifests must agree.
Current build-input pins (including SDK/helpers) must still match local bytes.
Compiler, archive, inspection-tool, invocation and output pins must be present.
Historical source coverage/hashes are checked against the immutable Git tree;
manifest and successful byte replay must match the retained release-index entry,
not a replacement accepted hash supplied by the declaration. Future upstream,
release-index, source, SDK or evidence drift invalidates the linkage. Missing or
invalid evidence leaves all original diagnostics unresolved and adds an error.
Absence of the whole optional declaration file confers no qualification and restores
the original diagnostics; it never silently grants a previously qualified status.

Successful binding resolves ONLY two metadata diagnostic forms for that identity:
`source version mismatch` and `recorded source tree needs refresh`. The former
historical release declaration is not the current source version; the latter tree
is explicitly retained as historical while the current tree is separately pinned.
The full original list remains `legacy_diagnostics`; each resolution has an exact
qualification pointer in `metadata_resolutions`. All other errors remain, including
actual conflict/source drift, missing imports and untracked source directories.
The original observation status and bytes_match_upstream are not rewritten.

The linked evidence is visible in `current_source_qualification`, including all
qualifiers. Hashes verify local binding, not external CI authentication or source
truth. Retained independent hosted provenance review supplies that attribution.
Build success/historical replay does not establish hardware behavior or equivalence
of a local variant to upstream. An unknown/new source remains usable for inspection
but receives no metadata resolution from this mechanism. Synthetic tests are
contract tests only, not additional hosted qualification.

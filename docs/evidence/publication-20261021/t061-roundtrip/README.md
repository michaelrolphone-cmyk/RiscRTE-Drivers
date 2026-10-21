# t061-roundtrip: durable source and evidence checkpoint

Published 2026-10-21. Source commit: `dc52499d4839928040a68e406f19cb31321092b0`. Original tested checkpoint: `fe310f01a90a616ce05be02b677debdf3cfcd607`. Identical complete source tree: `ac75332006ebb561369e73834631db432a55470a`.

This branch preserves existing work, not a firmware release or completed product feature. No new target build, device test, merge, release, or deployment was performed by publication. Historical results and acceptance gaps are in qualification-report.md. Prior local-only/publication-held wording is retained as history and does not describe the new GitHub checkpoint.

## Evidence

`evidence.tar.gz` contains the exact original incremental Git bundle, binary-safe patch, technical source/command receipts, logs (including preserved failures), provenance and available rendered screenshots. SHA-256: `9746d33d0c9c20655a3cda946ec4fc554be0db4909458f60061b9f6a947b7f21`; 23365 bytes; 27 files.

The archive's source-checkpoint.json lists every included technical file with SHA-256 and every excluded generated binary/object or duplicated archive. Existing historical SHA256SUMS may name omitted generated files; the new inventory specifies what is captured here. No private task-tracker snapshots are included. Exact original commit restoration requires its original parent; restore stacked parent bundles first. Remote API commit metadata differs, but its source Git tree hash is identical.

## Dependency and qualification boundary

Original parent: `a7ae39994603c4545a3c014f09051ecfc12084d2`. Proposed PR base: `main`. Read source-checkpoint.json and the qualification report before integration. Host/sanitizer evidence does not establish target, radio, physical display/touch, product composition, or deployment acceptance.

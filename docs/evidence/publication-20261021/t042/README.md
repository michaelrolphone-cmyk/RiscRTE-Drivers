# t042: durable source and evidence checkpoint

Published 2026-10-21. Source commit: `50d233c82a75dab153da295bd224ecca7043d7d0`. Original tested checkpoint: `a66670d605fc88a3e562e48ceeb4021fb9a6a5f5`. Identical complete source tree: `6eec6a06b8d2873f77edcdbabe914bc4e09afea4`.

This branch preserves existing work, not a firmware release or completed product feature. No new target build, device test, merge, release, or deployment was performed by publication. Historical results and acceptance gaps are in qualification-report.md. Prior local-only/publication-held wording is retained as history and does not describe the new GitHub checkpoint.

## Evidence

`evidence.tar.gz` contains the exact original incremental Git bundle, binary-safe patch, technical source/command receipts, logs (including preserved failures), provenance and available rendered screenshots. SHA-256: `91f0db535601bb530db18eb7a72dc34b21ba836f08e8fe3d3fd2a2f689b4fdd6`; 14753 bytes; 13 files.

The archive's source-checkpoint.json lists every included technical file with SHA-256 and every excluded generated binary/object or duplicated archive. Existing historical SHA256SUMS may name omitted generated files; the new inventory specifies what is captured here. No private task-tracker snapshots are included. Exact original commit restoration requires its original parent; restore stacked parent bundles first. Remote API commit metadata differs, but its source Git tree hash is identical.

## Dependency and qualification boundary

Original parent: `ba28f9bdfbe84d23de36350189cc2ec89fea1677`. Proposed PR base: `tasker/t061-battery-provider-local`. Read source-checkpoint.json and the qualification report before integration. Host/sanitizer evidence does not establish target, radio, physical display/touch, product composition, or deployment acceptance.

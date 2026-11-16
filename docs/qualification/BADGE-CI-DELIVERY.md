# Exact Badge CI delivery plumbing

This change does not modify product source selection, firmware bytes or device
behavior. The historical minimal profile remains its own named matrix lane. A
second selected-full lane enables foreground idle, Files, native daily apps,
native alarms/geometry and Quick Actions. Jobs remain single-build parallelism;
permissions remain contents:read. No automatic merge or device operation exists.

The full lane retains the existing complete Runtime execution and admission
checks. It additionally runs the source-owned short-prepare HYBRID suite and
Springboard report-gap suite in normal and sanitized modes. Required receipts
are validated against the exact target digest, source pins, source/fixture hashes,
store and scenario lists before copying into package evidence. Expected-retained
baseline or partial receipts are not accepted as product recovery evidence.
Owning GPIO preparation and CST custody suites also run. The sealed image then
receives independent byte/header/region/blank-NVS/store checks, followed by actual
packaged AppData replay through the native LittleFS test in both lanes.
Post-seal receipts/logs are separate uploaded qualification evidence; no claim
is made that an earlier ZIP magically contains tests run after its creation.

Source recovery fetches only exact locked commits. Additional SDK/fixture refs
are read from their owning selected builder constants, plus the maintained USB
preservation lock; there is no fallback to main/latest. The new helper never
changes those constants or selection locks. Missing remote refs remain a real
failure until the owning source-delivery mapping is completed. A connector-created
identical-tree commit is a new identity: dependent locks/constants must be mapped
truthfully and the successor qualified. Historical firmware receipts must not be
relabeled as builds from new SHAs.

Local checks for this plumbing use a mocked orchestration runner (no full build
claimed), exact dependency-plan/strict receipt negative tests, the real owning
GPIO normal/sanitized fixture, and real image verification against the sealed
07046df image. Hosted Actions have not run for this change. Final source pins,
other upstream work, draft integration PRs and exact-head hosted CI remain the
owning integrator's delivery work.

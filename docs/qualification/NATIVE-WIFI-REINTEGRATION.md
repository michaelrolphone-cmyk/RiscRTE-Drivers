# Native Wi-Fi reintegration (source preparation)

Base: navigation-qualified Drivers `8d514ff65668c964885c9ed8ae08b216582eb724`.
Reused Wi-Fi builder/runner/recipe/evidence changes: preserved Drivers
`40dec7cade564ad9cca35c39c455dc579ba1cb61` relative to b7cef8d.

The source lock intentionally remains the current navigation cohort. This commit
is not a ready-to-build native-Wi-Fi product selection: final merged System,
Settings and matching Watch pins must be selected by the integrator. Do not
replace current Home or CST sources with the old Wi-Fi cohort. Independent
Settings both-timer selector/receipt changes are intentionally left to its owner.

`--native-wifi-geometry` remains explicit and requires the existing product Quick
cohort; omission preserves historical selection. Shared Home/Settings Hybrid
sleep defaults and selected sleep receipt validation remain unchanged. Wi-Fi
native flags/rotation, exact recipe provenance and 46-case receipt completeness
are validated in build/seal. CI adds Wi-Fi checks without removing the existing
Hybrid/navigation cohorts, exact recovery receipts, image preflight or AppData
replay. Native kernel reuse is not inferred from a UI selection.

The shared alarm execution fixture has explicit `BADGE_WIFI_EXECUTION` guards
around Wi-Fi-model dispatch. Only the expanded main execution fixture defines
it. Dedicated navigation/Hybrid fixtures keep their owning models; merely
reapplying the old unguarded Wi-Fi patch would break the newer navigation build.

Preparation qualification: Wi-Fi/Quick/Files/inventory/sleep-default/CI owning
unit checks, separate Hybrid/navigation evidence-gate checks, and syntax checks
of all three actual C++ execution fixtures against current Runtime/Alarm SDKs.
These are not the final target build or full Runtime execution results.

Final integration still requires exact target/admission checks, 46 normal and
46 sanitized installed executions, generic Alarm compatibility cases31/32/35/40,
current focused Hybrid/navigation recovery and custody checks, Wi-Fi owning
geometry/workflow/raster suites, Settings timer tests and updated modeled visual
review. Historical 40dec execution receipts do not qualify newly merged sources.
Physical Wi-Fi association, touch usability and sleep/power remain unverified.

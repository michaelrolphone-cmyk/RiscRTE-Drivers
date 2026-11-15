## Optional native daily-app presentation increment

When `native_daily` is true in the store receipt, Calculator, Battery and
Stopwatch use native glyph/primitives within the reported safe content rectangle.
Arithmetic, telemetry semantics, native UTC, namespace 2 and returning Light
remain the existing controllers. No new capability or app is added. Physical
input, sleep/wake and readability remain unverified.

# Supported-app successor (separate from sealed morning 66761a8)

When explicitly built with --include-files and --foreground-idle, this candidate
adds native Watch Files to the launcher. It browses only Runtime's read-only
allowlist of admitted application/driver manifests and ELFs. It can list, filter
and preview those files; it cannot alter installed firmware, access arbitrary
private AppData, or use absent SD/USB storage. PWR short presses are local Back; the long Home event returns from nested
views after safe cleanup. Foreground Light preserves the same invocation.

The package's generated APPLICATION-INVENTORY.json compares all discovered
source app profiles and intended Watch roles against the actual installed set.
A static capability match is not operational or hardware qualification. In
particular, OTA/App Store need update/bank composition, and sleeping Alarms/
Countdown need tagged native clients, service custody and timer wake. This
increment does not claim those omitted applications work.

The following inherited baseline capabilities are preserved. Exact receipts,
not this description, determine which successor qualification has passed.

# Prototype capability scope

Morning interaction candidate: retains the product-sized AppData mount, native
dark-until-complete-frame display gate, CST9217 handoff repair, and selected
180-degree panel transport. Home initializes an unset clock to January 1, 2026;
Settings can set actual time. Watch-native Springboard uses reported dimensions
and the shared native UTC/timezone service. Exact foreground Light selection is
recorded in store-proof.json. This new combined image is physically untested;
prior boot/display observations do not qualify these changes.


The exact package receipts define what passed. Until the complete package gate
passes, this file describes the selected candidate and is not a delivery receipt.

Selected shared application roles: unchanged maintained Home/default and Clock
return,shared Springboard,Settings with native time,Bringup Hardware Test,
Calculator,native-UTC Stopwatch,Battery and Wi-Fi Networks. Applications share
physical466×466 raster/touch coordinates;provider safe insets69each side give
328×328content. Insets are conservative software bounds,not measured visibility.

Selected real providers: scoped I2C,shared CO5300,existing passive DCDC1 observer,
CST9217,read-only AXP2101 battery,PMIC PWRON,transport-neutral navigation,
existing display-power bridge and shared native-station Wi-Fi. Battery
never writes PMIC policy. PWR owns onlyIRQ-enable/statusbits0x0c with owned-bit
restore/W1C acknowledgment. It does not read GPIO0 or cut sharedCPU/DCDC1.

Storage: NVS namespaces1(shared preferences),2(native Stopwatch),6(Wi-Fi);
AppData namespace61(Home crash records) in separately preformatted1MiB LittleFS.
Runtime preserves existing namespace identities,opaque encodings,commit/readback,
quota/atomic-replacement and retained-cleanup behavior. Namespace1 is intentionally
shared by Home/Settings;unrelated app data has separate namespaces. Bootfs is
read-only application/configuration storage. No wear lifetime or power-loss
endurance is claimed from host tests;no format/grow/erase-on-error at boot.

Civil-time authority is explicit Runtime UTC. No externalRTC or synthesizedRTC
exists. Settings validates timezone/DST and seed/readback. Cold/ordinary non-deep reset
leaves Runtime time UNSET, then the selected Home initializes2026-01-01 midnight
in the saved timezone (UTC when missing). This explicit placeholder is not
synchronized time; Settings supplies the actual date/time.
Stopwatch uses the existing nativeUTC controller and a separate record key;
missing or moved-back civil time is visible,not fabricated elapsed time.

True CPU deep sleep is selected through a scoped generic key-sleep provider.
The official 1.75C schematic routes PWRON through T1/BSS138 to SYS_OUT/GPIO3,
released LOW and pressed HIGH. GPIO3 is claimed input-only, with no added pull;
BOOT/GPIO0 is untouched. Shared Home and Settings default to the established
HYBRID sequence: brief PWR or configurable inactivity first enters Light; only
the subsequent configured Light interval timer may advance to Deep. A key wake
returns normally, and an earlier/equal alarm deadline interrupts escalation.
The idle fallback remains 15 seconds; the shared subsequent interval fallback
is 300 seconds. Valid saved mode and independent timers override defaults.
No touch, motion or radio wake is selected. Manual PWR sleep remains available. Stable key
release is required before arming; held/reasserted keys refuse entry rather than
creating reset loops. A physical PWR press caused by pocket pressure can still
wake the device: this is intentional-key-only wake, not a proximity detector.

Home drains input and display work, stages its existing checkpoint and blanks
then closes the panel. CO5300 reset is held low. CST9217 uses the source-backed
sleep command after its last subscriber and resets/reprobes on the next one.
No shared DCDC1 rail, PMIC charger or hard-power-off policy is changed. Native
entry refuses pending/retained AppData and active or unsafe native resources.
Synchronous NVS writes are committed/read back; AppData writes are synced,
closed and verified. Unsaved arbitrary application RAM is not serialized.

Runtime's seeded RTC-backed UTC survives admitted deep resets. It is deliberately
UNSET after cold/software/watchdog/brownout reset or invalid retention state;
the selected Home then applies the requested initial-date placeholder.
The selected RTC source is the internal RC oscillator; drift and actual current
are unmeasured. Deep wake creates a fresh shared Home invocation. Hardware wake,
voltage, board revision, timing and battery life remain qualification gates.
Bluetooth remains unselected. Home combined radio toggles may be unavailable
although the dedicated Wi-Fi app works independently.

QMI8658 motion diagnostics are intentionally unselected in this pocket/time
profile. Full Runtime integration showed deferred provider promotion enabling
125Hz acceleration immediately before deep entry. Its current sample API has no
sleep preparation contract, and byte order/scale remains physically unqualified.
The sampling implementation/tests remain in source for later motion work; the
current graph grants no samples. A separate exclusive idle-owner now establishes
the manufacturer-documented power-down state using only CTRL7[1:0] and CTRL1[0].
It handles supported ordinary inherited modes and already-confirmed power-down;
unknown command/activity/FIFO/SyncSample/WoM/self-test/reserved states refuse
before writes and do not qualify sleep. Active sensors settle for a checked
667ms bound before oscillator stop, then1ms; cached no-I/O readiness is granted
only after both readbacks and session release. Normal awake resume leaves the
no-motion profile idle; checked unload restores only the original owned bits.
This is a software mode/readback claim, not measured total-board current. Do not add motion back merely to make Hardware Test's optional
row available without adding and qualifying a bounded sleep lifecycle.

## Foreground battery scope

When `foreground_idle.enabled` is true in the exact store receipt, six shared
apps use cooperative returning Light sleep: Settings, Springboard, Wi-Fi
Settings, Calculator, Stopwatch and Battery. They preserve the same invocation,
private namespaces and unsaved draft without synthetic Home/Save/Cancel.
They use the shared saved idle interval, with the explicit build fallback only
when the preference is confirmed absent. Unsafe resources or legitimate active
work defer entry. See FOREGROUND-IDLE.md for exact retention and refusal rules.

Without that explicit build option, the historical Home-only scope is unchanged.
Hardware Test is not yet opted into the common boundary and can remain awake.
Light preserves RAM, not a durable checkpoint across reset or loss of power.
App-owned durable Deep recovery remains an actionable follow-on, not a condition
for reversible Light suspension. No overall pocket safety or current is claimed.

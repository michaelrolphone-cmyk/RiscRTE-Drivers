# Board-scoped panel lease and lifecycle interlock

This is a source migration of actual Reader TPS65185 power authority, composed
with the qualified PCA9535 source and current scoped I2C/Runtime graph. It is not
an alias for the native RGB565/SPI backend or a complete EPD engine migration.

## Source and authority

Reader9c8783d6f20659a514bfaeb3847a77c7213dc4f6 TPS driver blob
6d7445428a6e341c6983b352b376f6f3549922d9 and manifest blob
207f018cdae14aa0040ce30355d43fbf40b43eef identify original source0.1.0. Current
`Drivers/tps65185_power` source0.1.1 keeps chip register/power sequencing in its
ELF while changing admission and the previously colliding capability. No old
release artifact equivalence is inferred. `qualification/panel-lease-source.json`
pins the original and exact external source/header dependencies.

Three ordinary ABI2 providers constitute this increment:

- `tps65185-power` provides **display.panel-lease@1**, not display.power. Requires
  hardware.device, i2c.bus, gpio.expander, platform.clock, platform.sync and the
  explicitly selected board.display.tps65185.profile. Physical identity uses the
  existing peripheral.i2c@1 layout, compatible ti,tps65185, revision unspecified,
  address0x68, chip_id0, no IRQ. No unrelated typed layout is extended or cast.
- `t5s3-panel-profile` contains the recovered electrical data: outputs0x3b00,
  inputs0xc000, WAKEUP0x2000, power-good0x4000 and VCOM magnitude1600mV. It is an
  explicitly selected data provider, following the existing BQ profile pattern;
  no generic Runtime product-name branch or electrical sequence is introduced.
- `panel-lease-lifecycle` consumes that SAME lease authority and provides the
  unchanged **display.power@1 light-sleep prefix**. Lease and lifecycle callbacks
  are not cast between layouts. The deeper callbacks are absent and struct_size
  advertises only the supported prefix. No deep-sleep electrical retention or
  terminal-entry acceptance is claimed.

The board graph must explicitly select the physical I2C provider for the typed
bus instance, the qualified PCA instance for gpio.expander, and the intended
profile. These capability tables have no physical-identity introspection API;
the driver does not pretend that checking a header proves the loader's binding.
Independent physical devices require independent ELF state and scoped native
sync contexts. The host fixture uses explicit graph instances22/45/46 as
examples, not provider constants. Existing display.output and ROUND bit6 are
unchanged; this migration does not import Reader's conflicting QUIESCE_SLEEP bit.

## Lifecycle and custody

The exclusive generation token is installed before any power-up write. A failed
acquire can return a nonzero cleanup token. Confirmed safe rollback after a read
or bounded wait failure may clear the token; an unconfirmed write permanently
quarantines the source instance and retains dependencies. Release only clears a
token after safe outputs and guard cleanup are confirmed; stale/double/wrong
owner calls refuse. A failed release/unlock retains cleanup custody rather than
pretending the physical panel is off. Guard failures fence new admission.

Sleep preparation establishes a fence before checking for an active lease. If
one exists, preparation refuses and the caller MUST pair it with resume. No new
lease is admitted while fenced. Idle preparation performs the actual safe-output
sequence. A failed wait/off attempt leaves a dirty fence; resume retries safe
shutdown before removing that fence. Successful resume does not power up or
create a lease. The lifecycle adapter retains a failed/unfinished preparation
and refuses unload until paired cleanup succeeds. A caller must not dispose a
prepared lifecycle provider or retain a raw interface after graph revocation.

The recovered GPIO-mask ordering remains: clear non-WAKEUP control outputs,
cooperate for at least the checked one-millisecond interval, then clear WAKEUP.
Power-up readiness has both400-iteration and400ms bounds. Scheduler-only
platform.clock's existing tagged wait suffix is required; legacy sleep_ms is
not used, avoiding nested provider polling while the native guard is held.
Early/stalled/backward/invalid clock observations refuse rather than claim an
elapsed delay. Native synchronization is owner-task-only, not worker/IRQ access.

The profile currently supports active-HIGH control outputs with safe LOW states
and active-HIGH power-good. Masks must be disjoint, WAKEUP/power-good must each
be one bit within the appropriate mask, and VCOM uses chip-supported10mV steps.
Different electrical polarity/sequence requires explicitly qualified source,
not silently treating a profile as universal rail policy. TI's TPS65185 data
sheet specifies the VCOM0..5110mV magnitude range in10mV increments:
https://www.ti.com/lit/gpn/TPS65185 . The selected1600mV profile is recovered
source evidence, not a new calibration or measured panel setting.

## Executable qualification

Run `python test/run_panel_lease_composition.py --runtime PATH --output receipt.json`
(and repeat with `--sanitize`). Normal checkout uses its own PCA/I2C sources.
For sparse retained evidence, explicit `--dependency-drivers` and `--pca-source`
paths can be supplied; there is no workspace search. Runtime headers win shared
names; only required Drivers/new capability headers supplement them.

All eight compiled translation units are scanned with their actual compile
flags, including C PIC/visibility flags. The45 non-system inputs comprise39
remote-pinned external sources/headers and six candidate sources/headers.
Unexpected/mutated/missing external inputs refuse before compilation. The
sixteen process-isolated scenarios exercise actual TPS/profile/lifecycle/PCA/I2C
provider bodies plus current Runtime GraphV2/ModuleV2, including:

- malformed/duplicate dependencies, wrong typed identity/profile/wait suffix;
- eager lifecycle activation and wrong owner, stale generation across restart,
  double release and exclusive lease refusal;
- unsuccessful start returning chip, PCA pin or native lock cleanup tokens;
- external/internal readiness timeout, stalled acquire clock and safe rollback;
- live-lease sleep refusal with required resume, failed prepare/rollback waits,
  stalled release and failed unlock cleanup;
- uncertain TPS/PCA writes and attempted prepared-provider disposal.

Only native owner/scheduler and lowest I2C registers are simulated. Four
retained-chain cases intentionally expect current graph destructor SIGABRT after
checking custody; they do not force-unload hardware or report successful
recovery. Host passes are not physical power, waveform, bus, retention, deep
sleep or complete product acceptance.

`scripts/build_panel_lease.py --runtime PATH` builds the three current packages
using existing compiler selection, SDK staging and relocation normalization.
Each ELF/manifest/import/export set is checked; aggregate provenance records
source/header/compiler dependencies and tools. Only dist/panel-lease is written;
consumer receipts are preserved alongside target outputs. The new workflow
produces actual target and normal/UBSan consumer artifacts for verification.
Historical packages, frozen custody inputs, current native private-ABI closure,
existing lifecycle headers and ROUND semantics remain untouched. Worker/cache/
DMA and the recovered EPD rendering engines remain separately scoped work.

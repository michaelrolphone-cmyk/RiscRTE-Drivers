# PCA9535 scoped GPIO authority

`Drivers/pca9535_gpio` adapts Reader9c8783 source0.1.0 to current native ordinary
provider0.1.1. Original driver Git blob38cfc2373a55dd7608b28d39f4a33a5964707374
and original manifestf6c1a4677771d55192a26886b53d6d74dd8526fb remain distinct from
the adaptation. No historical release artifact equivalence is asserted.

The provider requires exactly hardware.device@1, i2c.bus@1 and platform.sync@1.
Select compatible `nxp,pca9535`, revision `unspecified`, existing typed
`peripheral.i2c@1`, address0x20..0x27, chip_id0, and no IRQ (irq=-1; polled input).
The accepted bus is I2C0/1, <=400kHz, with distinct valid SDA/SCL and no SPI pins.
There is no board-name check or fixed board pin assignment. Board validation and
explicit dependency selection must bind the typed device's bus.instance_id to
its actual i2c.bus provider. The I2C capability has no bus-identity query; this
driver does not pretend header admission proves the loader's selection.
Independent devices require independent ELF state and scoped sync tables as
specified by hardware.device; do not restart a live singleton for another chip.

One PCA instance owns the chip address. Eight generation-tagged nonoverlapping
pin grants expose only their input samples/output masks. Output latch safe bits
are set before output direction, and read-modify-write preserves other pins.
A partial claim returns a nonzero cleanup token. Failed physical writes are
uncertain and quarantine the provider with dependencies pinned; later success
must not silently erase that uncertainty. Failed reads are not writes and allow
checked cleanup. Release restores only that grant's safe output bits, not a
whole-bank snapshot. Double/stale releases fail. Exhausted generations refuse.
The native owner-only synchronization service replaces the recovered provider's
PSRAM atomic spin/wait state; it is not a cross-thread GPIO API. Failed unlock
fences new work and retains child tokens for checked cleanup; failed destroy or
bus release retains the dependent authority. No firmware peripheral imports or
new private OS/CPU ABI are added.

`test/run_pca9535_composition.py --runtime PATH --output receipt.json` compiles
this actual provider, current Drivers `components/i2c-scoped/driver.c`, and exact
Runtime13eca114 GraphV2/ModuleV2. Every non-system dependency of all five compiled
translation units is scanned using the same compile prefix and compared with
33 pinned external inputs in source-map.json. The three candidate translation
unit/header inputs are recorded separately. `--sanitize` adds UBSan. The
optional explicit `--dependency-drivers` supports retained sparse workspaces;
normal repository/CI use defaults to the current checkout. Runtime shared SDK
headers win; only missing Drivers headers and the new expander header are added.

Sixteen scenarios cover malformed/duplicate dependencies and typed admission,
wrong selected bus before I/O, independent same-address I2C-claim refusal,
two additional valid addresses, owner/overlap/
stale generation and double release, partial claim, read failure, uncertain
claim/write/release, unlock failure, destroy/close retry, failed create returning
a token, and failed-start read cleanup. Uncertain-write cases deliberately
expect the actual graph destructor's SIGABRT after checking its retained chain;
this demonstrates refusal to discard live code, not successful recovery.
Only lowest controller registers and native guard operations are simulated.
These host checks do not qualify physical polarity, electrical levels, timing,
board wiring, IRQ behavior, or a deployed panel.

`scripts/build_pca9535_gpio.py --runtime PATH` uses the existing native compiler
selector, SDK staging helper and relocation normalizer. It writes only
`dist/pca9535-gpio`, validates the exact current manifest/ELF/export/import set,
and records selected SDK/source/toolchain inputs plus compiler/libgcc/readelf
hashes. The workflow retains normal+UBSan receipts beside target build evidence.
It does not overwrite historical package outputs or alter current admission.

TPS65185 panel lease/lifecycle integration remains a separate dependent task.
This package alone does not add the TPS board profile, change display.power,
reinterpret ROUND, implement worker/cache/DMA, select a board, or prove hardware.

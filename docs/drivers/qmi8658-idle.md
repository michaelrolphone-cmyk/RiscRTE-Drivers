# QMI8658 idle owner for the no-motion pocket profile

Experimental source, not measured device qualification. This provider is
mutually exclusive with every motion/sample/wake writer of this device. The
profile must omit motion grants/providers and require this guard before CPU
sleep. It does not implement sensor sampling or a universal inherited-mode reset.

## Pinned evidence

[QST QMI8658C Rev A, document13-52-27, June20,2022](https://www.qstcorp.com/upload/pdf/202210/13-52-27%20QMI8658C%20Datasheet%20Rev%20A%20%281%29.pdf),
SHA256 `9887d8827b1769dff31ad5d5c0c4672458d593af5309aa0ed183d38440f7defd`:

- Table22, printed pages30/33: CTRL1 address02 SensorDisable is **bit0**;
  CTRL7 address08 aEN/gEN are bits0/1. CTRL1 bit1 is reserved and bit7 is SIM.
- Table31, page49: power-down is SensorDisable=1 and aEN=gEN=0. Digital
  interfaces remain accessible and configuration is preserved.
- Figure16 and section7.3, pages50–51: turn sensors off before stopping the
  oscillator; sensor shutdown t6 is at most2/ODR. Lowest listed valid rate3Hz
  gives a conservative ceil(2000/3)=667ms bound. The sensors-off transition
  t7 is below100us at400kHz, rounded upward to1ms. Leaving power-down has
  a documented maximum15ms t0 before sensor enables are restored.
- Section13.4 requires an additional AHB command to exit SyncSample. Section12.6
  specifies a separate WoM threshold/CTRL9 exit. This owner does not normalize
  those modes or issue those commands.

The earlier `qmi_powerdown.c` reproduction used CTRL1=0x61, correctly testing
SensorDisable bit0. No historical review needs rewriting as a bit7 defect.
The independent A-part datasheet agrees on these common power bits. The official
1.75C SensorLib at commit `4bfdc80eedbf95031e384610796bed15e0240312`,
SensorQMI8658.hpp blob `e93015e7b256cf6cd0529c50e1337357729fb0d2`, lines955–965,
sets/clears reserved CTRL1 bit1 in powerDown/powerOn. That helper is **not copied**.

## Admission and exact algorithm

Uses existing driver ABI2, peripheral.i2c v1, scoped i2c.bus and platform.clock.
The new local header `components/co5300/include/RiscPowerSleepGuardV1.h` exports
power.sleep-guard@1: version, size, context, cached bool ready callback. No frozen
SDK header is edited. Compatible is qst,qmi8658-idle; accepted address6a/6b,
WHO_AM_I5,400kHz, no IRQ.

1. Read identity/configuration under an authorized address claim. An exact
   existing power-down state is accepted with arbitrary preserved configuration,
   zero writes and no speculative reset or mode normalization.
2. Otherwise require documented ordinary modes. Reject contradictory enable /
   SensorDisable state, reserved CTRL1 bit1, SyncSample/reserved CTRL7 bits,
   self-test, active command, FIFO read/active/reserved mode, activity controls,
   unsupported enabled ODR or full-scale values. Active low-power/WoM modes are
   outside this bounded implementation. Rejection happens before writes.
3. Save only original CTRL1 bit0 and CTRL7 bits0:1 for restoration. RMW-clear
   CTRL7 bits0:1 and check readback. If previously enabled, wait667ms with valid,
   nondecreasing monotonic time proving the requested elapsed interval.
4. RMW-set CTRL1 bit0, check readback, wait a checked1ms, then reread both owned
   fields. Publish readiness only after successful address-session release.
5. `ready()` performs **no I/O, waits, logging or cleanup**. Its cache is valid
   only while this exclusive no-motion graph remains active. Ordinary awake
   resume or sleep rollback leaves the sensor idle, not restored to sampling.
6. Graph unload clears readiness first. It disables/settles any enabled sensors,
   restores the original oscillator bit, waits15ms when restoring oscillator-on
   (1ms for off), then restores original enable bits. All writes RMW-preserve
   unrelated bits. Failed or uncertain writes, clock checks or releases retain
   cleanup obligations and dependencies; readiness remains false.

False/nonzero claims are cleanup-only and never authorize I/O. Failures emit
bounded `QMI_IDLE reason=...` diagnostics. A failed guard is **not** evidence of
low power. No writes to CTRL2/3/5/8/9, IRQ, reset, PMIC or rails are permitted.

## Reproduction

`bash test/run_qmi8658_idle_test.sh RUNTIME_CHECKOUT` compiles source and runs
normal plus ASan/UBSan. Coverage: all65536 CTRL1/CTRL7 combinations,1024 enable /
ODR combinations,64 full-scale combinations,256 arbitrary-config existing-idle
cases, every start/unload I/O fault, acted-but-false writes, readback faults,
clock faults, retained claims/releases and retry. The model enforces owned masks,
ordering/timing, no-I/O readiness and restoration of unrelated-bit changes.

Actual silicon identification, physical mode acceptance, hidden prior feature
state, shutdown timing and total-board current remain hardware gates. Register
readback is not a current measurement. The former optional sample provider is
retained separately but must not be selected alongside this owner.

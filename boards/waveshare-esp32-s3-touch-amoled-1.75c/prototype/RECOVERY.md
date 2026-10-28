# Recovery and backups

The exact 1.75C board has **PWR** and **BOOT** side buttons. It does not have
a discrete RESET button. The official schematic identifies BOOT as Key1/GPIO0,
PWR as Key2/AXP2101 PWRON, and RST as test pad TP11, not a button. Do not follow
the earlier generic “hold BOOT, tap RESET” wording as if this enclosure had a
RESET switch; do not short test pads as a recovery instruction.

Sources:
- https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.75C
- https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.75C/FAQ
- https://files.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.75C/ESP32-S3-Touch-AMOLED-1.75C-schematic.pdf

For an ordinary restart, do not hold BOOT: its documented role is selecting ROM
download mode during power-on. Use the owner's verified normal power-cycle
method. No unverified PWR press duration is prescribed here. If ROM download
recovery is needed, follow the exact board's current official instructions and
check the USB data cable first. Security-protected devices need their existing
authorized recovery process; do not bypass it.

Retain the owner-authorized full backup and the earlier display/touch Home
candidate as separate artifacts. Earlier Home-only recovery image identity:
33554432bytes, SHA256
`841eda67d54afe1e62d6ad697f6d36b22a9b20b7e6f375ec733d20791e9eeb42`.
Its ZIP is `badge-flash-candidate-f1b207ac.zip`, SHA256
`2f06645931dde1d9f0c85e6e88bc2d4a4378c0dbdd4ed1160b93eaf6b79c9e8a`.
It has the older bootfs-only partition plan. It is software-qualified, not a
hardware-known-good boot. Reinstalling either full image destroys user data and
replaces the partition table; never mix their filesystem images or offsets.

New layout: bootloader0, partition table0x8000, NVS0x9000/0x6000,
native0x10000/0x300000, bootfs0x310000/0x1bf0000,
AppData0x1f00000/0x100000. Runtime will not format or grow a corrupt AppData
volume on boot. Preserve the failed image/logs before an authorized reinstall.
No erase-first, force, ignore-flash-encryption or security-disable step is part
of this procedure. Recovery commands are owner instructions, not actions taken.

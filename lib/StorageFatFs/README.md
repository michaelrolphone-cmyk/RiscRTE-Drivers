# Shared storage.volume filesystem implementation

This directory is the canonical reusable migration of the volume/FatFs source
recorded in `UPSTREAM.json`. The older T5S3-Reader tree remains unchanged.
Providers include `volume.c` in their serialized storage owner after defining
the existing transport and lifecycle hooks. The X4 provider now builds against
this directory through `minimal/scripts/build_storage_fs.py`.

`fs_extensions.inc` implements `RiscStorageVolumeFsV1` under that same guard.
Directory tokens are FatFs offsets, not replay counts. They are valid only for
the same unmodified directory on the same mounted medium. `f_seekdir` validates
the offset using FatFs locking, directory state and sector lookup.

Metadata distinguishes not-found from I/O failure and reports FAT's local-time
timestamp explicitly. Truncation only shrinks a writable file, synchronizes it,
and preserves its cursor when possible. No sparse extension is implied.

Replacement requires closed files in one directory. The provider temporarily
renames the old destination to `<destination>.risc-replace`, installs the synced
source, then removes the backup. Recovery restores the backup when the
destination is absent or discards it when the destination exists. The suffix is
reserved; callers must not use it as an operand. Operations are recoverable
across interruption under FatFs metadata consistency, not a claim of multi-file
atomicity or immunity to damaged media. Failed I/O preserves recovery evidence
and follows the existing retained-custody rules.

X4's wire-model tests exercise this production source, including seek/reopen,
metadata, truncation, replacement, reset recovery, sharing/owner checks, failed
writes, lock failure, SD export and sleep. Neither this source nor those host
tests qualify physical media/power-loss behavior.

FatFs R0.15, upstream https://elm-chan.org/fsw/ff/arc/ff15.zip
Archive SHA-256: e0d76654d877e6c74be5ea3c395808794d495169514e98cbf6046168b8f4f070

Built into the provider ELF, never delegated to firmware. Configuration: UTF-8 long names (127 UTF-16 units), one volume, FAT12/16/32, fixed LFN workspace, 24 object locks, no exFAT/formatting/RTC. Provider entry points serialize access. Local ff.c patches reject self-linked FAT clusters and add bounded cooperative checkpoints to FAT and directory traversal; timeout is an I/O failure, not EOF. Original license retained.

Text files use normalized LF line endings; incidental upstream indentation/trailing whitespace is normalized.

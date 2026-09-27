# usb-mass-storage

## Purpose and scope

`usb-mass-storage` is the ABI-v2 `storage.volume@1` provider for USB Mass Storage Class devices using Bulk-Only Transport and the SCSI transparent command set. It depends on `usb.host@1` for USB enumeration, interface claims, control transfers, and bulk transfers. The driver owns Mass Storage Class matching, BOT/SCSI framing, removable-volume state, FAT16/FAT32 mounting, directory traversal, sequential file reads, exclusive file creation/writes, file deletion, and filesystem metadata updates.

The implementation exposes one mounted removable volume at a time. It does not provide a generic block-device capability; `storage.volume@1` is a filesystem-level interface.

## Package identity and inspected release state

- Driver/package ID: `usb-mass-storage`
- Version: `0.1.1`
- Driver ABI: `2`
- Architecture: `xtensa-esp32s3`
- Source path: `Drivers/usb_mass_storage`
- Upstream source tree SHA: `f1e1184823b0257a61c4c3c9d35c9091068674f5`
- `driver.c` blob: `3164a693fc8dbe2ced19d25f8842ac932d687e5b`
- `manifest.json` blob: `cbecb7f1e4210dd6e5bde77cc6cec8901aa52ea8`
- `RiscStorageVolumeV1.h` blob: `69b7a67999a2ae52c87c96c4efab0bec0918c3e6`
- Exact upstream host fixture blob: `81bdd8e689a39b8c7e0036f09ba6d52bf83cae21`
- Requires: `usb.host@1`
- Provides: `storage.volume@1`
- Source-manifest status: `experimental-unpublished`
- Published tag: `driver-usb-mass-storage-v0.1.1`
- Canonical `driver.elf`: 23,792 bytes
- Canonical ELF SHA-256: `af0cd0a820f8b163c8cf2efff9890c5ccfa538a2339a2411d6a43135172ac3a6`

The source manifest still labels this package `experimental-unpublished`, while the inspected release index publishes v0.1.1. Those are separate observed facts.

## Migrated files and build path

- `Drivers/usb_mass_storage/driver.c` is copied byte-for-byte from the inspected upstream master.
- `Drivers/usb_mass_storage/manifest.json` is copied byte-for-byte from upstream.
- `sdk/driver/RiscStorageVolumeV1.h` is the filesystem-volume capability ABI required by the driver.
- `sdk/driver/RiscProviderV2.h` and `sdk/driver/RiscUsbControllerV1.h` already existed here and are byte-identical to the inspected upstream headers.
- `test/drivers/usb_mass_storage_test.c` is the exact upstream deterministic BOT/FAT16 fixture.
- `scripts/build_usb_mass_storage.py` independently builds the Xtensa ELF, normalizes relocations, validates architecture/export/import shape, and requires exact canonical release-byte parity.
- `.github/workflows/driver-parity.yml` compiles and runs the host fixture, runs the standalone Xtensa builder, and uploads the produced package directory.

The standalone build uses the pinned ESP32-S3 Xtensa GCC toolchain provisioned through PlatformIO, `-std=c11`, `-Os`, PIC/long-call flags, hidden default visibility, no standard startup/runtime, shared ELF output, the repository driver SDK include path, libgcc, and the repository relocation normalizer.

## Exported root symbol and provider descriptor

The sole intended global function export is `t5_driver_get(uint32_t abi)`. It returns the static `risc_driver_v2` only when the requested ABI equals `RISC_PROVIDER_DRIVER_ABI_V2`.

The descriptor advertises:
- driver ID `usb-mass-storage`;
- capability `storage.volume`;
- capability API `RISC_STORAGE_VOLUME_API_V1`;
- provider ABI `RISC_PROVIDER_DRIVER_ABI_V2`;
- `start`, `stop`, and `quiesce` callbacks;
- a static `risc_storage_volume_api_v1` capability table.

The standalone builder requires `t5_driver_get` to be the sole global function export and exactly `memcpy` and `memset` as unresolved runtime imports. The observed published `privileged-imports.v1` file is 14 bytes, consistent with that import set.

## storage.volume@1 ABI

`RiscStorageVolumeV1.h` defines:
- API version 1;
- `RISC_STORAGE_VOLUME_NAME_MAX = 128`;
- 32-bit directory and file handles, with zero reserved as invalid;
- `risc_storage_dirent_v1` containing a 128-byte name buffer, 64-bit size, and directory flag;
- `refresh`, `ready`, `label`, and `stat`;
- `dir_open`, `dir_next`, and `dir_close`;
- `file_open_read` and `file_read`;
- `file_open_write`, `file_write`, and `file_close`;
- `remove`;
- `last_error`.

The ABI explicitly states that creation is exclusive: an existing destination must be rejected. `file_close(commit=false)` is defined to remove an incomplete destination and release its clusters.

## Dependency binding and lifecycle

`start` accepts exactly one dependency and rejects a second start. The dependency must be `usb.host` at API 1 with a non-null interface. The host table must report API 1, be at least the size of `risc_usb_host_discovery_v1`, and provide `configuration`, `claim`, `release`, `control`, `bulk_read`, `bulk_write`, discovery `poll`, and `devices` callbacks.

The driver does not mount media during `start`. The consumer calls `refresh` to service USB discovery and media state.

`quiesce` returns false while a file handle or directory handle is active. When no filesystem handle is active, `quiesce` detaches the current volume, releases the USB interface claim if present, clears mounted media geometry, and returns true. `stop` calls `quiesce` and leaves the dependency bound if quiescence fails; otherwise it clears the `usb.host` pointer.

A USB detach observed through `refresh` invalidates active file/directory state as part of `detach_volume`, marks the volume unready, releases the current claim, and clears device/interface/media geometry.

The source uses file-static mutable state and contains no locks or atomics. It therefore establishes no internal multi-thread safety guarantee. It supports at most one active directory iterator and one active file handle globally.

## USB discovery and matching

`refresh` delegates bounded host discovery with `max_events = 16`, then snapshots at most `RISC_USB_HOST_MAX_DEVICES` device tokens.

If the currently mounted device token disappears from that snapshot, the driver detaches it. If no device is mounted, `refresh` tries current host devices in snapshot order and stops at the first one `attach_device` can mount.

The implementation performs no VID/PID allowlisting. Matching is descriptor-class based:
- interface class `0x08` (Mass Storage);
- interface subclass `0x06` (SCSI transparent command set);
- interface protocol `0x50` (Bulk-Only Transport);
- one bulk IN endpoint;
- one bulk OUT endpoint.

Endpoint matching checks transfer type and direction; the source does not impose a particular endpoint number or VID/PID. The selected interface number and alternate setting are claimed through `usb.host` before protocol traffic begins.

The configuration parser walks descriptor boundaries and rejects structurally impossible descriptor lengths. It returns the first qualifying MSC interface that has both bulk directions.

## LUN behavior

After claiming a qualifying interface, the driver sends GET_MAX_LUN as a class/interface control read:
- `bmRequestType = 0xa1`;
- `bRequest = 0xfe`;
- `wValue = 0`;
- `wIndex` equal to the selected interface;
- one-byte data buffer;
- 250 ms timeout.

The implementation initializes `lun` to zero and continues using LUN 0. Even when GET_MAX_LUN returns one valid byte no greater than 15, the source does not select a nonzero LUN.

## Bulk-Only Transport framing

BOT commands use:
- a 31-byte Command Block Wrapper;
- signature `0x43425355`;
- a monotonically incremented nonzero 32-bit tag;
- direction flag `0x80` for IN and zero for OUT;
- CDB lengths 1 through 16;
- the selected LUN;
- an optional data phase;
- a 13-byte Command Status Wrapper;
- CSW signature `0x53425355`.

A command succeeds only when the returned CSW has the expected signature and tag, zero residue, and success status zero.

`bulk_write_exact` and `bulk_read_exact` loop until the full requested transfer length is completed. Any zero/negative result, or a host result larger than the remaining request, fails the operation. All BOT bulk calls use a 1,000 ms timeout.

## SCSI subset and sector model

The implemented SCSI operations are:
- TEST UNIT READY (`0x00`), retried up to four times while attaching;
- READ CAPACITY(10) (`0x25`);
- READ(10) (`0x28`), one sector per command;
- WRITE(10) (`0x2a`), one sector per command.

READ CAPACITY must report a logical block size of exactly 512 bytes. A returned last-LBA value of `0xffffffff` is rejected. The filesystem implementation therefore operates only on 512-byte logical sectors.

No other SCSI command support is established by this source.

## FAT mount behavior

The driver supports FAT16 and FAT32. FAT12 is rejected.

Mounting first attempts to parse LBA 0 directly as a FAT boot sector, allowing a superfloppy layout. If that fails, it reads an MBR-style sector at LBA 0 and considers up to four partition entries. A candidate partition must have a nonzero type byte, nonzero start/count, and remain within reported device capacity.

BPB validation requires:
- boot-sector signature `0x55aa`;
- bytes-per-sector exactly 512;
- sectors-per-cluster a nonzero power of two no greater than 128;
- nonzero reserved-sector count;
- one or two FAT copies;
- nonzero total-sector and FAT-size values;
- computed data region inside the volume;
- at least 4,085 data clusters and fewer than `0x0ffffff5` clusters.

Cluster count selects FAT16 below 65,525 clusters and FAT32 at or above that threshold. FAT16 requires a nonzero fixed-root entry count. FAT32 requires a valid root cluster and a zero fixed-root entry count.

Mounted geometry stores partition LBA, total sectors, FAT start/size/count, root layout, data start, cluster count, FAT kind, sectors per cluster, root cluster, and allocation hint.

## FAT allocation and mutation

FAT entries are read and written through 512-byte sector I/O. When two FAT copies are present, `fat_set` updates both copies.

Cluster allocation linearly scans from an allocation hint, wraps within the valid cluster range, marks a free cluster end-of-chain, zero-fills every sector in the new cluster, and advances the hint. Exhaustion sets the error text `USB storage is full`.

`free_chain` walks a cluster chain with a guard bounded by `cluster_count`, clears each FAT entry, and stops at end-of-chain or invalid continuation.

FAT32 directories can be extended by allocating and linking a new cluster. The FAT16 fixed root cannot grow beyond its fixed root-directory sectors.

## Directory scanning and names

Directory scanning is bounded by `FAT_MAX_DIR_SCAN = 8192` entries.

The scanner:
- stops at the FAT end marker;
- skips deleted entries;
- assembles FAT long-file-name entries only when ordinal sequence and checksum are consistent;
- skips volume-label entries;
- falls back to 8.3 short-name decoding when an LFN chain is invalid or absent;
- excludes `.` and `..`;
- reports directory attribute, first cluster, 32-bit file size, and on-disk entry positions.

Name lookup is ASCII case-insensitive through the implementation's simple lowercase conversion. It does not implement full Unicode case folding.

Paths must be absolute and begin with `/`. Multiple slash separators between components are tolerated by the path parser. The root path `/` resolves as a directory.

Returned API names are bounded by the 128-byte `RISC_STORAGE_VOLUME_NAME_MAX` buffer.

## File creation and long-file-name encoding

New files are created exclusively; an existing path is rejected with `Destination already exists`.

Creation converts the final path component from UTF-8 to FAT UTF-16 long-name units. The converter validates UTF-8 sequence shape, rejects overlong encodings, rejects UTF-16 surrogate code points encoded directly, accepts scalar values through U+10FFFF, and creates surrogate pairs for non-BMP characters.

The source rejects FAT-forbidden name characters/control characters, names ending in space or period, empty names, and names that exceed its LFN capacity. The implementation permits at most 20 LFN directory entries for a new name.

A generated 8.3 alias is derived from a name hash plus an attempt suffix and optional extension. Up to 36 alias attempts are tried. Creation requires a contiguous run of LFN entries plus one short entry within the bounded directory scan.

The new short entry starts as a regular archive file with cluster zero and size zero.

## Directory API

`dir_open` requires a ready volume, no already-active directory iterator, and a resolvable directory path. It allocates a nonzero monotonically increasing handle.

`dir_next` validates that handle, returns one non-volume-label/non-dot entry at a time, copies its bounded name, size, and directory flag, and advances the stored index.

`dir_close` deactivates the iterator when the supplied token matches. Only one directory iterator may be active at once.

## File read API

`file_open_read` requires a ready volume, no active file handle, an existing non-directory path, and a valid first cluster for nonempty files. It returns a unique nonzero handle and reports file size.

`file_read` is sequential. It advances through FAT clusters as the position crosses cluster boundaries, reads one sector at a time, copies only the requested/file-remaining bytes, and returns the number of bytes copied. An impossible/broken continuation sets `Broken FAT file chain`. Sector read failure sets `USB read failed`.

There is no seek callback in `storage.volume@1` and no seek implementation in this driver.

## File write and commit/abort semantics

`file_open_write` creates a new empty namespace entry. It does not overwrite or truncate an existing file. It resolves the parent directory, creates LFN/short entries, and then opens the single writable file state.

`file_write` is sequential. It allocates clusters as needed, preserves unaffected bytes for partial-sector writes by reading the current sector first, writes 512-byte sectors through SCSI WRITE(10), and updates in-memory file size. It rejects growth beyond the 32-bit FAT file-size field with `FAT file exceeds 4 GiB`.

`file_close(commit=true)` writes the final first-cluster and size fields into the file's directory entry.

`file_close(commit=false)`, and a failed commit path, delete the namespace entries before releasing the allocated cluster chain. The source explicitly orders cleanup so a FAT cleanup failure can leak space but cannot leave a live directory entry pointing at clusters that might later be reallocated.

The file state is zeroed after close regardless of the success result.

## Removal

`remove` is limited to files. It refuses operation while any file or directory handle is active, rejects directories, and rejects FAT read-only entries. It resolves the parent, marks the LFN/short namespace entries deleted, then releases the file cluster chain.

The source does not implement directory creation, directory deletion, file rename, or move operations in `storage.volume@1`.

## Error reporting

The provider keeps one 96-byte error string. `last_error` returns false when no error is currently stored or when called without a valid destination/capacity.

Explicit errors established in source include:
- `USB storage is full`
- `Unsupported FAT filename`
- `Filename is too long`
- `Could not allocate FAT alias`
- `Directory has no free entries`
- `USB drive is not FAT16/FAT32`
- `USB host poll failed`
- `USB device enumeration failed`
- `Broken FAT file chain`
- `USB read failed`
- `Destination already exists`
- `Destination folder not found`
- `FAT file exceeds 4 GiB`
- `USB write failed`

Not every false-return path writes a new error string, so consumers must not assume `last_error` changes after every failed operation.

## Deterministic host validation

The migrated test is the exact upstream `usb_mass_storage_test.c` fixture. It dynamically loads a host build of the driver and supplies a deterministic `usb.host` simulation.

The fixture establishes:
- provider ABI/driver identity and one-dependency start gating;
- successful discovery/claim of a qualifying MSC interface;
- BOT CBW/CSW framing and endpoint routing;
- READ CAPACITY with 512-byte sectors;
- FAT16 mounting;
- ready state and the literal label `USB Storage`;
- stat of an existing `TEST.TXT` file;
- root directory enumeration;
- sequential read of the existing five-byte file;
- exclusive creation of `Copied File.bin`;
- a 600-byte write split across calls and subsequent full readback;
- committed close;
- file removal;
- aborted creation using `file_close(commit=false)`;
- hot-unplug refresh, readiness loss, claim release, and final quiescence.

The fixture's success output is:

`USB MSC BOT + FAT16 browse/read/write/delete/hotplug: PASS`

This fixture validates deterministic FAT16 behavior. FAT32 support is established by the inspected implementation source but is not exercised by this host fixture. The fixture also does not establish behavior against physical USB media or controller timing.

## Standalone Xtensa validation

`scripts/build_usb_mass_storage.py` validates the exact source manifest, builds a 32-bit little-endian Xtensa `ET_DYN` driver, normalizes supported Xtensa relocations, requires `t5_driver_get` as the sole global function export, and requires exactly `memcpy` and `memset` as unresolved runtime imports.

The build records generated size and SHA-256 and fails unless they exactly equal the observed canonical release: 23,792 bytes and SHA-256 `af0cd0a820f8b163c8cf2efff9890c5ccfa538a2339a2411d6a43135172ac3a6`.

## Published package metadata

Observed v0.1.1 package files:
- `.package.json`: 627 bytes, SHA-256 `5f543e99611749c973f58f7c4d7bd88d92b11cd9f708e49ba15b618504bb9d16`
- `driver.elf`: 23,792 bytes, SHA-256 `af0cd0a820f8b163c8cf2efff9890c5ccfa538a2339a2411d6a43135172ac3a6`
- `provider-abi.v1`: 43 bytes, SHA-256 `e16b6352d537660ad2da358814e8e22f8f0488febe22851c05012d371a503df4`
- `privileged-imports.v1`: 14 bytes, SHA-256 `a46fff766cb063bc348b3e7a6670e4a48c5c9201f0da92e73a67670229be7cca`

## Established limitations

- One mounted USB MSC volume at a time.
- LUN 0 only.
- Bulk-Only Transport with SCSI transparent command set only.
- Fixed 512-byte logical sectors.
- FAT16 and FAT32 only; FAT12 is rejected.
- One active directory iterator and one active file handle globally.
- Sequential file I/O only; no seek.
- New-file creation is exclusive; no overwrite/truncate path.
- File removal only; no directory creation/removal, rename, or move capability.
- Directory scanning is bounded to 8,192 entries per scan.
- New LFN creation is bounded to 20 LFN entries, and the ABI returns at most 127 bytes plus a terminator per name.
- There is no internal locking/atomic synchronization.
- The deterministic migration fixture exercises FAT16, not FAT32 or physical USB hardware.

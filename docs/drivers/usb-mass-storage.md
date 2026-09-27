# usb-mass-storage

## Scope and identity

`usb-mass-storage` v0.1.1 provides `storage.volume@1` above `usb.host@1`. The implementation owns USB Mass Storage Bulk-Only Transport framing, SCSI block commands, media discovery, and FAT16/FAT32 filesystem semantics for one removable volume.

Verified current upstream identity:
- tree `f1e1184823b0257a61c4c3c9d35c9091068674f5`
- source blob `3164a693fc8dbe2ced19d25f8842ac932d687e5b`
- manifest blob `cbecb7f1e4210dd6e5bde77cc6cec8901aa52ea8`
- host fixture blob `81bdd8e689a39b8c7e0036f09ba6d52bf83cae21`
- storage ABI header blob `69b7a67999a2ae52c87c96c4efab0bec0918c3e6`
- driver ABI 2; `xtensa-esp32s3`
- source status `experimental-unpublished`

The published v0.1.1 ELF is 23,792 bytes with SHA-256 `af0cd0a820f8b163c8cf2efff9890c5ccfa538a2339a2411d6a43135172ac3a6`. Package records also establish `.package.json` 627 bytes / `5f543e99611749c973f58f7c4d7bd88d92b11cd9f708e49ba15b618504bb9d16`, `provider-abi.v1` 43 bytes / `e16b6352d537660ad2da358814e8e22f8f0488febe22851c05012d371a503df4`, and a 14-byte privileged-import list with SHA-256 `a46fff766cb063bc348b3e7a6670e4a48c5c9201f0da92e73a67670229be7cca`.

## Public API and fixed resources

The volume API exposes refresh/readiness, label, path metadata, directory iteration, sequential file input, exclusive file creation/output, close/commit behavior, namespace mutation, and a bounded diagnostic string. Directory and file handles are nonzero 32-bit tokens. Public entry names are bounded at 128 bytes.

The driver allocates no heap memory. Static resources include one configuration-descriptor buffer, one 512-byte sector buffer, a 260-unit UTF-16 long-name workspace, one directory iterator, and one file state. Only one directory iterator and one file handle may therefore be active at a time. `t5_driver_get` is the intended sole ELF export.

## USB binding and lifecycle

Startup requires exactly one complete `usb.host@1` discovery API with configuration, claim/release, control, bulk input/output, discovery polling, and device enumeration callbacks.

The USB matcher accepts class `0x08`, subclass `0x06`, protocol `0x50`, with one bulk-IN and one bulk-OUT endpoint. After claiming the interface it queries GET_MAX_LUN using class request `0xa1/0xfe` with a 250 ms timeout; current operation remains LUN 0.

Attachment requires TEST UNIT READY, READ CAPACITY(10), 512-byte media blocks, and a FAT mount. `refresh` gives host discovery a 16-event budget, tracks the generation-qualified active device token, and clears mounted state when that token disappears. Quiescence is unavailable while either public handle is active. Successful quiescence releases the host claim and clears mounted geometry; stop then clears the host dependency.

## BOT and SCSI

CBWs are 31 bytes with signature `0x43425355`; CSWs are 13 bytes with signature `0x53425355`. BOT tags increase while skipping zero. CDBs are limited to 16 bytes. Bulk I/O uses a 1,000 ms timeout and exact-length loops. Successful CSWs require matching signature/tag, zero residue, and status zero.

Source-established SCSI commands are TEST UNIT READY (`0x00`, up to four attempts), READ CAPACITY(10) (`0x25`), READ(10) (`0x28`), and WRITE(10) (`0x2a`). Sector transfers are exactly 512 bytes.

## FAT mount and geometry

The provider first tests LBA 0 as a FAT boot sector; otherwise it treats LBA 0 as an MBR and checks up to four partition records. BPB validation requires the boot signature, 512-byte sectors, a power-of-two sectors-per-cluster value from 1 through 128, nonzero reserved/FAT/volume sizes, one or two FAT copies, and geometry within device capacity.

Cluster count selects FAT16 from 4,085 through 65,524 data clusters and FAT32 from 65,525 upward. FAT12 is not accepted. FAT32 additionally requires a valid root cluster.

Mounted state tracks partition start, total sectors, FAT location/length, root location, data start, cluster count, root cluster, sectors per cluster, FAT-copy count, FAT kind, and an allocation hint. FAT changes are mirrored to all FAT copies; newly allocated clusters are marked end-of-chain and zero-filled before use.

## Names, paths, and file semantics

Paths are absolute. Root is `/`; intermediate components must resolve to directories. Directory scans are bounded at 8,192 entries. The implementation parses short names and long-filename records, validates LFN checksums, handles UTF-16 surrogate pairs, and exposes UTF-8 names.

New names are decoded from UTF-8 and checked against FAT naming constraints. The fixed LFN workspace is 260 UTF-16 units and creation uses at most 20 LFN entries plus a generated short alias.

The label is the literal `USB Storage`. Reads walk FAT chains through the shared sector buffer. Output handles allocate/extend chains, use sector read-modify-output for partial sectors, and enforce the FAT 32-bit file-size limit. Commit records the resulting first cluster and size; non-commit close restores namespace/allocation consistency. The API also supports regular-file namespace cleanup when no public handle conflicts.

The diagnostic buffer is 96 bytes. Source-established errors cover host discovery, unsupported media/filesystem, full media, broken chains, USB sector I/O, destination state, unsupported names, namespace capacity, and size bounds.

The source contains no locks or atomics; execution assumes the serialized provider model.

## Validation and build

The exact upstream fixture at `81bdd8e689a39b8c7e0036f09ba6d52bf83cae21` simulates an 8,192-sector BOT disk containing FAT16. It exercises startup, mount/readiness, label/path metadata, directory iteration, existing-file input, long-name creation, a 600-byte multi-call output/readback, namespace cleanup, aborted-output rollback, hot-unplug claim release, quiescence, and unload. Expected terminal result: `USB MSC BOT + FAT16 browse/read/write/delete/hotplug: PASS`.

The fixture directly validates FAT16; FAT32 support is established by implementation paths rather than that fixture.

`scripts/build_usb_mass_storage.py` validates the exact manifest, ELF32 little-endian Xtensa ET_DYN output, sole export `t5_driver_get`, and exactly `memcpy` plus `memset` as unresolved imports, then records canonical size/hash parity.

## Established limits

One volume, LUN 0, BOT/SCSI-transparent protocol, 512-byte sectors, FAT16/FAT32, one file plus one directory iterator, 128-byte public names, 260 UTF-16 LFN units, 8,192 directory entries per scan, 20 LFN records per created name, 1,000 ms BOT transfer timeout, and no internal concurrency control.

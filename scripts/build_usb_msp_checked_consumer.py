#!/usr/bin/env python3
import hashlib
import json
import os
import shutil
import subprocess
from pathlib import Path

from normalize_xtensa_relocations import normalize
from native_driver_toolchain import compiler


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "Drivers/usb_msp"
OUT = ROOT / "dist/usb-msp-checked-consumer"

manifest = json.loads((SRC / "manifest.json").read_text())
required = {
    "type": "driver",
    "id": "usb-msp",
    "version": "0.1.2",
    "driver_abi": 2,
    "architecture": "xtensa-esp32s3",
    "file_name": "driver.elf",
    "requires": [
        {"capability": "usb.host", "api": 1},
        {"capability": "platform.clock", "api": 1},
    ],
    "provides": [{"capability": "debug.vendor.msp", "api": 1}],
    "status": "experimental-local-checked-consumer",
}
if manifest != required:
    raise SystemExit("usb-msp manifest mismatch")

cc = compiler()

OUT.mkdir(parents=True, exist_ok=True)
elf = OUT / "driver.elf"
subprocess.run(
    [
        cc,
        "-std=c11",
        "-Os",
        "-fPIC",
        "-mtext-section-literals",
        "-mlongcalls",
        "-fvisibility=hidden",
        "-nostdlib",
        "-nostartfiles",
        "-shared",
        "-I" + str(ROOT / "sdk/driver"),
        "-Wl,--hash-style=sysv",
        "-Wl,--exclude-libs,ALL",
        str(SRC / "driver.c"),
        "-lgcc",
        "-o",
        str(elf),
    ],
    check=True,
)
normalize(elf)

readelf = str(Path(cc).with_name(Path(cc).name.replace("gcc", "readelf")))
symbols = subprocess.check_output(
    [readelf, "--dyn-syms", "--wide", str(elf)], text=True
)
exports = {
    fields[7]
    for line in symbols.splitlines()
    if len((fields := line.split())) >= 8
    and fields[4] == "GLOBAL"
    and fields[6] != "UND"
    and fields[3] == "FUNC"
}
if exports != {"t5_driver_get"}:
    raise SystemExit(f"unexpected exports: {sorted(exports)}")

imports = {
    fields[7]
    for line in symbols.splitlines()
    if len((fields := line.split())) >= 8 and fields[6] == "UND"
}
if imports != {"memcpy", "memmove", "memset"}:
    raise SystemExit(f"unexpected imports: {sorted(imports)}")

data = elf.read_bytes()
if (
    not 52 <= len(data) <= 256 * 1024
    or data[:7] != b"\x7fELF\x01\x01\x01"
    or int.from_bytes(data[16:18], "little") != 3
    or int.from_bytes(data[18:20], "little") != 94
):
    raise SystemExit("invalid Xtensa shared driver")

digest = hashlib.sha256(data).hexdigest()
meta = dict(manifest)
meta.update(
    size_bytes=len(data),
    sha256=digest,
    canonical_byte_parity_claim=False,
)
(OUT / "manifest.json").write_text(json.dumps(meta, indent=2) + "\n")

print(
    f"built usb-msp v{manifest['version']} {len(data)} bytes "
    f"{digest} local_candidate=true"
)

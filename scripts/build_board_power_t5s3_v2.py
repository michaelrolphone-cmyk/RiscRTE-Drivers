#!/usr/bin/env python3
import hashlib
import json
import os
import shutil
import subprocess
from pathlib import Path

from normalize_xtensa_relocations import normalize

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "Drivers/bq25896"
OUT = ROOT / "dist/board-power-t5s3-v2"
CANONICAL_SIZE = 12660
CANONICAL_SHA256 = "5f50b5eb048085e3128939b6bed3693a9a44ebe27c9e2cae3b40dd725aa6a4c4"

manifest = json.loads((SRC / "manifest.json").read_text())
required = {
    "type": "driver",
    "id": "board-power-t5s3-v2",
    "version": "0.1.6",
    "driver_abi": 2,
    "architecture": "xtensa-esp32s3",
    "file_name": "driver.elf",
    "requires": [
        {"capability": "i2c.bus", "api": 1},
        {"capability": "platform.clock", "api": 1},
        {"capability": "board.power.bq25896.profile", "api": 1},
    ],
    "provides": [{"capability": "board.power.vbus", "api": 1}],
    "status": "experimental-unpublished",
}
if manifest != required:
    raise SystemExit("board-power-t5s3-v2 manifest mismatch")

cc = os.environ.get("NATIVE_DRIVER_CC") or shutil.which("xtensa-esp32s3-elf-gcc")
if not cc:
    cc = str(Path.home() / ".platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-gcc")
if not Path(cc).is_file():
    raise SystemExit(f"missing Xtensa compiler: {cc}")

OUT.mkdir(parents=True, exist_ok=True)
elf = OUT / "driver.elf"
subprocess.run([
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
], check=True)
normalize(elf)

readelf = str(Path(cc).with_name(Path(cc).name.replace("gcc", "readelf")))
symbols = subprocess.check_output([readelf, "--dyn-syms", "--wide", str(elf)], text=True)
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

data = elf.read_bytes()
if (
    not 52 <= len(data) <= 256 * 1024
    or data[:7] != b"\x7fELF\x01\x01\x01"
    or int.from_bytes(data[16:18], "little") != 3
    or int.from_bytes(data[18:20], "little") != 94
):
    raise SystemExit("invalid Xtensa shared driver")

digest = hashlib.sha256(data).hexdigest()
byte_parity = len(data) == CANONICAL_SIZE and digest == CANONICAL_SHA256
meta = dict(manifest)
meta.update(
    size_bytes=len(data),
    sha256=digest,
    canonical_size_bytes=CANONICAL_SIZE,
    canonical_sha256=CANONICAL_SHA256,
    byte_parity=byte_parity,
)
(OUT / "manifest.json").write_text(json.dumps(meta, indent=2) + "\n")
print(
    f"built board-power-t5s3-v2 v{manifest['version']} {len(data)} bytes "
    f"{digest} byte_parity={byte_parity}"
)
if not byte_parity:
    raise SystemExit("board-power-t5s3-v2 canonical release byte parity failed")

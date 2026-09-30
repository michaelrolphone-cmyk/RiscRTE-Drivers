#!/usr/bin/env python3
import hashlib
import json
import os
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "Drivers/t5s3_usb_power_profile"
OUT = ROOT / "dist/t5s3-usb-power-profile"
CANONICAL_SIZE = 2360
CANONICAL_SHA256 = "e1a61504f63be342a13afdeaccee637b2cba657ed192a20260bbf15041ae52ff"

manifest = json.loads((SRC / "manifest.json").read_text())
required = {
    "type": "driver",
    "id": "t5s3-usb-power-profile",
    "version": "0.1.1",
    "driver_abi": 2,
    "architecture": "xtensa-esp32s3",
    "file_name": "driver.elf",
    "requires": [],
    "provides": [{"capability": "board.power.bq25896.profile", "api": 1}],
    "status": "experimental-unpublished",
    "board": "t5s3-pro",
}
if manifest != required:
    raise SystemExit("t5s3-usb-power-profile manifest mismatch")

cc = os.environ.get("NATIVE_DRIVER_CC") or shutil.which("xtensa-esp32s3-elf-gcc")
if not cc:
    cc = str(Path.home() / ".platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-gcc")
if not Path(cc).is_file():
    raise SystemExit(f"missing Xtensa compiler: {cc}")

OUT.mkdir(parents=True, exist_ok=True)
elf = OUT / "driver.elf"
subprocess.run([
    cc, "-std=c11", "-Os", "-fPIC", "-mtext-section-literals", "-mlongcalls",
    "-fvisibility=hidden", "-nostdlib", "-nostartfiles", "-shared",
    "-I" + str(ROOT / "sdk/driver"), "-Wl,--hash-style=sysv",
    "-Wl,--exclude-libs,ALL", str(SRC / "driver.c"), "-lgcc", "-o", str(elf),
], check=True)

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
if len(data) < 52 or data[:7] != b"\x7fELF\x01\x01\x01" or int.from_bytes(data[18:20], "little") != 94:
    raise SystemExit("not an ELF32 Xtensa driver")
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
print(f"built {manifest['id']} v{manifest['version']} {len(data)} bytes {digest} byte_parity={byte_parity}")
if not byte_parity:
    raise SystemExit("t5s3-usb-power-profile canonical release byte parity failed")

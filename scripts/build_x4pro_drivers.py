#!/usr/bin/env python3
"""Build Xteink X4 Pro capability ELFs. These are board packages, not T5S3 parity drivers."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
BOARD = ROOT / "boards/xteink-x4-pro/drivers"
INCLUDE = ROOT / "boards/xteink-x4-pro/include"
SDK = ROOT / "sdk/driver"
OUT = ROOT / "dist/xteink-x4-pro"
sys.path.insert(0, str(ROOT / "scripts"))
from normalize_xtensa_relocations import normalize

DRIVERS = [
    "x4pro_gpio", "x4pro_i2c", "x4pro_panel", "x4pro_gt911",
    "x4pro_buttons", "x4pro_frontlight", "x4pro_battery", "x4pro_sd",
]


def compiler():
    env = os.environ.get("NATIVE_DRIVER_CC")
    if env and Path(env).is_file():
        return env
    found = shutil.which("xtensa-esp32s3-elf-gcc")
    if found:
        return found
    fallback = Path("/tmp/xtensa/xtensa-esp32s3-elf/bin/xtensa-esp32s3-elf-gcc")
    if fallback.is_file():
        return str(fallback)
    core = Path(os.environ.get("PLATFORMIO_CORE_DIR", Path.home() / ".platformio"))
    return str(core / "packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-gcc")


def build_one(cc, name):
    source = BOARD / name
    manifest = json.loads((source / "manifest.json").read_text())
    if manifest.get("architecture") != "xtensa-esp32s3" or manifest.get("driver_abi") != 2:
        raise ValueError(name + " manifest is not an ABI-v2 Xtensa driver")
    dest = OUT / manifest["id"]
    dest.mkdir(parents=True, exist_ok=True)
    elf = dest / "driver.elf"
    subprocess.run([
        cc, "-std=c11", "-Os", "-Wall", "-Wextra", "-Wno-unused-function",
        "-fPIC", "-mtext-section-literals", "-mlongcalls", "-fvisibility=hidden",
        "-nostdlib", "-nostartfiles", "-shared",
        "-I" + str(SDK), "-I" + str(INCLUDE),
        "-Wl,--hash-style=sysv", "-Wl,--exclude-libs,ALL",
        str(source / "driver.c"), "-lgcc", "-o", str(elf),
    ], check=True)
    try:
        normalize(elf)
    except ValueError as exc:
        if "Missing required section" not in str(exc):
            raise
    readelf = str(Path(cc).with_name(Path(cc).name.replace("gcc", "readelf")))
    symbols = subprocess.check_output([readelf, "--dyn-syms", "--wide", str(elf)], text=True)
    exported = {
        fields[7] for line in symbols.splitlines()
        if len(fields := line.split()) >= 8 and fields[4] == "GLOBAL"
        and fields[6] != "UND" and fields[3] == "FUNC"
    }
    if exported != {"t5_driver_get"}:
        raise ValueError(name + " exports " + repr(exported))
    payload = elf.read_bytes()
    if payload[:7] != b"\x7fELF\x01\x01\x01" or int.from_bytes(payload[18:20], "little") != 94:
        raise ValueError(name + " is not an Xtensa ELF")
    digest = hashlib.sha256(payload).hexdigest()
    manifest.update(size_bytes=len(payload), sha256=digest, byte_parity=False)
    (dest / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"{manifest['id']} {len(payload)} bytes {digest}")
    return manifest


def main():
    cc = compiler()
    if not Path(cc).is_file():
        raise SystemExit("missing Xtensa compiler: " + cc)
    built = [build_one(cc, name) for name in DRIVERS]
    (OUT / "load-order.json").write_text((ROOT / "boards/xteink-x4-pro/load-order.json").read_text())
    print(f"built {len(built)} X4 Pro drivers in {OUT}")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Build the released EPD display.output provider and verify canonical bytes."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

from normalize_xtensa_relocations import normalize

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "Drivers/display_epd_video"
OUTPUT = ROOT / "dist/display-epd-video"
CANONICAL_SIZE = 5852
CANONICAL_SHA256 = "7691d63e729d69aad3486a58bf913ddafff5af3cb156a3bcaed59270dcb71f7a"


def build(cc=None):
    manifest = json.loads((SOURCE / "manifest.json").read_text())
    required = {
        "type": "driver", "id": "display-epd-video", "version": "0.1.3",
        "driver_abi": 2, "architecture": "xtensa-esp32s3",
        "file_name": "driver.elf", "requires": [],
        "provides": [{"capability": "display.output", "api": 1}],
        "status": "experimental-unpublished",
    }
    if any(manifest.get(key) != value for key, value in required.items()):
        raise ValueError("Invalid EPD display provider manifest")

    cc = cc or os.environ.get("NATIVE_DRIVER_CC") or shutil.which("xtensa-esp32s3-elf-gcc")
    if not cc:
        core = Path(os.environ.get("PLATFORMIO_CORE_DIR", Path.home() / ".platformio"))
        cc = str(core / "packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-gcc")
    if not Path(cc).is_file():
        raise SystemExit(f"missing Xtensa compiler: {cc}")

    OUTPUT.mkdir(parents=True, exist_ok=True)
    elf = OUTPUT / "driver.elf"
    subprocess.run([
        cc, "-std=c11", "-Os", "-fPIC", "-mtext-section-literals", "-mlongcalls",
        "-fvisibility=hidden", "-nostdlib", "-nostartfiles", "-shared",
        "-I" + str(ROOT / "sdk/driver"),
        "-Wl,--hash-style=sysv", "-Wl,--exclude-libs,ALL",
        str(SOURCE / "driver.c"), "-lgcc", "-o", str(elf),
    ], check=True)
    normalize(elf)

    readelf = str(Path(cc).with_name(Path(cc).name.replace("gcc", "readelf")))
    symbols = subprocess.check_output([readelf, "--dyn-syms", "--wide", str(elf)], text=True)
    exported = {
        fields[7] for line in symbols.splitlines()
        if len(fields := line.split()) >= 8 and fields[4] == "GLOBAL"
        and fields[6] != "UND" and fields[3] == "FUNC"
    }
    if exported != {"t5_driver_get"}:
        raise ValueError("Display provider must export only t5_driver_get: " + repr(exported))
    imported = {
        fields[7] for line in symbols.splitlines()
        if len(fields := line.split()) >= 8 and fields[4] == "GLOBAL" and fields[6] == "UND"
    }
    allowed = {"memcpy", "memset", "usleep", "t5_video_get_api"}
    if not imported <= allowed or not {"usleep", "t5_video_get_api"} <= imported:
        raise ValueError("Display provider has unexpected firmware imports: " + repr(sorted(imported)))

    payload = elf.read_bytes()
    if (not 52 <= len(payload) <= 256 * 1024 or payload[:7] != b"\x7fELF\x01\x01\x01"
            or int.from_bytes(payload[16:18], "little") != 3
            or int.from_bytes(payload[18:20], "little") != 94):
        raise ValueError("Invalid Xtensa display provider ELF")
    digest = hashlib.sha256(payload).hexdigest()
    if len(payload) != CANONICAL_SIZE or digest != CANONICAL_SHA256:
        raise ValueError(f"canonical byte parity mismatch: got {len(payload)} {digest}")
    manifest.update(size_bytes=len(payload), sha256=digest, byte_parity=True)
    (OUTPUT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"display-epd-video {len(payload)} bytes {digest} byte_parity=true")
    return elf


if __name__ == "__main__":
    build()

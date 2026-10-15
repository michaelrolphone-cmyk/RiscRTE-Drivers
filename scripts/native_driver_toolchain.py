"""Compiler discovery for hardware-independent ESP32-S3 ABI drivers.

Historical release replay builders deliberately do not use this module.
"""
import argparse
from functools import lru_cache
import os
from pathlib import Path
import shutil
import subprocess

NATIVE_CC = "xtensa-esp-elf-gcc"
LEGACY_CC = "xtensa-esp32s3-elf-gcc"


def compiler(override=None, *, legacy_fallbacks=()):
    """Prefer explicit choice, native PATH, S3 PATH, then legacy installations.

    An invalid explicit override fails closed; it must never silently select a
    different compiler. Both executable names on PATH and paths are accepted.
    """
    requested = override if override is not None else os.environ.get("NATIVE_DRIVER_CC")
    if requested:
        resolved = shutil.which(str(Path(requested).expanduser()))
        if resolved:
            return _validate(_frontend(str(Path(resolved).absolute())))
        raise SystemExit(f"missing or non-executable NATIVE_DRIVER_CC: {requested}")
    for name in (NATIVE_CC, LEGACY_CC):
        found = shutil.which(name)
        if found:
            if name == NATIVE_CC:
                # Unified GCC is generic/big-endian without its target frontend.
                found = _frontend(found)
            return _validate(str(Path(found).absolute()))
    core = Path(os.environ.get("PLATFORMIO_CORE_DIR", Path.home() / ".platformio"))
    for path in (*legacy_fallbacks, core / "packages/toolchain-xtensa-esp32s3/bin" / LEGACY_CC):
        found = shutil.which(str(path))
        if found:
            return _validate(str(Path(found).absolute()))
    raise SystemExit(
        "missing Xtensa compiler; activate ESP-IDF 6.1 (xtensa-esp-elf-gcc), "
        "set NATIVE_DRIVER_CC, or install the legacy ESP32-S3 toolchain"
    )


def _frontend(cc):
    if Path(cc).name == NATIVE_CC:
        frontend = Path(cc).with_name(LEGACY_CC)
        if not shutil.which(str(frontend)):
            raise SystemExit(f"native toolchain is missing its ESP32-S3 frontend: {frontend}")
        return str(frontend)
    return cc


@lru_cache(maxsize=None)
def _validate(cc):
    target = subprocess.check_output([cc, "-dumpmachine"], text=True).strip()
    if target not in ("xtensa-esp-elf", "xtensa-esp32s3-elf"):
        raise SystemExit(f"unsupported driver compiler target {target!r}: {cc}")
    macros = subprocess.check_output([cc, "-dM", "-E", "-x", "c", "-"],
                                     input="", text=True)
    if "#define __XTENSA_EL__ 1" not in macros or "#define __XTENSA_WINDOWED_ABI__ 1" not in macros:
        raise SystemExit(
            f"compiler does not select little-endian Xtensa windowed ABI: {cc}; "
            f"use the native ESP32-S3 frontend {Path(cc).with_name(LEGACY_CC)}"
        )
    return cc


def parity_requested():
    parser = argparse.ArgumentParser(description="Build an ESP32-S3 ABI-v2 driver")
    parser.add_argument("--require-byte-parity", action="store_true",
                        help="Fail unless the output matches the historical GCC8 release")
    return parser.parse_args().require_byte_parity


def native_support_sources(cc):
    """PIC uint64 division for the GCC15 archive's non-PIC __udivdi3.

    Only callers which need uint64 division request this source. Keeping the
    symbol hidden and local preserves the firmware ABI; legacy builds retain
    their original libgcc input and exact-byte parity.
    """
    if is_unified(cc):
        return (str(Path(__file__).parent / "xtensa_support/unsigned_division.c"),)
    return ()


@lru_cache(maxsize=None)
def is_unified(cc):
    return subprocess.check_output([cc, "-dumpmachine"], text=True).strip() == "xtensa-esp-elf"

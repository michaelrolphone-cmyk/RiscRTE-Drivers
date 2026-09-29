#!/usr/bin/env python3
"""Reproduce the published gt911-touch v0.1.1 ELF in its release environment.

The migrated two-file driver source must be byte-identical to the source at the
upstream release commit. Canonical byte-parity validation replays the upstream
GT911 build at the original GitHub Actions workspace path and then audits the
produced ELF without modifying T5S3-Reader.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
DRIVER = ROOT / "Drivers/gt911_touch"
OUT = ROOT / "dist/gt911-touch"
HISTORICAL_COMMIT = "36be2ee496468feb3779616e69f61e3a671c192b"
UPSTREAM = "https://github.com/michaelrolphone-cmyk/T5S3-Reader.git"
CANONICAL_ROOT = Path("/home/runner/work/T5S3-Reader/T5S3-Reader")
CANONICAL_SIZE = 42976
CANONICAL_SHA256 = "44d753b736a2a433549ab500a3cae52f1e2844f79332fd119fc8df8d57cd11f4"
HISTORICAL_ELF = Path("dist/experimental/gt911-touch/driver.elf")
EXPECTED_MANIFEST = {
    "type": "driver",
    "id": "gt911-touch",
    "version": "0.1.1",
    "driver_abi": 2,
    "architecture": "xtensa-esp32s3",
    "file_name": "driver.elf",
    "requires": [
        {"capability": "i2c.bus", "api": 1},
        {"capability": "platform.clock", "api": 1},
    ],
    "provides": [{"capability": "input.touch.raw", "api": 1}],
    "status": "experimental-unpublished",
}


def _files(root: Path) -> list[str]:
    return sorted(
        p.relative_to(root).as_posix()
        for p in root.rglob("*")
        if p.is_file()
    )


def _prepare_workspace() -> tuple[Path, bool]:
    canonical = os.environ.get("GITHUB_ACTIONS") == "true"
    workspace = CANONICAL_ROOT if canonical else OUT / "historical-release-workspace"
    if workspace.exists():
        raise RuntimeError(
            "historical release workspace already exists; refusing to replace it: "
            + str(workspace)
        )
    workspace.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(["git", "init", str(workspace)], check=True)
    subprocess.run(
        ["git", "-C", str(workspace), "remote", "add", "origin", UPSTREAM],
        check=True,
    )
    subprocess.run(
        ["git", "-C", str(workspace), "fetch", "--depth=1", "origin", HISTORICAL_COMMIT],
        check=True,
    )
    subprocess.run(
        ["git", "-C", str(workspace), "checkout", "--detach", "FETCH_HEAD"],
        check=True,
    )
    head = subprocess.check_output(
        ["git", "-C", str(workspace), "rev-parse", "HEAD"], text=True
    ).strip()
    if head != HISTORICAL_COMMIT:
        raise RuntimeError(f"historical checkout mismatch: {head}")
    return workspace, canonical


def _overlay_migrated_source(workspace: Path) -> list[str]:
    historical = workspace / "Drivers/gt911_touch"
    migrated_files = _files(DRIVER)
    historical_files = _files(historical)
    if migrated_files != historical_files:
        missing = sorted(set(historical_files) - set(migrated_files))
        extra = sorted(set(migrated_files) - set(historical_files))
        raise RuntimeError(
            "GT911 source file-set differs from canonical release "
            f"(missing={missing}, extra={extra})"
        )
    for rel in migrated_files:
        if (DRIVER / rel).read_bytes() != (historical / rel).read_bytes():
            raise RuntimeError(f"GT911 source differs from canonical release: {rel}")
    for rel in migrated_files:
        shutil.copyfile(DRIVER / rel, historical / rel)
    return migrated_files


def _historical_tools(workspace: Path) -> tuple[Path, Path]:
    entries = json.loads((workspace / "compile_commands.json").read_text())
    matches = [
        entry
        for entry in entries
        if Path(entry["file"]).as_posix().endswith("src/native/NativeUsbBridge.cpp")
    ]
    if len(matches) != 1:
        raise RuntimeError("historical compile database lacks unique NativeUsbBridge entry")
    entry = matches[0]
    argv = list(entry["arguments"]) if "arguments" in entry else __import__("shlex").split(entry["command"])
    compiler = Path(argv[0])
    if not compiler.name.endswith("g++"):
        raise RuntimeError("unexpected historical Xtensa compiler: " + str(compiler))
    prefix = compiler.name[:-3]
    return compiler.with_name(prefix + "nm"), compiler.with_name(prefix + "readelf")


def _audit(elf: Path, nm: Path, readelf: Path) -> tuple[list[str], int, str, bool]:
    raw = subprocess.check_output([str(nm), "-u", str(elf)], text=True)
    (OUT / "unresolved-symbols.txt").write_text(raw)
    imports = sorted(line.split()[-1] for line in raw.splitlines() if line.split())
    symbols = subprocess.check_output(
        [str(readelf), "--dyn-syms", "--wide", str(elf)], text=True
    )
    exports = {
        fields[7]
        for line in symbols.splitlines()
        if len((fields := line.split())) >= 8
        and fields[3] == "FUNC"
        and fields[4] == "GLOBAL"
        and fields[6] != "UND"
    }
    if exports != {"t5_driver_get"}:
        raise RuntimeError("unexpected GT911 exports: " + repr(sorted(exports)))
    data = elf.read_bytes()
    if (
        len(data) < 52
        or data[:7] != b"\x7fELF\x01\x01\x01"
        or int.from_bytes(data[16:18], "little") != 3
        or int.from_bytes(data[18:20], "little") != 94
    ):
        raise RuntimeError("not ELF32 little-endian Xtensa ET_DYN")
    digest = hashlib.sha256(data).hexdigest()
    parity = len(data) == CANONICAL_SIZE and digest == CANONICAL_SHA256
    return imports, len(data), digest, parity


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--require-byte-parity", action="store_true")
    args = parser.parse_args()

    manifest = json.loads((DRIVER / "manifest.json").read_text())
    if manifest != EXPECTED_MANIFEST:
        raise SystemExit("gt911-touch manifest mismatch")

    OUT.mkdir(parents=True, exist_ok=True)
    workspace, canonical_path = _prepare_workspace()
    try:
        migrated_files = _overlay_migrated_source(workspace)
        subprocess.run(
            [sys.executable, "scripts/build_gt911_touch.py"], cwd=workspace, check=True
        )
        historical_elf = workspace / HISTORICAL_ELF
        if not historical_elf.is_file():
            raise RuntimeError("historical GT911 build produced no linked ELF")
        elf = OUT / "driver.elf"
        shutil.copyfile(historical_elf, elf)
        nm, readelf = _historical_tools(workspace)
        imports, size, digest, parity = _audit(elf, nm, readelf)
        parity = parity and canonical_path

        provenance = {
            "historical_repository": "michaelrolphone-cmyk/T5S3-Reader",
            "historical_commit": HISTORICAL_COMMIT,
            "historical_release_tag": "driver-gt911-touch-v0.1.1",
            "historical_workspace": str(workspace),
            "canonical_workspace_path": canonical_path,
            "migrated_source_files": migrated_files,
            "migrated_source_byte_identical": True,
        }
        (OUT / "canonical-build-provenance.json").write_text(
            json.dumps(provenance, indent=2) + "\n"
        )
        meta = dict(manifest)
        meta.update(
            size_bytes=size,
            sha256=digest,
            canonical_size_bytes=CANONICAL_SIZE,
            canonical_sha256=CANONICAL_SHA256,
            byte_parity=parity,
            unresolved_imports=imports,
            historical_release_commit=HISTORICAL_COMMIT,
        )
        (OUT / "manifest.json").write_text(json.dumps(meta, indent=2) + "\n")
        print(
            f"built gt911-touch v{manifest['version']} "
            f"{size} bytes {digest} byte_parity={parity}"
        )
        if args.require_byte_parity and not parity:
            raise SystemExit("gt911-touch canonical release byte parity failed")
    finally:
        if workspace.exists():
            shutil.rmtree(workspace)


if __name__ == "__main__":
    main()

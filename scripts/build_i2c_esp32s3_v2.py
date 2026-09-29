#!/usr/bin/env python3
"""Reproduce the published i2c-esp32s3-v2 v0.1.6 ELF in its release environment.

The migrated four-file driver source is required to be byte-identical to the
source tagged by T5S3-Reader as driver-i2c-esp32s3-v2-v0.1.6.  The published
ELF was built by the upstream release workflow through
scripts/probe_i2c_esp32s3_v2.py, which derives compile/link arguments from the
full `t5s3-pro` PlatformIO compilation database.

For canonical byte-parity validation, rebuild those migrated bytes inside a
read-only checkout of the exact historical release commit at the original
GitHub Actions workspace path.  This reproduces the release build inputs
without making T5S3-Reader authoritative for the migrated driver source.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
DRIVER = ROOT / "Drivers/i2c_esp32s3_v2"
OUT = ROOT / "dist/i2c-esp32s3-v2"
HISTORICAL_COMMIT = "36be2ee496468feb3779616e69f61e3a671c192b"
UPSTREAM = "https://github.com/michaelrolphone-cmyk/T5S3-Reader.git"
CANONICAL_ROOT = Path("/home/runner/work/T5S3-Reader/T5S3-Reader")
CANONICAL_SIZE = 24960
CANONICAL_SHA256 = "0230f71ca21340165c59cba89e18e30ba169671c714dd1098ed5b4994f90fc34"
HISTORICAL_ELF = Path("dist/experimental/i2c-esp32s3-v2/driver.elf")
EXPECTED_IMPORTS = {
    "risc_fw_i2c_transact_v1",
    "vQueueDelete",
    "xQueueCreateMutex",
    "xQueueGenericSend",
    "xQueueSemaphoreTake",
    "xTaskGetTickCount",
}
EXPECTED_MANIFEST = {
    "type": "driver",
    "id": "i2c-esp32s3-v2",
    "version": "0.1.6",
    "driver_abi": 2,
    "architecture": "xtensa-esp32s3",
    "file_name": "driver.elf",
    "requires": [],
    "provides": [{"capability": "i2c.bus", "api": 1}],
    "status": "experimental-unpublished",
    "board": "t5s3-pro",
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
        ["git", "-C", str(workspace), "rev-parse", "HEAD"],
        text=True,
    ).strip()
    if head != HISTORICAL_COMMIT:
        raise RuntimeError(f"historical checkout mismatch: {head}")
    return workspace, canonical


def _overlay_migrated_source(workspace: Path) -> list[str]:
    historical = workspace / "Drivers/i2c_esp32s3_v2"
    migrated_files = _files(DRIVER)
    historical_files = _files(historical)
    if migrated_files != historical_files:
        missing = sorted(set(historical_files) - set(migrated_files))
        extra = sorted(set(migrated_files) - set(historical_files))
        raise RuntimeError(
            "i2c driver source file-set differs from canonical release "
            f"(missing={missing}, extra={extra})"
        )

    for rel in migrated_files:
        migrated = DRIVER / rel
        canonical = historical / rel
        if migrated.read_bytes() != canonical.read_bytes():
            raise RuntimeError(f"i2c driver source differs from canonical release: {rel}")

    for rel in migrated_files:
        shutil.copyfile(DRIVER / rel, historical / rel)
    return migrated_files


def _historical_tools(workspace: Path) -> tuple[Path, Path, Path]:
    entries = json.loads((workspace / "compile_commands.json").read_text())
    matches = [
        entry
        for entry in entries
        if Path(entry["file"]).as_posix().endswith("src/native/NativeUsbBridge.cpp")
    ]
    if len(matches) != 1:
        raise RuntimeError("historical compile database lacks unique NativeUsbBridge entry")
    entry = matches[0]
    argv = list(entry["arguments"]) if "arguments" in entry else shlex.split(entry["command"])
    if not argv:
        raise RuntimeError("historical compiler command is empty")
    compiler = Path(argv[0])
    name = compiler.name
    if not name.endswith("g++"):
        raise RuntimeError("unexpected historical Xtensa compiler: " + str(compiler))
    prefix = name[:-3]
    return (
        compiler,
        compiler.with_name(prefix + "nm"),
        compiler.with_name(prefix + "readelf"),
    )


def _audit(elf: Path, nm: Path, readelf: Path) -> tuple[list[str], int, str, bool]:
    raw = subprocess.check_output([str(nm), "-u", str(elf)], text=True)
    (OUT / "unresolved-symbols.txt").write_text(raw)
    imports = sorted(line.split()[-1] for line in raw.splitlines() if line.split())

    if set(imports) != EXPECTED_IMPORTS:
        missing = sorted(EXPECTED_IMPORTS - set(imports))
        extra = sorted(set(imports) - EXPECTED_IMPORTS)
        raise RuntimeError(f"unexpected runtime imports: missing={missing} extra={extra}")

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
        raise RuntimeError("unexpected exports: " + repr(sorted(exports)))

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
        raise SystemExit("i2c-esp32s3-v2 manifest mismatch")

    OUT.mkdir(parents=True, exist_ok=True)
    workspace, canonical_path = _prepare_workspace()
    migrated_files = _overlay_migrated_source(workspace)

    subprocess.run(
        [sys.executable, "scripts/probe_i2c_esp32s3_v2.py"],
        cwd=workspace,
        check=True,
    )

    historical_elf = workspace / HISTORICAL_ELF
    if not historical_elf.is_file():
        raise RuntimeError("historical I2C probe produced no linked ELF")

    elf = OUT / "driver.elf"
    shutil.copyfile(historical_elf, elf)

    _, nm, readelf = _historical_tools(workspace)
    imports, size, digest, parity = _audit(elf, nm, readelf)
    parity = parity and canonical_path

    provenance = {
        "historical_repository": "michaelrolphone-cmyk/T5S3-Reader",
        "historical_commit": HISTORICAL_COMMIT,
        "historical_release_tag": "driver-i2c-esp32s3-v2-v0.1.6",
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
        f"built i2c-esp32s3-v2 v{manifest['version']} "
        f"{size} bytes {digest} byte_parity={parity}"
    )
    if workspace.exists():
        shutil.rmtree(workspace)
    if args.require_byte_parity and not parity:
        raise SystemExit("i2c-esp32s3-v2 canonical release byte parity failed")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Reproduce the published usb-controller-esp32s3 v0.1.18 ELF exactly.

The migrated controller source is byte-identical to the source published from
T5S3-Reader commit 935ac7f81191994e78a00bb94f443389764c1a35. The release
ELF also contains compiler/debug metadata derived from that repository's full
`t5s3-pro` PlatformIO environment and canonical GitHub Actions workspace path.

For the canonical parity gate, rebuild the migrated source bytes inside a
read-only clone of that exact release commit at the historical workspace path.
This keeps RiscRTE-Drivers authoritative for the driver bytes while reproducing
the original build configuration instead of approximating it with a reduced
synthetic PlatformIO probe.
"""

import argparse
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys

import build_usb_controller_esp32s3 as builder

ROOT = Path(__file__).resolve().parents[1]
DRIVER = ROOT / "Drivers/usb_controller_esp32s3"
OUT = ROOT / "dist/usb-controller-esp32s3"
HISTORICAL_COMMIT = "935ac7f81191994e78a00bb94f443389764c1a35"
UPSTREAM = "https://github.com/michaelrolphone-cmyk/T5S3-Reader.git"
CANONICAL_ROOT = Path("/home/runner/work/T5S3-Reader/T5S3-Reader")
CANONICAL_SIZE = 783576
CANONICAL_SHA256 = "f67064a9678a7b048e40cbf411d46653b69006aec07b9f2c95428597cc706e0e"
HISTORICAL_ELF = Path("dist/experimental/usb-controller-esp32s3/controller-link-experiment.elf")
HISTORICAL_IDF_SOURCE = Path("dist/experimental/usb-controller-esp32s3/idf-usb-source.txt")


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
    historical = workspace / "Drivers/usb_controller_esp32s3"
    migrated_files = _files(DRIVER)
    historical_files = _files(historical)
    if migrated_files != historical_files:
        missing = sorted(set(historical_files) - set(migrated_files))
        extra = sorted(set(migrated_files) - set(historical_files))
        raise RuntimeError(
            "controller source file-set differs from canonical release "
            f"(missing={missing}, extra={extra})"
        )

    for rel in migrated_files:
        migrated = DRIVER / rel
        canonical = historical / rel
        if migrated.read_bytes() != canonical.read_bytes():
            raise RuntimeError(f"controller source differs from canonical release: {rel}")

    # Make the historical build consume bytes from RiscRTE-Drivers after proving
    # that those bytes exactly match the source used for the published release.
    for rel in migrated_files:
        shutil.copyfile(DRIVER / rel, historical / rel)
    return migrated_files


def _historical_compiler(workspace: Path) -> Path:
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
    return Path(argv[0])


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--require-byte-parity", action="store_true", default=True,\n                        help="Require the canonical published ELF bytes (always enabled in migration CI)")
    args = parser.parse_args()

    manifest = json.loads((DRIVER / "manifest.json").read_text())
    if manifest != builder.EXPECTED:
        raise SystemExit("usb-controller-esp32s3 manifest mismatch")

    OUT.mkdir(parents=True, exist_ok=True)
    workspace, canonical_path = _prepare_workspace()
    migrated_files = _overlay_migrated_source(workspace)

    subprocess.run(
        [sys.executable, "scripts/probe_usb_controller_esp32s3.py", "--link-experiment"],
        cwd=workspace,
        check=True,
    )

    historical_elf = workspace / HISTORICAL_ELF
    if not historical_elf.is_file():
        raise RuntimeError("historical controller probe produced no linked ELF")

    elf = OUT / "driver.elf"
    shutil.copyfile(historical_elf, elf)

    idf_source = workspace / HISTORICAL_IDF_SOURCE
    if idf_source.is_file():
        shutil.copyfile(idf_source, OUT / "idf-usb-source.txt")

    compiler = _historical_compiler(workspace)
    imports, size, digest, parity = builder.audit(elf, compiler)
    parity = parity and canonical_path

    provenance = {
        "historical_repository": "michaelrolphone-cmyk/T5S3-Reader",
        "historical_commit": HISTORICAL_COMMIT,
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
        f"built usb-controller-esp32s3 v{manifest['version']} "
        f"{size} bytes {digest} byte_parity={parity}"
    )
    if args.require_byte_parity and not parity:
        raise SystemExit("usb-controller-esp32s3 canonical release byte parity failed")


if __name__ == "__main__":
    main()

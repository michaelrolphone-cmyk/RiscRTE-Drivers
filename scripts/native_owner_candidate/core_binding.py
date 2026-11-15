"""Checked-in exact committed Core closure. Metadata is not runtime authority."""
import hashlib
import json
import os
from pathlib import Path
import re
import stat

CORE_REPOSITORY = "https://github.com/michaelrolphone-cmyk/RiscRTE.git"
CORE_COMMIT = "ec86b26b2f06271c62405dc8976b07dae19b713f"
BINDING_SHA256 = "a5ae41ea70bb982b6b579645bb3faafd15e035347404e445aa0a910a8c7cd94d"
MEMBERS = frozenset(["lib/elf_loader/src/esp_elf_validate.c","lib/elf_loader/src/esp_privileged_imports.c","lib/elf_loader/src/esp_privileged_manifest_imports.c","lib/elf_loader/include/private/elf_types.h","lib/elf_loader/include/private/esp_elf_data_layout.h","lib/elf_loader/include/private/esp_elf_internal_v1.h","lib/elf_loader/include/private/esp_privileged_imports.h","lib/elf_loader/include/private/esp_privileged_manifest_imports.h","lib/elf_loader/include/private/privileged_os_cpu_symbols_v1.def","lib/elf_loader/include/private/privileged_os_cpu_symbols_v2.def","lib/elf_loader/include/private/privileged_os_cpu_symbols_v5.def","lib/elf_loader/include/private/privileged_public_libc_compat_v1.def","lib/elf_loader/include/private/privileged_public_libc_compat_v2.def"])


def _snapshot(path, limit=8 * 1024 * 1024):
    fd = os.open(str(path), os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0))
    try:
        before = os.fstat(fd)
        if not stat.S_ISREG(before.st_mode) or before.st_size > limit:
            raise ValueError("bounded regular Core input required")
        chunks = []; remaining = limit + 1
        while remaining:
            b = os.read(fd, min(65536, remaining))
            if not b: break
            chunks.append(b); remaining -= len(b)
        raw = b"".join(chunks); after = os.fstat(fd)
        keys = ("st_dev", "st_ino", "st_size", "st_mtime_ns", "st_ctime_ns")
        if len(raw) != before.st_size or any(getattr(before,k) != getattr(after,k) for k in keys):
            raise ValueError("Core input changed while reading")
        return raw
    finally:
        os.close(fd)


def parse_binding(raw):
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result: raise ValueError("duplicate Core binding key")
            result[key] = value
        return result
    value = json.loads(raw.decode("utf-8"), object_pairs_hook=pairs)
    if not isinstance(value, dict) or set(value) != {"schema","repository","commit","os_cpu_abi","files"}:
        raise ValueError("exact Core binding fields required")
    if type(value["schema"]) is not int or value["schema"] != 1:
        raise ValueError("Core binding schema must be integer1")
    if value["repository"] != CORE_REPOSITORY or value["commit"] != CORE_COMMIT:
        raise ValueError("unreviewed Core repository or commit")
    if type(value["os_cpu_abi"]) is not int or value["os_cpu_abi"] != 5:
        raise ValueError("Core binding ABI must be integer5")
    members = value["files"]
    if not isinstance(members, dict) or set(members) != MEMBERS:
        raise ValueError("exact13 Core members required")
    if any(not isinstance(h, str) or not re.fullmatch("[0-9a-f]{64}",h) for h in members.values()):
        raise ValueError("canonical Core member SHA256 required")
    return value


def load_binding():
    path = Path(__file__).resolve().with_name("core-source.json")
    raw = _snapshot(path, 16384)
    if hashlib.sha256(raw).hexdigest() != BINDING_SHA256:
        raise ValueError("checked-in Core binding pin mismatch")
    return parse_binding(raw)


def validate_core_source(source):
    binding = load_binding()
    source = Path(source)
    for name, expected in binding["files"].items():
        path = source
        for part in Path(name).parts:
            path = path / part
            if path.is_symlink(): raise ValueError("Core source symlink refused")
        if hashlib.sha256(_snapshot(path)).hexdigest() != expected:
            raise ValueError("Core source pin mismatch: " + name)
    return binding

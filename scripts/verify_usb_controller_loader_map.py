#!/usr/bin/env python3
"""Check usb-controller provider ELF relocation sites and pointer values against the runtime map.

Derived from the current T5S3-Reader provider relocation-map validator. The
runtime maps .text, .data, .rodata, .data.rel.ro and .bss. R_XTENSA_RELATIVE
values outside those sections are invalid except for absolute ESP32-S3 MMIO
values backed by matching SHN_ABS symbols.
"""
import argparse
from pathlib import Path
import struct

MAPPED = frozenset((".text", ".data", ".rodata", ".data.rel.ro", ".bss"))
SHF_ALLOC = 2
SHF_EXECINSTR = 4
SHT_SYMTAB = 2
SHT_RELA = 4
SHT_NOBITS = 8
SHN_ABS = 0xFFF1
R_XTENSA_RELATIVE = 5
S3_MMIO_LOW = 0x60000000
S3_MMIO_HIGH = 0x600FE000


def audit_loader_map(path):
    data = Path(path).read_bytes()
    if len(data) < 52 or data[:7] != b"\x7fELF\x01\x01\x01":
        raise ValueError("not a supported little-endian ELF32")
    _, _, _, _, _, shoff, _, ehsize, _, _, shentsize, shnum, name_index = (
        struct.unpack_from("<HHIIIIIHHHHHH", data, 16)
    )
    if (
        ehsize != 52
        or shentsize != 40
        or not 0 < shnum <= 256
        or name_index >= shnum
        or shoff > len(data)
        or shnum * shentsize > len(data) - shoff
    ):
        raise ValueError("invalid ELF section header table")

    sections = [
        struct.unpack_from("<IIIIIIIIII", data, shoff + i * 40)
        for i in range(shnum)
    ]
    string_table = sections[name_index]
    if (
        string_table[1] != 3
        or string_table[4] > len(data)
        or string_table[5] > len(data) - string_table[4]
    ):
        raise ValueError("invalid ELF section names")
    names = data[string_table[4] : string_table[4] + string_table[5]]

    def section_name(section):
        offset = section[0]
        if offset >= len(names) or b"\0" not in names[offset:]:
            raise ValueError("unterminated ELF section name")
        return names[offset:].split(b"\0", 1)[0].decode("ascii")

    mapped = []
    orphan_exec = []
    absolute_symbols = set()
    for section in sections:
        name = section_name(section)
        _, kind, flags, virtual, file_offset, size, _, _, _, entries = section
        if kind != SHT_NOBITS and (
            file_offset > len(data) or size > len(data) - file_offset
        ):
            raise ValueError("ELF section extends past file")
        if flags & SHF_ALLOC and name in MAPPED:
            if virtual + size > (1 << 32):
                raise ValueError("mapped section address overflow")
            mapped.append((virtual, virtual + size, file_offset, kind, name))
        elif flags & SHF_ALLOC and flags & SHF_EXECINSTR and size:
            orphan_exec.append(name)
        if kind == SHT_SYMTAB:
            if entries != 16 or size % entries:
                raise ValueError("invalid ELF symbol table")
            for j in range(size // entries):
                _, value, _, _, _, shndx = struct.unpack_from(
                    "<IIIBBH", data, file_offset + j * entries
                )
                if shndx == SHN_ABS:
                    absolute_symbols.add(value)

    if not any(section_name(section) == ".text" and section[5] for section in sections):
        raise ValueError("missing runtime-mapped .text")

    unmapped = []
    bad_values = []
    preserved_absolute = []
    relocations = 0
    for section in sections:
        if section[1] != SHT_RELA:
            continue
        if section[5] % 12:
            raise ValueError("malformed RELA size")
        for j in range(section[5] // 12):
            offset, info, _ = struct.unpack_from(
                "<IIi", data, section[4] + j * 12
            )
            relocations += 1
            source = next(
                (
                    item
                    for item in mapped
                    if item[0] <= offset and offset + 4 <= item[1]
                ),
                None,
            )
            if source is None:
                unmapped.append(
                    {
                        "section": section_name(section),
                        "offset": hex(offset),
                        "type": info & 255,
                    }
                )
                continue
            if (info & 255) != R_XTENSA_RELATIVE:
                continue
            if source[3] == SHT_NOBITS:
                bad_values.append(
                    {"offset": hex(offset), "value": "uninitialized"}
                )
                continue
            value = struct.unpack_from(
                "<I", data, source[2] + offset - source[0]
            )[0]
            if value == 0 or any(lo <= value < hi for lo, hi, _, _, _ in mapped):
                continue
            if S3_MMIO_LOW <= value < S3_MMIO_HIGH and value in absolute_symbols:
                preserved_absolute.append(
                    {"offset": hex(offset), "address": hex(value)}
                )
            else:
                bad_values.append(
                    {
                        "offset": hex(offset),
                        "value": hex(value),
                        "has_absolute_symbol": value in absolute_symbols,
                    }
                )

    return {
        "relocations_examined": relocations,
        "unmapped_relocations": unmapped,
        "unmapped_relative_values": bad_values,
        "absolute_peripheral_relocations": preserved_absolute,
        "unmapped_executable_sections": sorted(set(orphan_exec)),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path)
    args = parser.parse_args()
    result = audit_loader_map(args.elf)
    if (
        result["unmapped_relocations"]
        or result["unmapped_relative_values"]
        or result["unmapped_executable_sections"]
    ):
        for item in result["unmapped_relocations"][:10]:
            print("UNMAPPED RELA:", item)
        for item in result["unmapped_relative_values"][:10]:
            print("UNMAPPABLE RELATIVE VALUE:", item)
        print("UNMAPPED executable sections:", result["unmapped_executable_sections"])
        parser.exit(1, "Provider ELF incompatible with actual runtime relocation map\n")
    print(
        "Provider ELF loader mapping: PASS "
        f"({result['relocations_examined']} sites, "
        f"{len(result['absolute_peripheral_relocations'])} validated ABS MMIO pointers)"
    )


if __name__ == "__main__":
    main()
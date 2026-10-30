#!/usr/bin/env python3
"""Normalize ordinary Xtensa providers; retain the frozen PLT recipe unchanged."""
from pathlib import Path
import struct
from normalize_xtensa_relocations import normalize as normalize_legacy_plt

def normalize(path):
    path = Path(path)
    data = bytearray(path.read_bytes())
    if len(data) < 52 or data[:7] != b'\x7fELF\x01\x01\x01':
        raise ValueError('Not a little-endian 32-bit ELF')

    def u16(pos):
        return struct.unpack_from('<H', data, pos)[0]

    def u32(pos):
        return struct.unpack_from('<I', data, pos)[0]

    if u16(16) != 3 or u16(18) != 94:
        raise ValueError('Only Xtensa shared objects are supported')
    shoff, shentsize, shnum, shstr = u32(32), u16(46), u16(48), u16(50)
    if shentsize != 40 or not 1 <= shnum <= 256 or shstr >= shnum or shoff + shnum * shentsize > len(data):
        raise ValueError('Invalid ELF section table')

    def section(i):
        return shoff + i * shentsize

    strings = section(shstr)
    if u32(strings + 4) != 3 or u32(strings + 16) + u32(strings + 20) > len(data):
        raise ValueError('Invalid section names')
    names_off, names_len = u32(strings + 16), u32(strings + 20)
    sections = {}
    for i in range(shnum):
        name_idx = u32(section(i))
        if name_idx >= names_len:
            raise ValueError('Invalid section name index')
        end = data.find(b'\0', names_off + name_idx, names_off + names_len)
        if end < 0:
            raise ValueError('Unterminated section name')
        name = bytes(data[names_off + name_idx:end]).decode('ascii')
        if name in sections:
            raise ValueError('Duplicate ELF section name: ' + name)
        sections[name] = section(i)
    # An import-free provider may have actionable RELATIVE relocations but no
    # PLT. Nothing needs rewriting in this layout; validate before returning.
    if '.rela.plt' not in sections:
        for name in ('.rela.dyn', '.dynamic'):
            if name not in sections:
                raise ValueError('Missing required section: ' + name)
        rela, dynamic = sections['.rela.dyn'], sections['.dynamic']
        start, size = u32(rela + 16), u32(rela + 20)
        dyn_start, dyn_size = u32(dynamic + 16), u32(dynamic + 20)
        if (u32(rela + 4) != 4 or not size or size % 12 or start % 4 or
                start + size > len(data) or u32(rela + 36) != 12 or
                u32(dynamic + 4) != 6 or dyn_size % 8 or
                dyn_start + dyn_size > len(data)):
            raise ValueError('Unexpected no-PLT relocation/dynamic layout')
        for name, other in sections.items():
            offset, length = u32(other + 16), u32(other + 20)
            if other != rela and u32(other + 4) != 8 and length and offset < start + size and start < offset + length:
                raise ValueError('Overlapping no-PLT relocation section')
        tags = {}
        for offset in range(dyn_start, dyn_start + dyn_size, 8):
            tag, value = u32(offset), u32(offset + 4)
            if tag == 0:
                break
            if tag in (7, 8, 9, 2, 20, 23):
                if tag in tags:
                    raise ValueError('Duplicate no-PLT dynamic tag')
                tags[tag] = value
        else:
            raise ValueError('Unterminated no-PLT dynamic table')
        if any(tag in tags for tag in (2, 20, 23)) or any(tags.get(tag) != value for tag, value in ((7, u32(rela + 12)), (8, size), (9, 12))):
            raise ValueError('Inconsistent no-PLT dynamic relocations')
        for offset in range(start, start + size, 12):
            address, info, _addend = struct.unpack_from('<IIi', data, offset)
            # No import table: accept only symbol-free RELATIVE entries. Do
            # not silently absorb padding or other layouts in this fast path.
            if not address or address % 4 or info != 5:
                raise ValueError('Unsupported no-PLT relocation')
        return 0
    return normalize_legacy_plt(path)

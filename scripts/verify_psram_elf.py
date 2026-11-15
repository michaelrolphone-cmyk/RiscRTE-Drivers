"""Reject hardware compare-and-set in ordinary PSRAM-loaded Xtensa providers.

Use original linked ELFs, before compaction strips Xtensa instruction/literal
properties. Disassembling a compacted image can mistake literal data for code.
This is a conservative qualification gate: the ordinary module may not issue
S32C1I at all; it must delegate synchronization to the scoped native service.
"""
import hashlib
from pathlib import Path
import re
import subprocess


def verify(elf, cc):
    elf = Path(elf)
    prefix = str(cc).removesuffix('gcc')
    sections = subprocess.check_output([prefix+'readelf','-SW',str(elf)],text=True)
    if '.xt.prop ' not in sections:
        raise ValueError('PSRAM check needs original ELF with .xt.prop: '+str(elf))
    assembly = subprocess.check_output([prefix+'objdump','-d',str(elf)],text=True)
    forbidden = [line.strip() for line in assembly.splitlines()
                 if re.search(r'\bs32c1i\s',line,re.IGNORECASE)]
    if forbidden:
        raise ValueError('PSRAM-unsafe hardware CAS in '+str(elf)+': '+repr(forbidden))
    return {'original_elf_sha256':hashlib.sha256(elf.read_bytes()).hexdigest(),
            'xtensa_instruction_properties_present':True,'hardware_cas_instructions':0}

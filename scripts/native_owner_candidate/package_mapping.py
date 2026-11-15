"""Supplement the pinned C structural/import gate with section-loader mapping.

This never executes or authorizes an image. The caller must first run the real
C gate on these exact bytes. Runtime scope, internal allocation/publish support,
import availability, cache safety and physical retirement remain independent.

Conservative package guards beyond the loader: duplicate mutating relocation
sites are refused; RELATIVE must use the null symbol; GLOB_DAT/JMP_SLOT require
named, canonical imports or an exact mapped definition. These prevent claiming
that immutable-file analysis models mutated words or name-based interposition.
The existing S3 peripheral exception is off by default. Explicit conditional
privileged-candidate analysis also requires SHN_ABS proof in .symtab, as in the
retained package verifier, and never grants privileged runtime scope.
"""
import io
import struct

from elftools.elf.elffile import ELFFile

MAX_IMAGE = 8 * 1024 * 1024
U32_MAX = 0xffffffff
MMIO_LOW, MMIO_HIGH = 0x60000000, 0x600fe000
ORDINARY = ('.text', '.data', '.rodata', '.data.rel.ro', '.bss')
INTERNAL = ('.risc.internal.text', '.risc.internal.data',
            '.risc.internal.rodata', '.risc.internal.bss')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def _reserve(section, capacity):
    alignment = section['sh_addralign'] or 1
    require(alignment & (alignment - 1) == 0 and
            section['sh_addr'] & (alignment - 1) == 0,
            'invalid mapped data alignment')
    result = capacity + max(4, alignment) - 1 + section['sh_size']
    require(result <= U32_MAX, 'mapped allocation capacity wraps')
    return result


def inspect_mapping(raw, *, privileged_candidate=False):
    require(type(privileged_candidate) is bool, 'explicit boolean candidate mode required')
    require(isinstance(raw, bytes) and 52 <= len(raw) <= MAX_IMAGE,
            'bounded immutable ELF bytes required')
    elf = ELFFile(io.BytesIO(raw))
    require(elf.elfclass == 32 and elf.little_endian and
            elf['e_type'] == 'ET_DYN' and elf['e_machine'] == 'EM_XTENSA',
            'expected ELF32 little-endian Xtensa shared image')
    sections = list(elf.iter_sections())
    require(0 < len(sections) <= 256, 'invalid section count')
    selected, mapped, seen = {}, {}, set()
    for index, section in enumerate(sections):
        name = section.name
        kind, flags, size = (section['sh_type'], section['sh_flags'],
                             section['sh_size'])
        keep = False
        if name in ORDINARY:
            if name == '.bss':
                keep = kind == 'SHT_NOBITS' and flags & 3 == 3
            else:
                keep = kind == 'SHT_PROGBITS' and bool(flags & 2)
                if name == '.text':
                    keep = keep and bool(flags & 4)
                elif name == '.data':
                    keep = keep and bool(flags & 1)
        elif name.startswith('.risc.internal.'):
            require(name in INTERNAL, 'unknown internal output class')
            slot = INTERNAL.index(name)
            expected_flags = (6, 3, 2, 3)[slot]
            expected_kind = 'SHT_NOBITS' if slot == 3 else 'SHT_PROGBITS'
            require(kind == expected_kind and flags == expected_flags and size > 0,
                    'invalid internal output class')
            _reserve(section, 0)
            if slot == 0:
                require(section['sh_addr'] % 4 == 0 and section['sh_addralign'] >= 4,
                        'invalid internal text alignment')
            keep = True
        if keep:
            require(name not in seen, 'duplicate loaded section')
            seen.add(name)  # The real loader also sees zero-size duplicates.
            selected[name] = section
            if size:
                require(section['sh_addr'] + size <= U32_MAX, 'section wraps')
                for other in mapped.values():
                    require(section['sh_addr'] + size <= other['sh_addr'] or
                            other['sh_addr'] + other['sh_size'] <= section['sh_addr'],
                            'loaded sections overlap')
                mapped[index] = section
        if flags & 6 == 6 and size:
            require(keep, 'executable section is not mapped by section loader')
    require('.text' in selected and selected['.text']['sh_size'] > 0,
            'missing mapped ordinary text')

    external_data = 0
    for name in ORDINARY[1:]:
        section = selected.get(name)
        if section is not None and section['sh_size']:
            external_data = _reserve(section, external_data)
    internal_text, internal_data = 0, 0
    for name in INTERNAL:
        section = selected.get(name)
        if section is None:
            continue
        if name == INTERNAL[0]:
            internal_text = _reserve(section, 0)
            require(internal_text <= U32_MAX - 3, 'internal text capacity wraps')
            internal_text = (internal_text + 3) & ~3
        else:
            internal_data = _reserve(section, internal_data)
    capacity = (selected['.text']['sh_size'] + external_data +
                internal_text + internal_data)
    require(capacity <= MAX_IMAGE, 'mapped allocation exceeds padded image budget')

    def owner(address, width):
        matches = [(index, section) for index, section in mapped.items()
                   if section['sh_addr'] <= address and
                   address - section['sh_addr'] < section['sh_size'] and
                   width <= section['sh_size'] - (address - section['sh_addr'])]
        require(width > 0 and len(matches) == 1,
                'address extent outside exact loaded section')
        return matches[0]

    # Retain the established verifier's static-symbol evidence requirement.
    absolute = set()
    for table in sections:
        if table['sh_type'] == 'SHT_SYMTAB':
            absolute.update(symbol['st_value'] for symbol in table.iter_symbols()
                            if symbol['st_shndx'] == 'SHN_ABS')

    relocations, written = [], set()
    requires_privileged_runtime_scope = False
    for table in sections:
        require(table['sh_type'] != 'SHT_REL', 'implicit-addend relocation rejected')
        if table['sh_type'] != 'SHT_RELA':
            continue
        require(table['sh_link'] < len(sections), 'invalid relocation symbol table')
        symbols = sections[table['sh_link']]
        require(symbols['sh_type'] in ('SHT_SYMTAB', 'SHT_DYNSYM'),
                'invalid relocation symbol table')
        for relocation in table.iter_relocations():
            entry = relocation.entry
            kind, address = entry['r_info_type'], entry['r_offset']
            if kind == 0:
                require(entry['r_info'] == address == entry['r_addend'] == 0,
                        'noncanonical NONE')
                continue
            require(kind in (2, 3, 4, 5), 'unsupported runtime relocation')
            require(address % 4 == 0, 'unaligned relocation site')
            _, destination = owner(address, 4)
            if destination.name == '.text':
                require((address - destination['sh_addr']) % 4 == 0,
                        'ordinary text allocation would misalign relocation')
            require(entry['r_info_sym'] < symbols.num_symbols(),
                    'relocation symbol index out of range')
            symbol = symbols.get_symbol(entry['r_info_sym'])
            row = {'table': table.name, 'type': kind, 'address': address,
                   'mapped_section': destination.name}
            if kind != 2:
                require(address not in written, 'repeated mutating relocation site')
                written.add(address)
            if kind == 5:
                require(entry['r_info_sym'] == 0 and symbol.name == '' and
                        symbol['st_shndx'] == 'SHN_UNDEF' and symbol['st_value'] == 0,
                        'RELATIVE requires canonical null symbol')
                value = 0 if destination['sh_type'] == 'SHT_NOBITS' else struct.unpack_from(
                    '<I', raw, destination['sh_offset'] + address - destination['sh_addr'])[0]
                row.update(initial_value=value,
                           zero_initialized=destination['sh_type'] == 'SHT_NOBITS')
                if value:
                    try:
                        owner(value, 1)
                    except ValueError:
                        require(privileged_candidate and
                                MMIO_LOW <= value < MMIO_HIGH and value in absolute,
                                'unmapped RELATIVE value without exact S3 ABS proof')
                        row['privileged_abs_mmio'] = True
                        requires_privileged_runtime_scope = True
            elif kind in (3, 4):
                require(bool(symbol.name), 'symbol relocation requires a name')
                if symbol['st_shndx'] == 'SHN_UNDEF':
                    require(symbol['st_value'] == 0, 'noncanonical undefined symbol value')
                    row['import'] = symbol.name
                else:
                    actual_index, _ = owner(symbol['st_value'], 1)
                    require(type(symbol['st_shndx']) is int and
                            symbol['st_shndx'] == actual_index,
                            'defined relocation symbol section mismatch')
            relocations.append(row)

    dyn = elf.get_section_by_name('.dynsym')
    require(dyn is not None, 'missing dynamic symbols')
    exports = []
    for symbol in dyn.iter_symbols():
        if symbol['st_shndx'] == 'SHN_UNDEF' or symbol['st_info']['bind'] != 'STB_GLOBAL':
            continue
        kind, index = symbol['st_info']['type'], symbol['st_shndx']
        if kind == 'STT_FUNC':
            actual_index, code = owner(symbol['st_value'], max(1, symbol['st_size']))
            require(type(index) is int and index == actual_index and
                    code.name in ('.text', INTERNAL[0]) and
                    code['sh_type'] == 'SHT_PROGBITS' and code['sh_flags'] & 6 == 6,
                    'function export outside exact owning executable section')
            section = code
        elif kind == 'STT_OBJECT':
            # The loader omits ineligible objects rather than rejecting the ELF.
            if not isinstance(index, int) or index not in mapped or not symbol['st_size']:
                continue
            section = mapped[index]
            if section['sh_flags'] & 4 or section.name not in ORDINARY[1:] + INTERNAL[1:]:
                continue
            value, size = symbol['st_value'], symbol['st_size']
            if not (section['sh_addr'] <= value and
                    value - section['sh_addr'] < section['sh_size'] and
                    size <= section['sh_size'] - (value - section['sh_addr'])):
                continue
        else:
            continue
        exports.append({'name': symbol.name, 'type': kind, 'section': section.name,
                        'visibility': symbol['st_other']['visibility']})
    return {'mapped_sections': [s.name for s in mapped.values()],
            'allocation_capacity': capacity, 'relocations': relocations,
            'loader_exports': exports,
            'exported_functions': sorted(row['name'] for row in exports if row['type'] == 'STT_FUNC'),
            'privileged_candidate': privileged_candidate,
            'requires_privileged_runtime_scope': requires_privileged_runtime_scope,
            'runtime_authority_granted': False,
            'target_execution': False, 'internal_port_available': False,
            'physical_admission': False}

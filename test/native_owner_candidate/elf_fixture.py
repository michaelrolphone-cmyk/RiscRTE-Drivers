"""Constructed ELF fixtures only, never a linked or qualified native owner.

make_elf(imports=(), sections=None, symbols=None, static_symbols=(),
         relocations=(), relocation_name='.rela.dyn', extra_tables=()).
Section dicts: name, address, data (bytes) or size, kind (1/8), flags, align.
Symbol dicts: name, section (name/ABS/UNDEF), value, size, type (2 function,
1 object), bind (1 global), visibility (0 default). Default: t5_driver_get.
Relocation dicts: offset, type, symbol (name/index, default 0), addend (default0).
extra_tables: dicts with name and relocations; every table links .dynsym.
"""
import struct


def make_elf(*, imports=(), sections=None, symbols=None, static_symbols=(),
             relocations=(), relocation_name='.rela.dyn', extra_tables=()):
    source_sections = sections if sections is not None else [
        {'name': '.text', 'address': 0x1000, 'data': bytes(16), 'flags': 6, 'align': 4},
        {'name': '.data', 'address': 0x2000, 'data': bytes(16), 'flags': 3, 'align': 4}]
    rows = [{'name': '', 'kind': 0, 'flags': 0, 'address': 0, 'data': b'', 'align': 0}]
    rows += [dict(row) for row in source_sections]
    indices = {row['name']: index for index, row in enumerate(rows)}
    dynamic = ([{'name': 't5_driver_get', 'section': '.text', 'value': 0x1000,
                 'size': 4, 'type': 2}] if symbols is None else list(symbols))
    dynamic += [{'name': name, 'section': 'UNDEF', 'value': 0, 'type': 2} for name in imports]

    def symbol_table(entries, strings_name, symbols_name):
        strings = bytearray(b'\0')
        encoded = bytearray(16)
        symbol_indices = {'': 0}
        for i, entry in enumerate(entries, 1):
            name = entry.get('name', '')
            offset = len(strings)
            strings += name.encode('ascii') + b'\0'
            section = entry.get('section', 'UNDEF')
            index = {'ABS': 0xfff1, 'UNDEF': 0}.get(section, indices.get(section))
            if isinstance(section, int):
                index = section
            if index is None:
                raise ValueError('unknown fixture symbol section: ' + str(section))
            encoded += struct.pack('<IIIBBH', offset, entry.get('value', 0),
                                   entry.get('size', 0),
                                   entry.get('bind', 1) << 4 | entry.get('type', 0),
                                   entry.get('visibility', 0), index)
            symbol_indices[name] = i
        strings_index = len(rows)
        rows.append({'name': strings_name, 'kind': 3, 'data': bytes(strings), 'align': 1})
        table_index = len(rows)
        rows.append({'name': symbols_name, 'kind': 11 if symbols_name == '.dynsym' else 2,
                     'data': bytes(encoded), 'align': 4, 'entsize': 16,
                     'link': strings_index, 'info': 1})
        return table_index, symbol_indices

    dynamic_index, symbol_indices = symbol_table(dynamic, '.dynstr', '.dynsym')
    if static_symbols:
        symbol_table(list(static_symbols), '.strtab', '.symtab')
    tables = ([{'name': relocation_name, 'relocations': relocations}]
              if relocations else []) + list(extra_tables)
    for table in tables:
        encoded = bytearray()
        for item in table['relocations']:
            symbol = item.get('symbol', 0)
            if isinstance(symbol, str):
                symbol = symbol_indices[symbol]
            encoded += struct.pack('<IIi', item['offset'], symbol << 8 | item['type'],
                                   item.get('addend', 0))
        rows.append({'name': table['name'], 'kind': 4, 'data': bytes(encoded),
                     'align': 4, 'entsize': 12, 'link': dynamic_index})
    shstr_index = len(rows)
    rows.append({'name': '.shstrtab', 'kind': 3, 'align': 1})
    strings = bytearray(b'\0')
    for row in rows:
        row['name_offset'] = len(strings)
        strings += row['name'].encode('ascii') + b'\0'
    rows[shstr_index]['data'] = bytes(strings)
    blob = bytearray(52)
    headers = []
    for row in rows:
        alignment = row.get('align', 1)
        # File placement must remain bounded even for invalid huge alignment fixtures.
        file_alignment = 4 if alignment >= 4 else 1
        blob += bytes((-len(blob)) % file_alignment)
        offset = len(blob)
        data = row.get('data', b'')
        kind = row.get('kind', 1)
        size = row.get('size', len(data))
        if kind != 8:
            blob += data
        headers.append((row['name_offset'], kind, row.get('flags', 0),
                        row.get('address', 0), offset, size,
                        row.get('link', 0), row.get('info', 0), alignment,
                        row.get('entsize', 0)))
    blob += bytes((-len(blob)) % 4)
    shoff = len(blob)
    for row in headers:
        blob += struct.pack('<10I', *row)
    ident = b'\x7fELF\x01\x01\x01' + bytes(9)
    blob[:52] = struct.pack('<16sHHIIIIIHHHHHH', ident, 3, 94, 1, 0x1000,
                           0, shoff, 0, 52, 0, 0, 40, len(rows), shstr_index)
    return bytes(blob)

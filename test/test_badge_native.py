#!/usr/bin/env python3
"""Check the actual linked Badge native selection, not physical behavior."""
import argparse
import json
from pathlib import Path
import shlex
import shutil
import struct
import tempfile
from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parents[1]
BOARD = ROOT / 'boards/waveshare-esp32-s3-touch-amoled-1.75c'


def verify(build, runtime):
    config = json.loads((build / 'config/sdkconfig.json').read_text())
    assert config['ESPTOOLPY_FLASHSIZE'] == '32MB'
    assert config['SPIRAM'] and config['SPIRAM_MODE_OCT']
    assert config['SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY']
    assert config['LIBC_TIME_SYSCALL_USE_RTC_HRT']
    assert config['SPIFFS_PAGE_SIZE'] == 512
    assert config['PARTITION_TABLE_CUSTOM_FILENAME'] == str(BOARD / 'native/partitions.csv')
    rows = json.loads((build / 'compile_commands.json').read_text())
    required = (
        runtime / 'src/bootstrap/Runtime.cpp',
        runtime / 'src/ports/esp32s3/NativeHardware.cpp',
        runtime / 'src/ports/esp32s3/CpuPort.cpp',
        ROOT / 'components/co5300/native/Co5300Display.cpp',
        BOARD / 'native/BadgeIdentity.cpp',
    )
    for path in required:
        matched = [row for row in rows if Path(row['file']).resolve() == path.resolve()]
        assert len(matched) == 1, str(path)
        args = matched[0].get('arguments') or shlex.split(matched[0]['command'])
        assert '-DRISC_NATIVE_ESP_IDF=1' in args
        assert '-DRISC_APP_REQUIREMENT_ROWS=24' in args
        assert not any(arg.startswith('-DRISC_APP_POLICY_ROWS=') for arg in args)
        assert str(BOARD / 'display-profile.h') in args
        # These are compile-time feature gates, not an ABI implemented by stubs.
        for excluded in ('RISC_ENABLE_SDMMC', 'RISC_ENABLE_RADIO_IQ',
                         'RISC_PAIRED_BANKS', 'RISC_PAIRED_APP_DATA'):
            assert not any(arg.startswith('-D' + excluded) for arg in args), excluded
    forbidden = {str(BOARD / 'touch-startup.c'), str(BOARD / 'startup.c'),
                 str(ROOT / 'boards/common/prepare.c')}
    assert not any(str(Path(row['file']).resolve()) in forbidden for row in rows)
    with (build / 'riscrte_native.elf').open('rb') as stream:
        elf = ELFFile(stream)
        assert elf.elfclass == 32 and elf.little_endian and elf['e_machine'] == 'EM_XTENSA'
        syms = list(elf.get_section_by_name('.symtab').iter_symbols())
        names = [symbol.name for symbol in syms if symbol['st_shndx'] != 'SHN_UNDEF']
        marker = [s for s in syms if s.name == 'risc_badge_native_identity']
        assert len(marker) == 1
        marker = marker[0]
        section = elf.get_section(marker['st_shndx'])
        # The pinned IDF linker merges inputs into flash.rodata with WA flags.
        # Prove its actual mapped flash placement, not generic section flags.
        assert section.name == '.flash.rodata' and section['sh_flags'] & 2
        assert 0x3c000000 <= marker['st_value'] < 0x3e000000
        offset = marker['st_value'] - section['sh_addr']
        identity = struct.unpack('<7I', section.data()[offset:offset + marker['st_size']])
        assert identity == (1, 466, 466, 6, 0, 32 * 1024 * 1024, 8 * 1024 * 1024)
        assert 'risc_co5300_native_display_anchor' in names
        assert any('selectedDisplayBackend' in n for n in names)
        assert any('nativeHardware' in n for n in names)
        assert any('Port4bind' in n for n in names)
        assert any('i2cOpen' in n for n in names)
        assert any('gpioOpen' in n for n in names)
        assert not any('NativeSdmmc' in n or 'NativeRadioIq' in n for n in names)
        assert '__wrap_app_main' not in names
    raw = (build / 'partition_table/partition-table.bin').read_bytes()
    parts = []
    for offset in range(0, len(raw), 32):
        magic, kind, subkind, start, size, name, flags = struct.unpack('<HBBII16sI', raw[offset:offset+32])
        if magic != 0x50aa:
            break
        parts.append((name.rstrip(b'\0').decode(), kind, subkind, start, size, flags))
    assert parts == [
        ('nvs', 1, 2, 0x9000, 0x6000, 0),
        ('factory', 0, 0, 0x10000, 0x300000, 0),
        ('bootfs', 1, 0x82, 0x310000, 0x1cf0000, 0),
    ], parts
    page_size = config['SPIFFS_PAGE_SIZE']
    assert 0x1cf0000 // page_size <= 65535
    assert 2 + 2 * (0x1cf0000 // (2 * page_size)) <= 65535
    assert (build / 'riscrte_native.bin').stat().st_size <= 0x300000
    proof = {'linked_native_selection': True, 'hardware_qualified': False,
             'display': {'width': 466, 'height': 466, 'offset_x': 6, 'offset_y': 0},
             'flash_bytes': 32 * 1024 * 1024, 'psram_expected_bytes': 8 * 1024 * 1024,
             'application_requirement_rows': 24, 'application_live_grants': 16,
             'bootfs': {'offset': 0x310000, 'bytes': 0x1cf0000, 'page_size': page_size,
                        'object_name_length': config['SPIFFS_OBJ_NAME_LEN'],
                        'metadata_length': config['SPIFFS_META_LENGTH'],
                        'use_magic': config['SPIFFS_USE_MAGIC'],
                        'use_magic_length': config['SPIFFS_USE_MAGIC_LENGTH'],
                        'aligned_object_index_tables': False, 'block_size': 4096},
             'native_registration': 'Runtime NativeHardware and CpuPort plus selectedDisplayBackend',
             'native_startup_gpio_reset': False, 'pmic_writes': False,
             'sd_selected': False, 'external_rtc_selected': False,
             'boot_store_verified': False}
    (build / 'qualification').mkdir(exist_ok=True)
    (build / 'qualification/badge-native.json').write_text(json.dumps(proof, indent=2) + '\n')
    return proof


def negative_checks(build, runtime):
    """Mutate copied evidence and require the same linked-target checker to fail."""
    cases = ['wrong-flash-size', 'wrong-spiffs-pages', 'missing-native-backend',
             'native-touch-reset', 'wrong-partition-bytes', 'wrong-panel-marker']
    with tempfile.TemporaryDirectory(prefix='badge-native-negatives-') as temp:
        root = Path(temp)
        for case in cases:
            trial = root / case
            (trial / 'config').mkdir(parents=True)
            (trial / 'partition_table').mkdir()
            for name in ('config/sdkconfig.json', 'compile_commands.json',
                         'partition_table/partition-table.bin'):
                shutil.copy2(build / name, trial / name)
            (trial / 'riscrte_native.bin').symlink_to(build / 'riscrte_native.bin')
            if case == 'wrong-panel-marker':
                shutil.copy2(build / 'riscrte_native.elf', trial / 'riscrte_native.elf')
                with (trial / 'riscrte_native.elf').open('rb') as stream:
                    elf = ELFFile(stream)
                    marker = next(s for s in elf.get_section_by_name('.symtab').iter_symbols()
                                  if s.name == 'risc_badge_native_identity')
                    section = elf.get_section(marker['st_shndx'])
                    offset = section['sh_offset'] + marker['st_value'] - section['sh_addr']
                with (trial / 'riscrte_native.elf').open('r+b') as stream:
                    stream.seek(offset + 4)
                    stream.write(struct.pack('<I', 480))
            else:
                (trial / 'riscrte_native.elf').symlink_to(build / 'riscrte_native.elf')
            if case in ('wrong-flash-size', 'wrong-spiffs-pages'):
                path = trial / 'config/sdkconfig.json'
                config = json.loads(path.read_text())
                config['ESPTOOLPY_FLASHSIZE' if case == 'wrong-flash-size' else 'SPIFFS_PAGE_SIZE'] = (
                    '16MB' if case == 'wrong-flash-size' else 256)
                path.write_text(json.dumps(config))
            elif case in ('missing-native-backend', 'native-touch-reset'):
                path = trial / 'compile_commands.json'
                rows = json.loads(path.read_text())
                if case == 'missing-native-backend':
                    rows = [r for r in rows if not r['file'].endswith('/Co5300Display.cpp')]
                else:
                    rows.append({'file': str(BOARD / 'touch-startup.c'), 'arguments': []})
                path.write_text(json.dumps(rows))
            elif case == 'wrong-partition-bytes':
                path = trial / 'partition_table/partition-table.bin'
                raw = bytearray(path.read_bytes())
                struct.pack_into('<I', raw, 64 + 8, 0x4f0000)
                path.write_bytes(raw)
            try:
                verify(trial, runtime)
            except AssertionError:
                continue
            raise AssertionError('Mutated native evidence was accepted: ' + case)
    return cases


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', required=True, type=Path)
    parser.add_argument('--runtime', required=True, type=Path)
    args = parser.parse_args()
    build, runtime = args.build.resolve(), args.runtime.resolve()
    proof = verify(build, runtime)
    proof['rejected_mutations'] = negative_checks(build, runtime)
    (build / 'qualification/badge-native.json').write_text(json.dumps(proof, indent=2) + '\n')
    print('Linked Badge native profile, real CPU port, panel selection, flash layout and six negative cases: PASS')


if __name__ == '__main__':
    main()

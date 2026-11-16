#!/usr/bin/env python3
"""Build the selected Badge display facade/supply and private startup archive.

This is component qualification, not a board image, boot graph or release.
The actual shared native backend is separately tested by test_co5300_native.py.
"""
import argparse
import hashlib
import json
import os
import shutil
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
BOARD = ROOT / 'boards/waveshare-esp32-s3-touch-amoled-1.75c'


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('watch', 'runtime', 'output'):
        p.add_argument('--' + name, type=Path, required=True)
    p.add_argument('--drivers', type=Path, default=ROOT)
    p.add_argument('--cc', required=True)
    a = p.parse_args()
    watch, runtime, drivers, out = [getattr(a, x).resolve()
                                    for x in ('watch', 'runtime', 'drivers', 'output')]
    out.mkdir(parents=True, exist_ok=True)
    # These are existing Runtime ABI copies, not independent definitions.
    for name, actual in (('RiscDisplayBackendV1.h', 'sdk/driver/RiscDisplayBackendV1.h'),
                         ('RiscDisplaySpiV2.h', 'sdk/hardware/RiscDisplaySpiV2.h'),
                         ('RiscI2cFunctionV1.h', 'sdk/hardware/RiscI2cFunctionV1.h')):
        if (ROOT / 'components/co5300/include' / name).read_bytes() != (runtime / actual).read_bytes():
            raise RuntimeError('Shared Runtime ABI copy differs: ' + name)
    version = subprocess.check_output([a.cc, '-dumpfullversion'], text=True).strip()
    if version != '15.2.0':
        raise RuntimeError('This qualification route uses the pinned GCC15.2.0 compiler')
    sys.path.insert(0, str(watch / 'scripts'))
    from normalize_xtensa_relocations import normalize
    from compact_current_elf import compact
    from verify_psram_elf import verify as verify_psram
    validator = out / 'validate-elf'
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-I' + str(runtime / 'test/native_apps/stubs'),
                    '-I' + str(runtime / 'lib/elf_loader/include'),
                    str(runtime / 'lib/elf_loader/src/esp_elf_validate.c'),
                    str(runtime / 'test/native_apps/validate_test.c'), '-o', str(validator)], check=True)
    common = ['-std=c11', '-Os', '-fPIC', '-mtext-section-literals', '-mlongcalls',
              '-fvisibility=hidden', '-ffreestanding', '-fno-builtin', '-Wall', '-Wextra',
              '-Werror', '-Wno-misleading-indentation', '-ffunction-sections', '-fdata-sections']
    from cst9217_sdk import stage_sdk
    sdk=out/'provider-sdk';sdk.mkdir(exist_ok=True)
    for directory in (watch/'sdk/driver',watch/'sdk/app'):
        for header in directory.glob('*.h'):shutil.copy2(header,sdk/header.name)
    stage_sdk(drivers,runtime,sdk)
    includes = ['-I' + str(x) for x in (ROOT / 'components/co5300/include', sdk, watch / 'include')]
    records = {}
    for name, source in (('co5300_display', ROOT / 'components/co5300/facade/driver.c'),
                         ('dcdc1_observer', BOARD / 'supply/driver.c')):
        target = out / name
        target.mkdir(exist_ok=True)
        manifest = json.loads(source.with_name('manifest.json').read_text())
        assert manifest['driver_abi'] == 2 and manifest['architecture'] == 'xtensa-esp32s3'
        assert manifest['id'] == {'co5300_display': 'co5300-display',
                                  'dcdc1_observer': 'axp2101-dcdc1-observer'}[name]
        shutil.copy2(source.with_name('manifest.json'), target / 'manifest.json')
        mapping = target / 'exports.map'
        mapping.write_text('{ global: t5_driver_get; local: *; };\n')
        elf = target / 'driver.elf'
        subprocess.run([a.cc, *common, *includes, '-include', str(BOARD / 'display-profile.h'),
                        '-nostdlib', '-nostartfiles', '-shared', '-Wl,--no-relax',
                        '-Wl,--hash-style=sysv', '-Wl,--gc-sections',
                        '-Wl,--version-script=' + str(mapping), str(source), '-lgcc', '-o', str(elf)], check=True)
        normalize(elf)
        proof = compact(elf, a.cc, debug_path=out / 'debug' / (name + '.elf'))
        psram=verify_psram(out/'debug'/(name+'.elf'),a.cc)
        subprocess.run([str(validator), str(elf)], check=True)
        symbols = subprocess.check_output([a.cc.removesuffix('gcc') + 'nm', '-D', str(elf)], text=True)
        imports = {line.split()[-1] for line in symbols.splitlines() if ' U ' in ' ' + line}
        exports = {line.split()[-1] for line in symbols.splitlines()
                   if len(line.split()) >= 3 and line.split()[-2] in ('T', 'D', 'B', 'R')}
        assert imports <= {'memcpy', 'memset', 'memcmp', 'memmove', 'strcmp', 'strlen',
                           'risc_runtime_get_api', 'snprintf'}, imports
        assert exports == {'t5_driver_get'}, exports
        raw = elf.read_bytes()
        assert raw[:7] == b'\x7fELF\x01\x01\x01' and raw[16:20] == b'\x03\x00\x5e\x00'
        records[name] = {'id': manifest['id'], 'version': manifest['version'],
                         'sha256': hashlib.sha256(raw).hexdigest(), 'bytes': len(raw),
                         'imports': sorted(imports), 'loader_validated': True, 'compaction': proof, 'psram_safety': psram}
        print(name + ': selected Badge target ABI and actual Runtime loader validation PASS', flush=True)
    objects = []
    for name, source in (('prepare', drivers / 'boards/common/prepare.c'),
                         ('startup', drivers / 'boards/waveshare-esp32-s3-touch-amoled-1.75c/startup.c'),
                         ('touch-startup', BOARD / 'touch-startup.c')):
        obj = out / (name + '.o')
        subprocess.run([a.cc, *common, '-I' + str(drivers / 'sdk/driver'),
                        '-I' + str(drivers / 'boards/common'),
                        '-I' + str(drivers / 'boards/waveshare-esp32-s3-touch-amoled-1.75c'),
                        '-c', str(source), '-o', str(obj)], check=True)
        objects.append(obj)
    archive = out / 'badge-touch-startup.a'
    subprocess.run([a.cc.removesuffix('gcc') + 'ar', 'rcs', str(archive), *map(str, objects)], check=True)
    revisions = {name: subprocess.check_output(['git', '-C', str(path), 'rev-parse', 'HEAD'], text=True).strip()
                 for name, path in (('watch', watch), ('runtime', runtime), ('drivers', drivers))}
    notices = ROOT / 'components/co5300'
    for path in (notices / 'licenses').glob('*'):
        if path.is_file():
            (out / 'licenses').mkdir(exist_ok=True)
            shutil.copy2(path, out / 'licenses' / path.name)
    for name in ('THIRD_PARTY_NOTICES.md', 'PROVENANCE.md', 'source-map.json'):
        shutil.copy2(notices / name, out / name)
    source_hashes = {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
                     for base in (notices, BOARD) for path in sorted(base.rglob('*'))
                     if path.is_file()}
    (out / 'build.json').write_text(json.dumps({'schema': 1, 'compiler': version,
        'dependencies': revisions, 'providers': records, 'source_sha256': source_hashes,
        'startup_archive_sha256': hashlib.sha256(archive.read_bytes()).hexdigest(),
        'installable_firmware': False, 'physical_verification': False}, indent=2) + '\n')
    print('Private touch-only startup archive: GCC15 PIC target compilation PASS')


if __name__ == '__main__':
    main()

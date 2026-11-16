#!/usr/bin/env python3
"""Compose the exact Badge Home store from real, pinned, reusable providers.

Only packaging declarations change. The maintained Home is built without any
board-specific define and copied byte for byte. This command neither creates a
flash image nor claims physical qualification.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
BOARD = ROOT / 'boards/waveshare-esp32-s3-touch-amoled-1.75c'
PROFILE = BOARD / 'home'


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + '\n')


def run(*args):
    subprocess.run(list(map(str, args)), check=True)


def home_manifest(original, policy):
    """Keep every declaration, with requiredness justified by supported grants.

    Missing services remain explicit optional declarations, never fake providers.
    Required display/touch/native time/promotion are not weakened by a caller.
    """
    required = {'display.output', 'input.touch.raw',
                'runtime.realtime-control', 'runtime.provider-promotion'}
    grants = policy['grants']
    if {(g['capability'], g['api']) for g in grants} != {(c, 1) for c in required}:
        raise ValueError('The minimum Home contract must remain intact')
    declarations = original['requires'] + original.get('optional', [])
    if len({d['capability'] for d in declarations}) != len(declarations):
        raise ValueError('Duplicate source declaration')
    selected = [d for d in declarations if d['capability'] in required]
    if {(d['capability'], d['api']) for d in selected} != {(c, 1) for c in required}:
        raise ValueError('Shared Home declarations differ from the reviewed contract')
    manifest = dict(original)
    manifest['requires'] = selected
    manifest['optional'] = [d for d in declarations if d['capability'] not in required]
    return manifest


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('runtime', 'watch', 'controllers', 'system', 'utilities', 'output'):
        p.add_argument('--' + name, type=Path, required=True)
    p.add_argument('--cc', required=True)
    a = p.parse_args()
    roots = {n: getattr(a, n).resolve() for n in
             ('runtime', 'watch', 'controllers', 'system', 'utilities')}
    out = a.output.resolve()
    if out.exists() and any(out.iterdir()):
        p.error('Use an empty output directory')
    out.mkdir(parents=True, exist_ok=True)
    lock = json.loads((PROFILE / 'sources.lock.json').read_text())
    sys.path.insert(0, str(roots['watch'] / 'scripts'))
    from build_inputs import verify_dependency, file_manifest
    from compact_current_elf import compact
    from normalize_xtensa_relocations import normalize
    from verify_psram_elf import verify as verify_psram
    paths = {
        'watch': ['apps', 'include', 'sdk', 'scripts', 'dependencies.lock.json'],
        'runtime': ['src', 'sdk', 'lib/elf_loader', 'test/native_apps', 'test/drivers/stubs'],
        'controllers': ['drivers/twatch_i2c', 'include', 'scripts/verify_update_elf.py'],
        'system': ['lib/PortableApps', 'lib/elf_loader', 'test/native_apps'],
        'utilities': ['Apps/alarm_writer.h', 'lib/Alarm/include', 'lib/Contexts/include'],
    }
    verified = {n: verify_dependency(r, lock[n]['commit'], paths[n]) for n, r in roots.items()}
    version = subprocess.check_output([a.cc, '--version'], text=True).splitlines()[0]
    if not all(v in version for v in ('15.2.0', 'esp-15.2.0_20251204')):
        raise ValueError('Unexpected compiler cohort')
    local_paths = ['components/co5300', 'components/i2c-scoped', 'Drivers/cst9217_touch',
                   'boards/waveshare-esp32-s3-touch-amoled-1.75c', 'scripts/build_badge_home.py',
                   'scripts/cst9217_sdk.py', 'scripts/verify_psram_elf.py', 'sdk/driver']
    local_inputs = file_manifest(ROOT, local_paths)
    home = out / 'shared-home'
    run(sys.executable, roots['watch'] / 'scripts/build.py', '--system', roots['system'],
        '--utilities', roots['utilities'], '--cc', a.cc, '--role', 'default', '--output', home)
    home_psram = verify_psram(home / 'debug/default.elf', a.cc)
    store = out / 'store'
    store.mkdir()
    for name in ('board.json', 'boot.json'):
        shutil.copy2(PROFILE / name, store / name)
    boot = json.loads((store / 'boot.json').read_text())
    original = json.loads((home / 'default.json').read_text())
    if original['version'] != '0.19.4':
        raise ValueError('Unexpected maintained Home version')
    selected = home_manifest(original, boot['app_capabilities'][0])
    write(store / 'default.json', selected)
    shutil.copy2(home / 'default.elf', store / 'default.elf')
    if sha(home / 'default.elf') != sha(store / 'default.elf'):
        raise ValueError('Shared Home bytes changed during staging')
    common = ['-std=c11', '-Os', '-fPIC', '-mtext-section-literals', '-mlongcalls',
              '-fvisibility=hidden', '-ffreestanding', '-fno-builtin', '-Wall', '-Wextra',
              '-Werror', '-Wno-misleading-indentation', '-ffunction-sections', '-fdata-sections',
              '-nostdlib', '-nostartfiles', '-shared', '-Wl,--gc-sections', '-Wl,--no-relax',
              '-Wl,--hash-style=sysv']
    from cst9217_sdk import stage_sdk
    sdk = out / 'provider-sdk'
    sdk.mkdir()
    for directory in (roots['watch'] / 'sdk/driver', roots['watch'] / 'sdk/app'):
        for header in directory.glob('*.h'):
            shutil.copy2(header, sdk / header.name)
    stage_sdk(ROOT, roots['runtime'], sdk)
    includes = [ROOT / 'components/co5300/include', sdk, roots['watch'] / 'include',
                roots['controllers'] / 'include']
    records = {}
    sources = [('i2c', ROOT / 'components/i2c-scoped/driver.c'),
               ('display', ROOT / 'components/co5300/facade/driver.c'),
               ('supply', BOARD / 'supply/driver.c'),
               ('touch', ROOT / 'Drivers/cst9217_touch/driver.c')]
    for name, source in sources:
        destination = store / name
        destination.mkdir()
        mapping = out / (name + '.map')
        mapping.write_text('{ global: t5_driver_get; local: *; };\n')
        elf = destination / 'driver.elf'
        run(a.cc, *common, *['-I' + str(i) for i in includes],
            *(['-include', str(BOARD / 'display-profile.h')] if name == 'display' else []),
            '-Wl,--version-script=' + str(mapping), source, '-lgcc', '-o', elf)
        normalize(elf)
        proof = compact(elf, a.cc, debug_path=out / 'debug' / (name + '.elf'))
        psram = verify_psram(out / 'debug' / (name + '.elf'), a.cc)
        run(home / 'validate-elf', elf)
        symbols = subprocess.check_output([a.cc.removesuffix('gcc') + 'nm', '-D', elf], text=True)
        imports = {r.split()[-1] for r in symbols.splitlines() if ' U ' in ' ' + r}
        if not imports <= {'memcpy', 'memset', 'memcmp', 'memmove', 'strcmp', 'strlen', 'snprintf',
                           'risc_runtime_get_api'}:
            raise ValueError(f'{name}: requires unapproved native imports: {imports}')
        shutil.copy2(source.with_name('manifest.json'), destination / 'manifest.json')
        records[name] = {'sha256': sha(elf), 'bytes': elf.stat().st_size,
                         'imports': sorted(imports), 'compaction': proof, 'psram_safety': psram}
    if local_inputs != file_manifest(ROOT, local_paths):
        raise ValueError('Local composition inputs changed during build')
    for n, root in roots.items():
        if verified[n] != verify_dependency(root, lock[n]['commit'], paths[n]):
            raise ValueError('Pinned input changed during build: ' + n)
    write(out / 'build.json', {'schema': 1, 'source_pins': lock, 'compiler': version,
        'verified_dependencies': verified, 'source_sha256': local_inputs,
        'home': {'version': original['version'], 'sha256': sha(store / 'default.elf'),
                 'unchanged_from_shared_build': True, 'psram_safety': home_psram, 'required': selected['requires'],
                 'unavailable_optional': selected['optional']}, 'providers': records,
        'store_files': {p.relative_to(store).as_posix(): {'sha256': sha(p), 'bytes': p.stat().st_size}
                        for p in sorted(store.rglob('*')) if p.is_file()},
        'real_runtime_admission': False, 'native_link_verified': False,
        'installable_firmware': False, 'hardware_verified': False})
    print('Badge Home store built; real Runtime admission and native link are separate required checks.')


if __name__ == '__main__':
    main()

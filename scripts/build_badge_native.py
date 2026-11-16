#!/usr/bin/env python3
"""Build/link the pinned Badge native Runtime. Never flash or access a device.

The boot store and its independent loadable providers are composed separately.
This uses Runtime's existing product profile and native CPU port, not fake
providers, a copied Runtime, or native GPIO/PMIC startup choreography.
"""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
BOARD = ROOT / 'boards/waveshare-esp32-s3-touch-amoled-1.75c/native'
RUNTIME_REVISION = '13eca114cb58469cf6004fafc3da84fbd9d29beb'
IDF_REVISION = 'fff9895c82d744c7237be8847347bdd1b07c6643'
TOOLCHAIN = 'esp-15.2.0_20251204'


def output(command, **kwargs):
    return subprocess.check_output([str(x) for x in command], text=True, **kwargs).strip()


def pinned_checkout(path, revision, label):
    actual = output(['git', '-C', path, 'rev-parse', 'HEAD'])
    if actual != revision:
        raise RuntimeError(f'{label} revision must be {revision}, got {actual}')
    if output(['git', '-C', path, 'status', '--porcelain']):
        raise RuntimeError(f'{label} checkout must be clean')
    return actual


def run(command, log, env):
    with log.open('w') as stream:
        stream.write(' '.join(str(x) for x in command) + '\n')
        stream.flush()
        result = subprocess.run([str(x) for x in command], env=env,
                                stdout=stream, stderr=subprocess.STDOUT)
    if result.returncode:
        print(log.read_text()[-16000:], file=sys.stderr)
        raise RuntimeError(f'Command failed ({result.returncode}); full log: {log}')


def selected_profile(prototype=False):
    board = BOARD.parent / 'prototype/native' if prototype else BOARD
    revision = RUNTIME_REVISION
    if prototype:
        lock = json.loads((board.parent / 'sources.lock.json').read_text())
        runtime = lock['runtime']
        if (runtime.get('repository') != 'michaelrolphone-cmyk/RiscRTE' or
                not re.fullmatch(r'[0-9a-f]{40}', runtime.get('commit', ''))):
            raise ValueError('Prototype requires an exact owning Runtime source lock')
        revision = runtime['commit']
    return board, revision


def local_sources(board=BOARD):
    paths = sorted([p for directory in (BOARD,board,ROOT/'components/co5300/native',ROOT/'components/co5300/include')
                    for p in directory.rglob('*') if p.is_file()] +
                   [BOARD.parent/'display-profile.h',Path(__file__),ROOT/'test/test_badge_native.py'])
    if board != BOARD:paths.append(board.parent/'sources.lock.json')
    return {str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--prototype', action='store_true', help='Select the exact prototype profile and source lock')
    p.add_argument('--quick-actions', action='store_true', help='Select the copied preference broker; requires prototype')
    p.add_argument('--runtime', required=True, type=Path)
    p.add_argument('--idf', required=True, type=Path)
    p.add_argument('--cc', required=True, type=Path)
    p.add_argument('--python', default=sys.executable, type=Path,
                   help='Python in an installed ESP-IDF 6.1 environment')
    p.add_argument('--output', required=True, type=Path)
    a = p.parse_args()
    if a.quick_actions and not a.prototype:p.error('quick-actions requires prototype')
    runtime, idf, cc, python, out = [getattr(a, name).absolute()
                                    for name in ('runtime', 'idf', 'cc', 'python', 'output')]
    out.mkdir(parents=True, exist_ok=True)
    # Clear stale success even when pin/compiler preflight fails.
    (out / 'build-proof.json').unlink(missing_ok=True)
    board, runtime_revision = selected_profile(a.prototype)
    # Preserve the ESP32-S3 frontend symlink: resolving it changes GCC target.
    pinned_checkout(runtime, runtime_revision, 'Runtime')
    pinned_checkout(idf, IDF_REVISION, 'ESP-IDF')
    version = output([cc, '--version'])
    if output([cc, '-dumpfullversion']) != '15.2.0' or TOOLCHAIN not in version:
        raise RuntimeError('Badge native build requires the exact GCC15 SDK cohort')
    macros = output([cc, '-dM', '-E', '-x', 'c', '-'], input='')
    if '#define __XTENSA_EL__ 1' not in macros or '#define __XTENSA_WINDOWED_ABI__ 1' not in macros:
        raise RuntimeError('Compiler must select the little-endian ESP32-S3 windowed ABI')
    env = dict(os.environ, IDF_PATH=str(idf), IDF_TARGET='esp32s3',
               IDF_PYTHON_ENV_PATH=str(python.parent.parent))
    env['PATH'] = os.pathsep.join((str(cc.parent), str(python.parent), env.get('PATH', '')))
    cmake = shutil.which('cmake', path=env['PATH'])
    ninja = shutil.which('ninja', path=env['PATH'])
    if not cmake or not ninja:
        raise RuntimeError('Install/export ESP-IDF CMake and Ninja before building')
    before = local_sources(board)
    build = out / 'build'
    profile = board / 'product-profile.cmake'
    run([cmake, '-S', runtime / 'idf', '-B', build, '-G', 'Ninja',
         '-DCMAKE_TOOLCHAIN_FILE=' + str(idf / 'tools/cmake/toolchain-esp32s3.cmake'),
         '-DPYTHON=' + str(python), '-DRISC_PRODUCT_PROFILE=' + str(profile),
         '-DRISC_SOURCE_SHA=' + runtime_revision,
         *(['-DRISC_BADGE_QUICK_ACTIONS=' + ('ON' if a.quick_actions else 'OFF')] if a.prototype else [])], out / 'configure.log', env)
    run([cmake, '--build', build, '--', '-j1'], out / 'build.log', env)
    if before != local_sources(board):raise RuntimeError('Native source bytes changed during build')
    pinned_checkout(runtime,runtime_revision,'Runtime')
    pinned_checkout(idf,IDF_REVISION,'ESP-IDF')
    qualify(runtime, idf, cc, python, out, env, version, before, a.prototype, a.quick_actions)


def qualify(runtime, idf, cc, python, out, env, version, before, prototype=False, quick_actions=False):
    """Audit a completed link without rebuilding or changing its image bytes."""
    if quick_actions and not prototype:raise ValueError("quick-actions requires prototype")
    board, runtime_revision = selected_profile(prototype)
    build = out / 'build'
    run([python, runtime / 'test/native_firmware_idf6_test.py', '--build', build,
         '--cxxfilt', cc.with_name('xtensa-esp-elf-c++filt')], out / 'runtime-proof.log', env)
    run([python, ROOT / 'test/test_badge_native.py', '--build', build,
         '--runtime', runtime, *(['--prototype'] if prototype else []),
         *(['--quick-actions'] if quick_actions else [])], out / 'badge-proof.log', env)
    if before != local_sources(board):raise RuntimeError('Native source bytes changed during qualification')
    pinned_checkout(runtime,runtime_revision,'Runtime')
    pinned_checkout(idf,IDF_REVISION,'ESP-IDF')
    report = {
        'runtime_revision': runtime_revision, 'prototype': prototype, 'quick_actions': quick_actions,
        'preference_store': {'enabled': quick_actions, 'atomic_key_value': False,
                             'backend': 'existing namespace KV copied synchronous operations' if quick_actions else None},
        'idf_revision': IDF_REVISION,
        'compiler': version, 'jobs': 1, 'hardware_qualified': False,
        'boot_store_included': False, 'full_link': True,
        'sources_sha256': before,
        'artifacts': {str(p.relative_to(build)): {
            'bytes': p.stat().st_size, 'sha256': hashlib.sha256(p.read_bytes()).hexdigest()}
            for p in (build / 'riscrte_native.elf', build / 'riscrte_native.bin',
                      build / 'bootloader/bootloader.bin', build / 'partition_table/partition-table.bin')},
        'limits': 'Native image only; compose and admit the boot store separately. No flash, device access, physical qualification or release.'
    }
    (out / 'build-proof.json').write_text(json.dumps(report, indent=2) + '\n')
    print('Pinned Badge native image linked and verified; no hardware qualification.')
    print(out / 'build-proof.json')


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""Test production scoped I2C, real Runtime tables, and Xtensa atomic safety.

Both normal and ASan/UBSan modes run by default. Target compilation is required:
pass --cc or set XTENSA_CC to the selected cohort's xtensa-esp32s3-elf-gcc.
Only the native hardware boundary is modeled in the real Runtime integration.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile
from cst9217_sdk import stage_sdk

ROOT = Path(__file__).resolve().parents[1]


def run(*args, env=None):
    subprocess.run(list(map(str, args)), check=True, env=env)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--runtime', required=True, type=Path)
    p.add_argument('--cc', default=os.environ.get('XTENSA_CC'))
    p.add_argument('--output', type=Path, help='retain target ELF, disassembly and receipt')
    a = p.parse_args()
    if not a.cc:
        p.error('--cc or XTENSA_CC is required; target atomic checks must not be silently skipped')
    runtime = a.runtime.resolve()
    environment = os.environ.copy()
    environment['ASAN_OPTIONS'] = environment.get('ASAN_OPTIONS', 'detect_leaks=1') + ':halt_on_error=1'
    environment['UBSAN_OPTIONS'] = environment.get('UBSAN_OPTIONS', '') + ':halt_on_error=1:print_stacktrace=1'
    with tempfile.TemporaryDirectory(prefix='scoped-i2c-') as temporary:
        work = Path(temporary)
        sdk = stage_sdk(ROOT, runtime, work / 'sdk')
        source = ROOT / 'components/i2c-scoped/driver.c'
        test = ROOT / 'tests/scoped_i2c_test.c'
        native_sdk = work / 'native-sdk'
        shutil.copytree(sdk, native_sdk)
        for directory in (runtime / 'sdk/app', runtime / 'sdk/driver', runtime / 'sdk/hardware'):
            for header in directory.glob('*.h'):
                shutil.copyfile(header, native_sdk / header.name)
        includes = [native_sdk, runtime / 'src', runtime / 'lib/ArduinoJson/src',
                    runtime / 'test', runtime / 'test/drivers/stubs']
        flags = ['-Wall', '-Wextra', '-Werror', '-O1', '-g']
        include_flags = ['-I' + str(d) for d in includes]
        native_sources = [runtime / relative for relative in (
            'src/bootstrap/Json.cpp', 'src/bootstrap/Board.cpp', 'src/bootstrap/Runtime.cpp',
            'src/runtime/streams/AppStreamSessions.cpp', 'src/runtime/streams/ProviderQueueHost.cpp',
            'src/runtime/drivers/ProviderGraphV2.cpp', 'src/runtime/drivers/ProviderModuleV2.cpp',
            'src/ports/esp32s3/CpuPort.cpp')]
        # Generalized pins/controller mapping differs from the Badge profile.
        board = {'schema': 'riscrte.board-hardware', 'schema_version': 1,
                 'board_id': 'scoped-i2c-test', 'revision': 'unspecified', 'buses': [], 'devices': []}
        for instance, bus, logical, physical, sda, scl, hz in (
                (27, 907, 1, 0, 8, 9, 100000), (28, 908, 0, 1, 10, 11, 400000)):
            board['buses'].append({'instance_id': bus, 'kind': 'i2c',
                'controller_namespace': 'riscrte.logical', 'controller': logical,
                'physical_controller': physical, 'frequency_hz': hz, 'mode': 0,
                'pins': {'sda': sda, 'scl': scl}})
            board['devices'].append({'instance_id': instance,
                'chip': {'vendor': 'espressif', 'model': 'esp32s3-i2c', 'revision': 'unspecified'},
                'compatible': 'espressif,esp32s3-i2c', 'config_type': 'controller.i2c',
                'config_version': 1, 'config': {'bus_instance_id': bus}})
        (work / 'board.json').write_text(json.dumps(board))
        (work / 'boot.json').write_text(json.dumps({'board': 'board.json', 'default_app': 'default.elf',
            'drivers': [{'manifest': 'manifest.json', 'instance_id': 27}]}))
        shutil.copyfile(source.with_name('manifest.json'), work / 'manifest.json')
        modes = []
        for sanitized in (False, True):
            mode = 'asan-ubsan' if sanitized else 'normal'
            san = ['-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                   '-fno-omit-frame-pointer', '-no-pie'] if sanitized else []
            unit = work / ('unit-' + mode)
            run(*shlex.split(os.environ.get('CC', 'cc')), '-std=c11', *flags, *san,
                *include_flags, test, '-o', unit)
            run(unit, env=environment)
            obj = work / ('provider-' + mode + '.o')
            run(*shlex.split(os.environ.get('CC', 'cc')), '-std=c11', *flags, *san,
                *include_flags, '-c', source, '-o', obj)
            native = work / ('native-' + mode)
            run(*shlex.split(os.environ.get('CXX', 'c++')), '-std=c++17', *flags, *san,
                '-O0', '-Wno-missing-field-initializers', *include_flags, '-rdynamic', *native_sources,
                '-x', 'c++', '-DSCOPED_I2C_RUNTIME_TEST', test, '-x', 'none', obj, '-ldl', '-o', native)
            run(native, work, env=environment)
            modes.append(mode)
        out = a.output.resolve() if a.output else work / 'target'
        out.mkdir(parents=True, exist_ok=True)
        elf = out / 'driver.elf'
        mapping = work / 'exports.map'
        mapping.write_text('{ global: t5_driver_get; local: *; };\n')
        run(a.cc, '-std=c11', '-Os', '-Wall', '-Wextra', '-Werror', '-fPIC',
            '-mtext-section-literals', '-mlongcalls', '-fvisibility=hidden', '-ffreestanding',
            '-fno-builtin', '-ffunction-sections', '-fdata-sections', '-nostdlib', '-nostartfiles',
            '-shared', '-Wl,--gc-sections', '-Wl,--no-relax', '-Wl,--hash-style=sysv',
            '-Wl,--version-script=' + str(mapping), '-I' + str(sdk), source, '-lgcc', '-o', elf)
        prefix = str(a.cc).removesuffix('gcc')
        disassembly = subprocess.check_output([prefix + 'objdump', '-d', str(elf)], text=True)
        symbols = subprocess.check_output([prefix + 'nm', '-D', str(elf)], text=True)
        if re.search(r'\bs32c1i\b|\bcas\w*\b|__atomic_|__sync_', disassembly + symbols, re.I):
            raise ValueError('Target provider contains compare-and-set/atomic instructions or imports')
        imports = {row.split()[-1] for row in symbols.splitlines() if ' U ' in ' ' + row}
        exports = {row.split()[-1] for row in symbols.splitlines() if ' T ' in ' ' + row}
        if not imports <= {'memcpy', 'memset', 'memcmp'} or exports != {'t5_driver_get'}:
            raise ValueError(f'Unexpected target ABI: imports={imports}, exports={exports}')
        (out / 'driver.disassembly.txt').write_text(disassembly)
        (out / 'driver.symbols.txt').write_text(symbols)
        receipt = {'schema': 1, 'host_modes': modes, 'real_runtime_port': True,
            'compiler': subprocess.check_output([a.cc, '--version'], text=True).splitlines()[0],
            'asan_options': environment['ASAN_OPTIONS'],
            'source_sha256': digest(source), 'elf_sha256': digest(elf), 'elf_bytes': elf.stat().st_size,
            'disassembly_sha256': hashlib.sha256(disassembly.encode()).hexdigest(),
            'imports': sorted(imports), 'exports': sorted(exports), 'inline_cas_or_atomic': False,
            'hardware_verified': False}
        (out / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
        print(json.dumps(receipt, indent=2))


if __name__ == '__main__':
    main()

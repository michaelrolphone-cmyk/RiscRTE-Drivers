#!/usr/bin/env python3
"""Focused app-linked Light helper qualification using canonical staged ABIs."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime', required=True, type=Path)
    parser.add_argument('--system', required=True, type=Path)
    parser.add_argument('--sanitize', choices=('address', 'undefined'))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    runtime, system = args.runtime.resolve(), args.system.resolve()
    with tempfile.TemporaryDirectory(prefix='portable-light-') as directory:
        out = Path(directory)
        sdk = out / 'sdk'
        sdk.mkdir()
        # One physical header per name avoids duplicate pragma-once types from
        # quoted adjacent includes. Preserve canonical current Runtime tables.
        for source in (system / 'lib/PortableApps/include',
                       root / 'sdk/driver', runtime / 'sdk/driver',
                       runtime / 'sdk/hardware', runtime / 'sdk/app'):
            for header in source.glob('*.h'):
                shutil.copyfile(header, sdk / header.name)
        for required in ('PortableForegroundIdle.h', 'RiscRuntimeSleepV1.h',
                         'RiscPowerKeyNeutralizeV1.h'):
            if not (sdk / required).is_file():
                raise SystemExit('Missing required staged header: ' + required)
        flags = ['-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                 '-Wno-misleading-indentation', '-I' + str(sdk),
                 '-I' + str(root / 'components/co5300/include')]
        if args.sanitize:
            flags += ['-fsanitize=' + args.sanitize, '-fno-sanitize-recover=all',
                      '-fno-omit-frame-pointer', '-no-pie']
        binary = out / 'portable-light-sleep'
        subprocess.run([os.environ.get('CC', 'cc'), *flags,
                        str(root / 'tests/test_portable_light_sleep.c'),
                        '-o', str(binary)], check=True)
        env = {**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0:halt_on_error=1',
               'UBSAN_OPTIONS': 'halt_on_error=1:print_stacktrace=1'}
        subprocess.run([str(binary)], check=True, env=env)
        # Also compile the real standalone translation unit, not only the
        # inclusion fixture. Verify no allocation or fixed sleep fallback link.
        obj = out / 'foreground_light_sleep.o'
        subprocess.run([os.environ.get('CC', 'cc'), *flags, '-c',
                        str(root / 'components/portable-light-sleep/foreground_light_sleep.c'),
                        '-o', str(obj)], check=True)
        symbols = {line.split()[-1] for line in
                   subprocess.check_output(['nm', '-u', str(obj)], text=True).splitlines()}
        for forbidden in ('malloc', 'calloc', 'realloc', 'free', 'deep_sleep',
                          'light_sleep_for', 'request_launch', 'request_default'):
            if forbidden in symbols:
                raise SystemExit('Unexpected dependency: ' + forbidden)
        print('Standalone helper compilation and allocation/fallback dependency audit PASS')


if __name__ == '__main__':
    main()

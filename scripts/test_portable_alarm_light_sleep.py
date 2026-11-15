#!/usr/bin/env python3
"""Opt-in timed alarm helper plus unchanged untimed baseline qualification."""
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
                        str(root / 'tests/test_portable_alarm_light_sleep.c'),
                        '-o', str(binary)], check=True)
        env = {**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0:halt_on_error=1',
               'UBSAN_OPTIONS': 'halt_on_error=1:print_stacktrace=1'}
        subprocess.run([str(binary)], check=True, env=env)
        # Also compile the real standalone translation unit, not only the
        # inclusion fixture. Verify no allocation or fixed sleep fallback link.
        obj = out / 'foreground_alarm_light_sleep.o'
        subprocess.run([os.environ.get('CC', 'cc'), *flags, '-DPORTABLE_FOREGROUND_ALARM_SLEEP', '-c',
                        str(root / 'components/portable-light-sleep/foreground_light_sleep.c'),
                        '-o', str(obj)], check=True)
        symbols = {line.split()[-1] for line in
                   subprocess.check_output(['nm', '-u', str(obj)], text=True).splitlines()}
        for forbidden in ('malloc', 'calloc', 'realloc', 'free', 'deep_sleep',
                          'light_sleep_for', 'request_launch', 'request_default'):
            if forbidden in symbols:
                raise SystemExit('Unexpected dependency: ' + forbidden)
        # Explicit omission preserves machine-code behavior, not just examples.
        old = out / 'baseline.c'
        old.write_text(subprocess.check_output(['git','-C',str(root),'show',
            'fc88ffd8d8329709bd30a38f27af43e83f29618e:components/portable-light-sleep/foreground_light_sleep.c'],text=True))
        images=[]
        for source,label in [(old,'baseline'),(root/'components/portable-light-sleep/foreground_light_sleep.c','candidate')]:
            obj=out/(label+'.o');raw=out/(label+'.text')
            subprocess.run([os.environ.get('CC','cc'),'-std=c11','-O2','-ffreestanding','-fno-builtin','-I'+str(sdk),'-c',str(source),'-o',str(obj)],check=True)
            subprocess.run(['objcopy','-O','binary','--only-section=.text',str(obj),str(raw)],check=True)
            images.append(raw.read_bytes())
        assert images[0]==images[1], 'Untimed helper machine code changed'
        print('Untimed helper .text byte-identical to sealed native daily source')
        print('Standalone helper compilation and allocation/fallback dependency audit PASS')


if __name__ == '__main__':
    main()

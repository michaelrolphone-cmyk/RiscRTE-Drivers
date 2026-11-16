#!/usr/bin/env python3
"""Compile the shared CO5300 profile contract as real C and C++, without an SDK.

These are profile-definition checks only. The real-SDK backend host and target
checks remain in test_co5300_native.py and are not replaced by this test.
"""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
DEFAULTS = dict(WIDTH=410, HEIGHT=502, OFFSET_X=22, OFFSET_Y=0,
                RESET_LOW_MS=200, RESET_RECOVERY_MS=200,
                PIXEL_WRITE_ADDRESS=0x003c00, RAMWR_BEFORE_PIXELS=1,
                SAFE_TOP=0, SAFE_RIGHT=0, SAFE_BOTTOM=0, SAFE_LEFT=0)
POSITIVE = {
    'default': {},
    'badge': dict(WIDTH=466, HEIGHT=466, OFFSET_X=6,
                  RESET_LOW_MS=10, RESET_RECOVERY_MS=150,
                  PIXEL_WRITE_ADDRESS=0x002c00, RAMWR_BEFORE_PIXELS=0,
                  SAFE_TOP=69, SAFE_RIGHT=69, SAFE_BOTTOM=69, SAFE_LEFT=69),
    'offset': dict(WIDTH=320, HEIGHT=240, OFFSET_X=4, OFFSET_Y=18,
                   RESET_LOW_MS=5, RESET_RECOVERY_MS=75),
    'minimum': dict(WIDTH=2, HEIGHT=2, OFFSET_X=0),
    'last-address': dict(WIDTH=2, HEIGHT=2, OFFSET_X=65534, OFFSET_Y=65534),
    'maximum-extent': dict(WIDTH=65534, HEIGHT=65534, OFFSET_X=2, OFFSET_Y=2),
    'timing-boundaries': dict(RESET_LOW_MS=1, RESET_RECOVERY_MS=500),
    'direct-memory-write': dict(PIXEL_WRITE_ADDRESS=0x002c00, RAMWR_BEFORE_PIXELS=0),
}
NEGATIVE = {}
for dimension in ('WIDTH', 'HEIGHT'):
    for value in (0, -2, 1, 467, 65536):
        NEGATIVE[f'{dimension}-{value}'] = {dimension: value}
for offset in ('OFFSET_X', 'OFFSET_Y'):
    for value in (-2, 1, 7, 65536):
        NEGATIVE[f'{offset}-{value}'] = {offset: value}
NEGATIVE['x-address-overflow'] = dict(WIDTH=466, OFFSET_X=65072)
NEGATIVE['y-address-overflow'] = dict(HEIGHT=466, OFFSET_Y=65072)
for timing in ('RESET_LOW_MS', 'RESET_RECOVERY_MS'):
    for value in (0, -1, 501, 65536):
        NEGATIVE[f'{timing}-{value}'] = {timing: value}
for value in (-1, 2):
    NEGATIVE[f'memory-policy-{value}'] = dict(RAMWR_BEFORE_PIXELS=value)
for value in (-1, 0x1000000, 0x003c01):
    NEGATIVE[f'memory-address-{value}'] = dict(PIXEL_WRITE_ADDRESS=value)
for inset in ('SAFE_TOP', 'SAFE_RIGHT', 'SAFE_BOTTOM', 'SAFE_LEFT'):
    for value in (-1, 65536):
        NEGATIVE[f'{inset}-{value}'] = {inset: value}
NEGATIVE['empty-safe-width'] = dict(SAFE_LEFT=200, SAFE_RIGHT=210)
NEGATIVE['empty-safe-height'] = dict(SAFE_TOP=250, SAFE_BOTTOM=252)

with tempfile.TemporaryDirectory(prefix='co5300-profile-') as directory:
    out = Path(directory)
    for language, compiler, standard, suffix, assertion in (
            ('C', os.environ.get('CC', 'cc'), 'c11', '.c', '_Static_assert'),
            ('C++', os.environ.get('CXX', 'c++'), 'c++17', '.cpp', 'static_assert')):
        source = out / ('profile' + suffix)
        for name, overrides in {**POSITIVE, **NEGATIVE}.items():
            expected = {**DEFAULTS, **overrides}
            header = (ROOT / 'boards/waveshare-esp32-s3-touch-amoled-1.75c/display-profile.h'
                      if name == 'badge' else 'RiscCo5300Profile.h')
            source.write_text(f'#include "{header}"\n' + ''.join(
                f'{assertion}(RISC_CO5300_{key} == {value}, "{key}");\n'
                for key, value in expected.items()) + 'int main(void) { return 0; }\n')
            command = [compiler, '-std=' + standard, '-Wall', '-Wextra', '-Werror',
                       '-I' + str(ROOT / 'components/co5300/include'), '-c', str(source), '-o', str(out / 'profile.o')]
            if name != 'badge':
                command += [f'-DRISC_CO5300_{key}={value}' for key, value in overrides.items()]
            result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            if name in POSITIVE:
                assert result.returncode == 0, f'{language} rejected {name}: {result.stdout}'
            else:
                assert result.returncode != 0 and 'CO5300 ' in result.stdout, (
                    f'{language} did not reject {name} with the profile diagnostic: {result.stdout}')
        print(f'{language} CO5300 profile: {len(POSITIVE)} valid configurations and '
              f'{len(NEGATIVE)} invalid geometry/reset/memory-write/inset configurations PASS')

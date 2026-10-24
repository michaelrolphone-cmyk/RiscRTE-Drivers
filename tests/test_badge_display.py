#!/usr/bin/env python3
"""Exercise the unchanged facade implementation under independent build profiles."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--watch', type=Path, required=True)
p.add_argument('--sanitize', action='store_true')
a = p.parse_args()
flags = ['-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-misleading-indentation', '-g']
if a.sanitize:
    flags += ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer', '-no-pie']
includes = ['-I' + str(x) for x in (ROOT / 'components/co5300/include', a.watch / 'sdk/driver', a.watch / 'sdk/app', a.watch / 'include')]
profiles = {
    'default': [],
    'badge': ['-include', str(ROOT / 'boards/waveshare-esp32-s3-touch-amoled-1.75c/display-profile.h'), '-DTEST_DISPLAY_RESET=1'],
    'offset': ['-DRISC_CO5300_WIDTH=320', '-DRISC_CO5300_HEIGHT=240', '-DRISC_CO5300_OFFSET_X=4',
               '-DRISC_CO5300_OFFSET_Y=18', '-DRISC_CO5300_SAFE_TOP=7', '-DRISC_CO5300_SAFE_RIGHT=8',
               '-DRISC_CO5300_SAFE_BOTTOM=9', '-DRISC_CO5300_SAFE_LEFT=10', '-DTEST_DISPLAY_RESET=3']
}
with tempfile.TemporaryDirectory() as tmp:
    for name, definitions in profiles.items():
        binary = Path(tmp) / name
        subprocess.run([os.environ.get('CC', 'cc'), *flags, *includes, *definitions,
                        str(ROOT / 'tests/test_vendor_panel.c'), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
        print(name + ': shared facade geometry/safe bounds, supply refusal and reset custody PASS', flush=True)

# The selected DCDC1 observer uses the same real ABI headers and host faults.
with tempfile.TemporaryDirectory() as tmp:
    binary = Path(tmp) / 'supply'
    subprocess.run([os.environ.get('CC', 'cc'), *flags, *includes,
                    str(ROOT / 'tests/test_badge_supply.c'), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
    print('DCDC1 observer: all codes/enables, claim/read/release failures PASS', flush=True)

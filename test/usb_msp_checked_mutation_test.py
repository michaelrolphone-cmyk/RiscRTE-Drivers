#!/usr/bin/env python3
"""Reject original lost-custody behavior and targeted receipt/phase mutations."""
import os
from pathlib import Path
import resource
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
flags = ['-std=c11', '-Wall', '-Wextra', '-Werror', '-I'+str(ROOT/'sdk/driver')]
if os.getenv('SANITIZE') == '1':
    flags += ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer', '-g']
cc = os.getenv('CC', 'cc')
msp = (ROOT/'Drivers/usb_msp/driver.c').read_text()
program = (ROOT/'Drivers/program_msp/driver.c').read_text()
original = subprocess.check_output(['git', '-C', str(ROOT), 'show', 'a7ae39994603c4545a3c014f09051ecfc12084d2:Drivers/usb_msp/driver.c'], text=True)
variants = {
    'baseline-lost-custody': (original, program),
    'swallowed-receipt': (msp.replace('if (released && !s->token)', 'if (released)'), program),
    'ignored-release-failure': (msp.replace('return host->release_checked(host->host.context, claim);', 'return (host->release_checked(host->host.context, claim), true);'), program),
    'repeat-target-close': (msp, program.replace('if (!s->closing) {\n        if (!sync_target', 'if (true) {\n        if (!sync_target')),
}
with tempfile.TemporaryDirectory() as tmp:
    build = Path(tmp)
    subprocess.run([cc, *flags, '-fPIC', '-fvisibility=hidden', '-shared', str(ROOT/'Drivers/usb_host_v2/driver.c'), '-o', str(build/'host.so')], check=True)
    subprocess.run([cc, *flags, str(ROOT/'test/drivers/usb_msp_checked_consumer_test.c'), '-ldl', '-o', str(build/'test')], check=True)
    for name, (ms, ps) in variants.items():
        assert ms != msp or ps != program
        for stem, source in [('msp',ms), ('program',ps)]:
            (build/(stem+'.c')).write_text(source)
            subprocess.run([cc, *flags, '-fPIC', '-fvisibility=hidden', '-shared', str(build/(stem+'.c')), '-o', str(build/(stem+'.so'))], check=True)
        r = subprocess.run([str(build/'test'), str(build/'host.so'), str(build/'msp.so'), str(build/'program.so')], text=True, capture_output=True)
        if r.returncode == 0:
            raise SystemExit('mutation escaped: '+name)
        # Reject at a test assertion, not an unrelated loader or sanitizer crash.
        if 'Assertion' not in r.stderr or 'failed' not in r.stderr:
            raise SystemExit(f'{name}: unexpected failure {r.returncode}: {r.stderr}')
        print(f'REJECTED {name}: {r.stderr.strip()}')

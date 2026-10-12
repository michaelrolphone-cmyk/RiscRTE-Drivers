#!/usr/bin/env python3
"""Exercise the actual NimBLE central with independent HCI/ATT peer fixtures."""
import argparse
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
CASES = ('normal', 'overflow', 'disconnect', 'timeout', 'subscribe-auth',
         'repeat', 'ffe0', 'nus', 'unknown', 'write-timeout')

def run(sanitize: bool) -> None:
    command = [sys.executable, str(ROOT/'scripts/build_ble_serial.py'), '--host']
    if sanitize:
        command.append('--sanitize')
    subprocess.run(command, check=True)
    env = dict(os.environ)
    if sanitize:
        library = subprocess.check_output([env.get('CC', 'cc'), '-print-file-name=libasan.so'], text=True).strip()
        env.update(LD_PRELOAD=library, ASAN_OPTIONS='detect_leaks=0',
                   UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    binary = ROOT/'build'/('ble-serial-san' if sanitize else 'ble-serial-host')/'driver.so'
    for case in CASES:
        subprocess.run([sys.executable, str(ROOT/'test/ble_serial_protocol_test.py'),
                        str(binary), case], env=env, check=True, timeout=45)
    subprocess.run([sys.executable, str(ROOT/'test/ble_serial_extended_indication_test.py'),
                    str(binary)], env=env, check=True, timeout=45)
    print(f'BLE serial: {len(CASES)+1} real-stack host cases PASS; sanitized={sanitize}')

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sanitize', action='store_true')
    run(parser.parse_args().sanitize)

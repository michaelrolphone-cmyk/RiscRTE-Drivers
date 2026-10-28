#!/usr/bin/env python3
"""Compile and exercise the real facade, including permanent custody fences."""
import os
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
def main():
    with tempfile.TemporaryDirectory(prefix='time-sample-facade-') as tmp:
        for sanitized in (False,True):
            binary=Path(tmp)/('sanitized' if sanitized else 'normal')
            flags=['-fsanitize=address,undefined','-g1'] if sanitized else []
            subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror',*flags,
                            '-I'+str(ROOT/'sdk/driver'),str(ROOT/'tests/test_time_sample.c'),'-o',str(binary)],check=True)
            env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0')
            for args in ([],['malformed'],['retained','step']):
                subprocess.run([str(binary),*args],check=True,env=env)
    print('PASS normal + ASan/UBSan facade; leak checking disabled under traced host')
if __name__=='__main__':main()

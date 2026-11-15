#!/usr/bin/env python3
"""Run source-owned measured GPIO preparation/fault cases, never hardware."""
import argparse,os,subprocess,tempfile
from pathlib import Path
from cst9217_sdk import stage_sdk
ROOT=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser();p.add_argument('--runtime',type=Path,required=True);a=p.parse_args()
 with tempfile.TemporaryDirectory(prefix='badge-gpio-owning-') as tmp:
  t=Path(tmp);sdk=stage_sdk(ROOT,a.runtime.resolve(),t/'sdk')
  for sanitized in [False,True]:
   flags=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-no-pie'] if sanitized else []
   binary=t/('sanitized' if sanitized else 'normal')
   subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',*flags,'-I'+str(ROOT/'components/co5300/include'),'-I'+str(sdk),str(ROOT/'tests/test_gpio_key_sleep.c'),'-o',str(binary)],check=True)
   subprocess.run([str(binary)],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'})
if __name__=='__main__':main()

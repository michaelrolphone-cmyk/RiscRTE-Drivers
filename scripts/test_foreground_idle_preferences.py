#!/usr/bin/env python3
"""Test generic cached foreground timeout against exact shared codec and SDK."""
import argparse,os,pathlib,shutil,subprocess,tempfile
p=argparse.ArgumentParser();p.add_argument('--runtime',required=True,type=pathlib.Path);p.add_argument('--system',required=True,type=pathlib.Path);a=p.parse_args();root=pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='foreground-preference-') as d:
 out=pathlib.Path(d);sdk=out/'sdk';shutil.copytree(a.system/'lib/PortableApps/include',sdk)
 for header in (a.runtime/'sdk/app').glob('*.h'):shutil.copyfile(header,sdk/header.name)
 for fallback in (5,15,3600):
  for sanitize in (False,True):
   flags=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-no-pie'] if sanitize else []
   binary=out/f'test-{fallback}-{sanitize}'
   subprocess.run(['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror',*flags,'-DPORTABLE_FOREGROUND_IDLE_FALLBACK_SECONDS='+str(fallback),'-I'+str(sdk),str(root/'tests/test_foreground_idle_preferences.c'),'-o',str(binary)],check=True)
   subprocess.run([str(binary)],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0','UBSAN_OPTIONS':'halt_on_error=1'})

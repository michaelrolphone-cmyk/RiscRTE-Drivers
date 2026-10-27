#!/usr/bin/env python3
"""Production-source battery telemetry fault tests; no hardware observations."""
import argparse,os,subprocess,tempfile
from pathlib import Path
from cst9217_sdk import stage_sdk
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--runtime',type=Path,required=True);p.add_argument('--sanitize',action='store_true');a=p.parse_args()
with tempfile.TemporaryDirectory() as tmp:
 t=Path(tmp);sdk=stage_sdk(ROOT,a.runtime.resolve(),t/'sdk')
 flags=['-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation']
 if a.sanitize:flags+=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-no-pie']
 subprocess.run(['cc',*flags,'-I'+str(sdk),'-I'+str(ROOT/'components/co5300/include'),str(ROOT/'tests/test_axp2101_observer.c'),'-o',str(t/'check')],check=True)
 subprocess.run([str(t/'check')],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0:halt_on_error=1','UBSAN_OPTIONS':'halt_on_error=1'})

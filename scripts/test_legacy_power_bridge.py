#!/usr/bin/env python3
import argparse,os,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--sdk',required=True,type=Path);p.add_argument('--sanitize',action='store_true');a=p.parse_args();r=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as td:
 for role in [1,2,3]:
  out=Path(td)/str(role);flags=['-fsanitize=address,undefined','-fno-omit-frame-pointer'] if a.sanitize else []
  subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror','-g',*flags,'-DLEGACY_POWER_ROLE='+str(role),'-I'+str(r/'sdk/driver'),'-I'+str(a.sdk),str(r/'tests/test_legacy_power_bridge.c'),'-o',str(out)],check=True)
  subprocess.run([str(out)],check=True)

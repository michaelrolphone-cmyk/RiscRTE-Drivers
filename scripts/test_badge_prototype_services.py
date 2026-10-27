#!/usr/bin/env python3
import argparse,os,pathlib,subprocess,tempfile
from cst9217_sdk import stage_sdk
root=pathlib.Path(__file__).resolve().parents[1];p=argparse.ArgumentParser();p.add_argument('--runtime',required=True,type=pathlib.Path);p.add_argument('--sanitize',action='store_true');a=p.parse_args()
with tempfile.TemporaryDirectory() as tmp:
 out=pathlib.Path(tmp);sdk=stage_sdk(root,a.runtime,out/'sdk')
 for name in ['axp2101_observer','axp2101_power_key','power_key_navigation','qmi8658_accel','gpio_key_sleep','wake_key_filter','qmi8658_idle']:
  san=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-no-pie'] if a.sanitize else []
  subprocess.run(['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',*san,'-I'+str(sdk),'-I'+str(a.runtime/'sdk/app'),'-I'+str(root/'components/co5300/include'),'-I'+str(root/'../controllers/include'),str(root/'tests'/('test_'+name+'.c')),'-o',str(out/name)],check=True)
  subprocess.run([str(out/name)],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0:halt_on_error=1','UBSAN_OPTIONS':'halt_on_error=1'})

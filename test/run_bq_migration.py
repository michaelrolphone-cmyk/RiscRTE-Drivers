#!/usr/bin/env python3
"""Actual source contract fixtures; no hardware or electrical qualification."""
import argparse,os,resource,shutil,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser();p.add_argument('--sanitize',action='store_true');p.add_argument('--target-cc');a=p.parse_args()
 resource.setrlimit(resource.RLIMIT_CORE,(0,0))
 flags=['-std=c11','-Wall','-Wextra','-Werror']+(['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-g','-no-pie'] if a.sanitize else [])
 sdk=ROOT/'sdk/driver';source=ROOT/'Drivers/bq25896';tests=ROOT/'test/drivers'
 def compile(parts):subprocess.run(['cc',*flags,*map(str,parts)],check=True)
 def run(parts):subprocess.run(list(map(str,parts)),check=True)
 with tempfile.TemporaryDirectory(prefix='bq-migration-') as tmp:
  t=Path(tmp);profile=t/'profile.o'
  compile(['-I'+str(sdk),'-Dt5_driver_get=t5_profile_get','-c',ROOT/'Drivers/t5s3_usb_power_profile/driver.c','-o',profile])
  compile(['-I'+str(sdk),'-DOLD_VIEW','-c',tests/'bq_vbus_prefix_test.c','-o',t/'old-prefix.o'])
  compile(['-I'+str(source),'-I'+str(sdk),tests/'bq_vbus_prefix_test.c',t/'old-prefix.o','-o',t/'prefix']);run([t/'prefix'])
  if a.target_cc:
   subprocess.run([a.target_cc,'-std=c11','-Wall','-Wextra','-Werror','-I'+str(source),'-I'+str(sdk),'-fsyntax-only',str(tests/'bq_vbus_prefix_test.c')],check=True)
  power=t/'power.o';compile(['-I'+str(sdk),'-c',source/'driver.c','-o',power])
  compile(['-I'+str(sdk),tests/'board_power_t5s3_v2_test.c',power,profile,'-o',t/'legacy']);run([t/'legacy'])
  compile(['-I'+str(source),'-I'+str(sdk),tests/'board_power_shutdown_test.c',power,profile,'-o',t/'shutdown'])
  run([t/'shutdown']);run([t/'shutdown','batfet-nack'])
  compile(['-I'+str(source),tests/'bq_charger_profile_test.c','-o',t/'charger']);run([t/'charger'])
  mutations=[('forget-uncertain-custody','driver.c','if (lease || source_requested || profile_uncertain || shutdown_pending)\n        return false;','if (lease || source_requested)\n        return false;','shutdown','!driver->quiesce()'),('no-readback-check','BqChargerProfile.h','(actual & field->mask) != (desired[i] & field->mask)','false','charger','!risc_bq_apply_charge_profile'),('nack-as-accepted','BqShutdownPolicy.h','RISC_BQ_SHUTDOWN_COMMAND_ACCEPTED : RISC_BQ_SHUTDOWN_BATFET_UNCERTAIN','RISC_BQ_SHUTDOWN_COMMAND_ACCEPTED : RISC_BQ_SHUTDOWN_COMMAND_ACCEPTED','shutdown-nack','power->request_shutdown')]
  for label,name,before,after,kind,expected in mutations:
   mutant=t/label;shutil.copytree(source,mutant);f=mutant/name;text=f.read_text();assert text.count(before)==1;f.write_text(text.replace(before,after))
   binary=t/(label+'-test')
   if kind=='charger':compile(['-I'+str(mutant),tests/'bq_charger_profile_test.c','-o',binary]);cmd=[str(binary)]
   else:
    compile(['-I'+str(sdk),'-c',mutant/'driver.c','-o',t/'mutant.o']);compile(['-I'+str(mutant),'-I'+str(sdk),tests/'board_power_shutdown_test.c',t/'mutant.o',profile,'-o',binary]);cmd=[str(binary)]+(['batfet-nack'] if kind=='shutdown-nack' else [])
   result=subprocess.run(cmd,capture_output=True,text=True)
   if result.returncode==0 or expected not in result.stderr:raise AssertionError((label,result.returncode,result.stdout,result.stderr))
   print('PASS negative',label,'rejected by',expected,flush=True)
 print('PASS BQ current source, old prefix consumers, charger/shutdown and three negative controls',flush=True)
if __name__=='__main__':main()

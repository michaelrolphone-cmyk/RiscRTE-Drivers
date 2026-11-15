#!/usr/bin/env python3
import os,subprocess,tempfile
from pathlib import Path
from badge_home_fixture_clock import home_watchdog_define
for bad in [[],['-DWATCH_DEFAULT_IDLE_SECONDS=0u'],['-DWATCH_DEFAULT_IDLE_SECONDS=5u']*2,['-DWATCH_DEFAULT_IDLE_SECONDS=4294968u']]:
 try:home_watchdog_define(bad)
 except AssertionError:pass
 else:raise AssertionError(bad)
with tempfile.TemporaryDirectory(prefix='badge-watchdog-') as d:
 p=Path(d)/'test.cpp';p.write_text('#include <assert.h>\n#include "badge_fixture_idle_policy.h"\nint main(){assert(badge_fixture_home_watchdog_ms()==EXPECTED);assert(badge_fixture_idle_fault_end_ms()>BADGE_FIXTURE_HOME_IDLE_MS);assert(badge_fixture_idle_fault_end_ms()<badge_fixture_home_watchdog_ms());}')
 for seconds in [5,15,30,60,3600,4294967]:
  for san in [False,True]:
   exe=Path(d)/'test';cmd=['c++','-std=c++17','-Wall','-Wextra','-Werror','-I'+str(Path(__file__).resolve().parents[1]/'tests'),home_watchdog_define(['-DWATCH_DEFAULT_IDLE_SECONDS='+str(seconds)+'u']),'-DEXPECTED='+str(30000+2*seconds*1000)+'ULL',str(p),'-o',str(exe)]
   if san:cmd+=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-no-pie']
   subprocess.run(cmd,check=True);subprocess.run([str(exe)],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'})
print('PASS six exact compile policies normal+ASan/UBSan; missing/duplicate/overflow rejected; no app linkage or runtime callback')

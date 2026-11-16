#!/usr/bin/env python3
"""Badge deployment fallback and saved-preference override, without target build."""
import argparse,ast,os,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--root',type=Path,default=Path(__file__).resolve().parents[1]);p.add_argument('--watch',type=Path,required=True);p.add_argument('--sdk',type=Path,required=True);a=p.parse_args()
s=(a.root/'scripts/build_badge_prototype.py').read_text();tree=ast.parse(s)
calls=[n for n in ast.walk(tree) if isinstance(n,ast.Call) and n.args and isinstance(n.args[0],ast.Constant) and n.args[0].value=='--idle-seconds'];assert len(calls)==1
assert next(k.value.value for k in calls[0].keywords if k.arg=='default')==30
assert s.count("'--default-idle-seconds',a.idle_seconds")==2
assert s.count("'--foreground-idle-fallback-seconds',a.idle_seconds")==2
code=r'''
#include <assert.h>
#include <string.h>
#include "apps/clock/WatchSleepDefaults.h"
static int result=RISC_KEY_VALUE_NOT_FOUND;static unsigned seconds=30;static int writes;
static int32_t get(void*c,const char*k,void*b,uint32_t cap,uint32_t*n){(void)c;assert(!strcmp(k,"sleep_idle")||!strcmp(k,"sleep_deep"));assert(cap==5);*n=5;unsigned char d[]={0x54,1,seconds&255,seconds>>8,(seconds^(seconds>>8)^0xa5)&255};memcpy(b,d,5);return result;}
static int32_t put(void*c,const char*k,const void*b,uint32_t n){(void)c;(void)k;(void)b;(void)n;++writes;return 0;}
int main(void){risc_key_value_v1 kv={.api_version=1,.struct_size=sizeof(kv),.get=get,.put=put};uint32_t ms;
assert(WATCH_DEFAULT_IDLE_MS==30000);assert(WATCH_DEFAULT_SLEEP_MODE==PORTABLE_SLEEP_HYBRID);
assert(watch_sleep_timer_load(&kv,0,&ms)==PORTABLE_SLEEP_MISSING&&ms==30000);
assert(watch_sleep_timer_load(&kv,1,&ms)==PORTABLE_SLEEP_MISSING&&ms==300000);
result=RISC_KEY_VALUE_IO;assert(watch_sleep_timer_load(&kv,0,&ms)==PORTABLE_SLEEP_UNAVAILABLE&&ms==30000);
result=RISC_KEY_VALUE_OK;unsigned saved[]={5,15,30,60,3600};for(unsigned i=0;i<5;i++){seconds=saved[i];assert(watch_sleep_timer_load(&kv,0,&ms)==PORTABLE_SLEEP_LOADED&&ms==seconds*1000);}
seconds=4;assert(watch_sleep_timer_load(&kv,0,&ms)==PORTABLE_SLEEP_INVALID&&ms==30000);assert(!writes);
}
'''
with tempfile.TemporaryDirectory(prefix='badge-idle-default-') as tmp:
 f=Path(tmp)/'test.c';f.write_text(code)
 for san in (False,True):
  exe=Path(tmp)/'test';subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-DWATCH_DEFAULT_IDLE_SECONDS=30u','-I'+str(a.watch),'-I'+str(a.sdk),*(['-fsanitize=address,undefined','-no-pie'] if san else []),str(f),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'})
print('PASS Badge30-second selection; all consumers; valid saved5/15/30/60/3600 override; missing/invalid/unavailable fallback; Deep300s unchanged; no writes; normal+sanitized')

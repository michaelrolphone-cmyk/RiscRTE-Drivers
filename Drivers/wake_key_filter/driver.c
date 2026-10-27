/* Consume the press that woke/started this invocation until physical release.
 * Later presses keep the underlying provider's ordinary short/long semantics. */
#include "DriverSupport.h"
#include "RiscPowerKeyNeutralizeV1.h"
#include "RiscPowerSleepV1.h"
static const risc_power_key_api_v1 *key;
static const risc_power_sleep_api_v1 *wake;
enum { READY, WAIT_RELEASE, WAIT_FRESH_PRESS };
static unsigned gate;
static bool events(void *context,uint32_t *out){
 (void)context;if(!out)return false;*out=0;if(!key||!wake)return false;
 uint32_t value=0;if(!key->events(key->context,&value))return false;
 if(gate!=READY){bool pending=true;if(!wake->sleep_wake_pending(wake->context,&pending))return false;
  if(gate==WAIT_RELEASE&&!pending)gate=WAIT_FRESH_PRESS;
  else if(gate==WAIT_FRESH_PRESS&&pending)gate=READY;
  /* Drain this frame too: an old PMIC latch may arrive after the release
   * observation. Only an observed new press re-arms later events. */
  return true;}
 *out=value;return true;
}
static bool neutralize(void *context){
 (void)context;if(!key||!wake)return false;
 /* The same provider remains mapped during Light sleep. Re-arm independently
  * of its startup path; even a currently released key can have a late latch. */
 gate=WAIT_RELEASE;return true;
}
static bool quiesce(void){return true;}
static void stop(void){key=NULL;wake=NULL;gate=READY;}
static bool start(const risc_provider_dependency_v1 *deps,size_t count){
 if(key||wake)return false;
 const risc_power_key_api_v1 *k=tw_dep(deps,count,"input.power-key.raw",sizeof(*k));
 const risc_power_sleep_api_v1 *w=tw_dep(deps,count,RISC_POWER_SLEEP_CAPABILITY,RISC_POWER_TIMED_SLEEP_SIZE);
 if(!k||!k->events||!w||!w->sleep_wake_pending)return false;
 bool pending=true;if(!w->sleep_wake_pending(w->context,&pending))return false;
 key=k;wake=w;gate=pending?WAIT_RELEASE:READY;return true;
}
static const risc_power_key_neutralize_v1 api={{1,sizeof(api),NULL,events},neutralize};
static const risc_driver_v2 driver={2,sizeof(driver),"wake-key-filter",RISC_POWER_KEY_CAPABILITY,1,&api.base,start,stop,quiesce};
__attribute__((visibility("default")))const risc_driver_v2*t5_driver_get(uint32_t abi){return abi==2?&driver:NULL;}

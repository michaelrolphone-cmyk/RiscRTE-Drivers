#include <assert.h>
#include <stdio.h>
#include "RiscPowerKeyNeutralizeV1.h"
#include "../Drivers/wake_key_filter/driver.c"
static bool held,read_ok=true,event_ok=true;
static uint32_t queued;
static bool event_cb(void*c,uint32_t*out){(void)c;*out=queued;queued=0;return event_ok;}
static bool pending_cb(void*c,bool*out){(void)c;*out=held;return read_ok;}
static const risc_power_key_api_v1 raw={1,sizeof(raw),NULL,event_cb};
static const risc_power_sleep_api_v1 sleep_api={.api_version=1,.struct_size=sizeof(sleep_api),.sleep_wake_pending=pending_cb};
static const risc_provider_dependency_v1 deps[]={{"input.power-key.raw",1,&raw},{"power.sleep",1,&sleep_api}};
int main(void){for(unsigned i=0;i<1000;i++){stop();held=true;assert(start(deps,2));uint32_t out=99;
 queued=3;assert(events(NULL,&out)&&out==0);queued=2;held=false;assert(events(NULL,&out)&&out==0);
 queued=2;assert(events(NULL,&out)&&out==0);held=true;queued=2;assert(events(NULL,&out)&&out==0);
 queued=2;held=false;assert(events(NULL,&out)&&out==2);queued=1;assert(events(NULL,&out)&&out==1);
 event_ok=false;out=99;assert(!events(NULL,&out)&&out==0);event_ok=true;assert(quiesce());}
 stop();held=false;assert(start(deps,2));uint32_t out=99;queued=2;assert(events(NULL,&out)&&out==2);
 stop();read_ok=false;assert(!start(deps,2));read_ok=true;held=true;assert(start(deps,2));read_ok=false;assert(!events(NULL,&out)&&!out);read_ok=true;stop();
 const risc_power_key_neutralize_v1 *n=t5_driver_get(2)->capability;
 assert(n->base.struct_size>=RISC_POWER_KEY_NEUTRALIZE_SIZE&&n->neutralize);
 assert(!n->neutralize(n->base.context));held=false;assert(start(deps,2));
 for(unsigned i=0;i<1000;i++){
  assert(n->neutralize(n->base.context));held=true;queued=3;assert(events(NULL,&out)&&!out);
  held=false;queued=2;assert(events(NULL,&out)&&!out);
  for(unsigned late=0;late<3;late++){queued=2;assert(events(NULL,&out)&&!out);}
  held=true;queued=2;assert(events(NULL,&out)&&!out);
  held=false;queued=2;assert(events(NULL,&out)&&out==2);
  /* Already released at resume still suppresses late wake-short. */
  assert(n->neutralize(n->base.context));queued=2;assert(events(NULL,&out)&&!out);
  queued=2;assert(events(NULL,&out)&&!out);held=true;assert(events(NULL,&out)&&!out);
  held=false;queued=1;assert(events(NULL,&out)&&out==1);
 }
 stop();assert(!n->neutralize(n->base.context));
 puts("Wake-key neutralize:1000 repeated live-Light held/released/late-latch cycles PASS");
 puts("Wake-key filter:1000 held-start/release/new-press cycles; missing/read failure and cold-start behavior PASS");}

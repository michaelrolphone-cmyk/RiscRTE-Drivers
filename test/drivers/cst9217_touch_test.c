#include "RiscTouchV1.h"
#include "RiscI2cBusV1.h"
#include "RiscPlatformClockV1.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint64_t clock_ms=100,token,serial;
static unsigned transfers,fail_transfer,release_fail,release_calls;
static bool false_claim,reenter;
static uint64_t invalid_clock_at=UINT64_MAX;
static const risc_touch_api_v1 *active_api;
static uint16_t reg,width=466,height=466;
static uint64_t selected_at;
static uint8_t raw[10];
static bool claim(void*c,uint8_t address,uint64_t*out){(void)c;assert(address==0x5a);if(token)return false;*out=token=++serial;return !false_claim;}
static bool release(void*c,uint64_t t){(void)c;assert(t==token);release_calls++;if(release_fail)return false;token=0;return true;}
static uint64_t now(void*c){(void)c;return clock_ms>=invalid_clock_at?UINT64_MAX:clock_ms;}
static void sleep_ms(void*c,uint32_t ms){(void)c;assert(ms==2||ms==10);clock_ms+=ms;if(reenter){reenter=false;risc_touch_snapshot_v1 s;assert(!active_api->snapshot(NULL,&s));}}
static bool transact(void*c,uint64_t t,const uint8_t*w,size_t wn,uint8_t*r,size_t rn,uint32_t timeout){
 (void)c;assert(t==token&&timeout==20);assert(!(wn&&rn)); /* STOP-separated only */
 transfers++;if(transfers==fail_transfer)return false;
 if(wn){assert(w&&wn==2&&!r);reg=(uint16_t)((w[0]<<8)|w[1]);selected_at=clock_ms;return true;}
 assert(!w&&r&&clock_ms-selected_at>=2);
 if(reg==0xd1f8){assert(rn==4);r[0]=width;r[1]=width>>8;r[2]=height;r[3]=height>>8;return true;}
 if(reg==0xd1fc||reg==0xd204){assert(rn==4);memset(r,0,rn);return true;}
 assert(reg==0xd000&&rn==10);memcpy(r,raw,10);return true;
}
static risc_i2c_bus_api_v1 bus={1,sizeof(bus),NULL,claim,transact,release};
static risc_platform_clock_api_v1 clock_api={1,sizeof(clock_api),NULL,now,sleep_ms};
static risc_provider_dependency_v1 deps[]={
 {"i2c.bus",1,&bus},{"platform.clock",1,&clock_api}
};
extern const risc_driver_v2 *t5_driver_get(uint32_t);
static void report(unsigned x,unsigned y,bool down){memset(raw,0,10);raw[6]=0xab;raw[5]=down;raw[0]=down?6:0;raw[1]=x>>4;raw[2]=y>>4;raw[3]=(x&15)<<4|(y&15);}
static risc_touch_event_v1 event(const risc_touch_api_v1*a,uint64_t s,unsigned kind){risc_touch_event_v1 e;assert(a->next(NULL,s,&e)==1&&e.kind==kind);return e;}
int main(void){
 const risc_driver_v2*d=t5_driver_get(2);assert(d&&!t5_driver_get(1));
 const risc_touch_api_v1*a=d->capability;active_api=a;assert(a&&!a->subscribe(NULL));
 report(0,0,false);
 assert(!d->start(NULL,0));
 clock_api.sleep_ms=NULL;assert(!d->start(deps,2));clock_api.sleep_ms=sleep_ms;
 for(unsigned failure=1;failure<=8;failure++){
  transfers=0;fail_transfer=failure;assert(!d->start(deps,2));assert(!token);d->stop();
 }
 fail_transfer=0;transfers=0;width=0;assert(!d->start(deps,2)&&!token);width=466;
 /* A clock failing only after probe must not expose a valid sentinel snapshot. */
 for(unsigned retained=0;retained<2;retained++){
  invalid_clock_at=clock_ms+18;release_fail=retained;
  assert(!d->start(deps,2));risc_touch_snapshot_v1 invalid;
  assert(!a->snapshot(NULL,&invalid)&&!a->subscribe(NULL));
  if(retained){assert(token&&!d->quiesce());d->stop();assert(token);}
  else assert(!token);
  release_fail=0;assert(d->quiesce());d->stop();assert(!token);
  invalid_clock_at=UINT64_MAX;
 }
 false_claim=true;release_fail=1;assert(!d->start(deps,2)&&token);assert(!a->subscribe(NULL));
 assert(!d->start(deps,2));assert(!d->quiesce());d->stop();assert(token);
 release_fail=0;assert(d->quiesce()&&!token);d->stop();false_claim=false;
 transfers=0;assert(d->start(deps,2));assert(!d->start(deps,2));
 uint64_t s=a->subscribe(NULL),second=a->subscribe(NULL);assert(s&&second&&s!=second);
 assert(!d->quiesce());d->stop();assert(token);
 risc_touch_snapshot_v1 snap;assert(a->snapshot(NULL,&snap)&&snap.width==466&&snap.height==466&&!snap.contact_count);
 assert(!a->poll(NULL,0)&&!a->poll(NULL,17));
 report(465,465,true);reenter=true;assert(a->poll(NULL,1));risc_touch_event_v1 e=event(a,s,RISC_TOUCH_EVENT_DOWN);assert(e.x==465&&e.y==465);
 event(a,second,RISC_TOUCH_EVENT_DOWN);assert(a->snapshot(NULL,&snap)&&snap.contact_count==1&&snap.contacts[0].x==465);
 assert(a->poll(NULL,16));assert(a->next(NULL,s,&e)==0);
 report(123,234,true);assert(a->poll(NULL,1));e=event(a,s,RISC_TOUCH_EVENT_MOVE);assert(e.x==123&&e.y==234);
 report(0,0,false);assert(a->poll(NULL,1));e=event(a,s,RISC_TOUCH_EVENT_UP);assert(e.x==123&&e.y==234);
 /* Corrupt ACK, count and out-of-range coordinates never become edge taps. */
 for(unsigned bad=0;bad<4;bad++){
  report(20,30,true);if(bad==0)raw[6]=0;if(bad==1)raw[5]=2;if(bad==2)report(466,30,true);if(bad==3)fail_transfer=transfers+2;
  assert(!a->poll(NULL,1));assert(!a->snapshot(NULL,&snap));assert(a->next(NULL,s,&e)==-1);fail_transfer=0;
  report(20,30,true);assert(a->poll(NULL,1));while(a->next(NULL,s,&e)>0){}
 }
 for(unsigned n=0;n<RISC_TOUCH_QUEUE_LENGTH+4;n++){report(n,20,true);assert(a->poll(NULL,1));}
 assert(a->next(NULL,s,&e)==-1);assert(a->snapshot(NULL,&snap)&&snap.contact_count==1);
 assert(a->unsubscribe(NULL,s)&&a->unsubscribe(NULL,second));
 release_fail=1;assert(!d->quiesce());d->stop();assert(token);
 release_fail=0;assert(d->quiesce());d->stop();assert(!token&&!a->snapshot(NULL,&snap));
 assert(d->start(deps,2));uint64_t fresh=a->subscribe(NULL);assert(fresh>second);assert(!a->unsubscribe(NULL,s));
 assert(a->unsubscribe(NULL,fresh));assert(d->quiesce());d->stop();assert(!token&&release_calls);
 puts("CST9217: STOP/delay protocol, packed coordinates, edges, fanout/gaps, bounds, fault and retained-custody lifecycle PASS");
}

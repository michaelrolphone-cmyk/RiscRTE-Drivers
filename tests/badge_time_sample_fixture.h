#pragma once
#include <cassert>
/* Explicit disconnected native boundary for unchanged product scenarios.
 * Selected synchronization success/fault cases qualify separately. This must
 * not manufacture UTC or mutate the clock when old cases refuse association. */
#ifdef BADGE_TIME_SERVER_SYNC
static struct {
 bool success=false,network=false,active=false;
 unsigned opens=0,steps=0,closes=0,closed=0;
 uint64_t (*now)()=nullptr;
 bool (*allowed)()=nullptr;
} badgeTimeSample;
static void badgeTimeSampleHardware(RiscCpu::Hardware& h){
 badgeTimeSample.now=h.now;
 static const risc_time_sample_api_v1 api={1,sizeof(api),nullptr,
  [](void*,const risc_time_sample_request_v1*request,uint64_t*out)->int32_t{
   if(badgeTimeSample.allowed)assert(badgeTimeSample.allowed());
   if(!badgeTimeSample.success)return RISC_TIME_SAMPLE_NETWORK;
   assert(request&&request->struct_size>=sizeof(*request)&&request->server&&out);
   assert(badgeTimeSample.network&&!badgeTimeSample.active);
   ++badgeTimeSample.opens;badgeTimeSample.active=true;*out=91;return RISC_TIME_SAMPLE_OK;
  },
  [](void*,uint64_t token,risc_time_sample_v1*out)->int32_t{
   if(badgeTimeSample.allowed)assert(badgeTimeSample.allowed());
   if(!badgeTimeSample.success)return RISC_TIME_SAMPLE_CLOSED;
   assert(token==91&&badgeTimeSample.active&&out&&out->struct_size>=sizeof(*out));
   if(++badgeTimeSample.steps<3)return RISC_TIME_SAMPLE_PENDING;
   *out={sizeof(*out),RISC_TIME_SAMPLE_UNAUTHENTICATED,1793000000,badgeTimeSample.now(),1000000000,0};
   return RISC_TIME_SAMPLE_OK;
  },
  [](void*,uint64_t token)->int32_t{
   if(badgeTimeSample.allowed)assert(badgeTimeSample.allowed());
   if(!badgeTimeSample.success)return RISC_TIME_SAMPLE_CLOSED;
   assert(token==91&&badgeTimeSample.active);
   if(++badgeTimeSample.closes<3)return RISC_TIME_SAMPLE_PENDING;
   badgeTimeSample.active=false;++badgeTimeSample.closed;return RISC_TIME_SAMPLE_OK;
  }};
 h.timeSample=&api;h.timeSampleIdle=[](){return !badgeTimeSample.active;};h.timeSampleSafe=[](){return true;};
}
#endif

#include <assert.h>
#include <stdio.h>
#include "../Drivers/time_sample/driver.c"
static unsigned opens,steps,closes;
static int32_t open_result,step_result,close_result;
static uint64_t next_token=7;
static int32_t mock_open(void*c,const risc_time_sample_request_v1*r,uint64_t*out){
 (void)c;(void)r;++opens;if(open_result==RISC_TIME_SAMPLE_OK)*out=next_token;return open_result;
}
static int32_t mock_step(void*c,uint64_t id,risc_time_sample_v1*out){
 (void)c;assert(id==next_token);++steps;
 if(step_result==RISC_TIME_SAMPLE_OK)out->utc_seconds=1800000000;
 return step_result;
}
static int32_t mock_close(void*c,uint64_t id){(void)c;assert(id==next_token);++closes;return close_result;}
int main(int argc,char **argv){
 (void)argv;
 risc_time_sample_api_v1 backend={1,sizeof(backend),NULL,mock_open,mock_step,mock_close};
 risc_provider_dependency_v1 dep={RISC_TIME_SAMPLE_CAPABILITY,1,&backend};
 const risc_driver_v2*d=t5_driver_get(2);assert(d&&!t5_driver_get(1));
 assert(!d->start(NULL,1)&&!d->start(&dep,0));
 backend.struct_size=0;assert(!d->start(&dep,1));backend.struct_size=sizeof(backend);
 backend.close=NULL;assert(!d->start(&dep,1));backend.close=mock_close;
 assert(d->start(&dep,1)&&!d->start(&dep,1));
 const risc_time_sample_api_v1*a=d->capability;
 uint64_t out=99;
 assert(a->open(NULL,NULL,NULL)==RISC_TIME_SAMPLE_INVALID&&opens==0);
 open_result=RISC_TIME_SAMPLE_NETWORK;
 assert(a->open(NULL,NULL,&out)==open_result&&out==99&&d->quiesce());
 open_result=RISC_TIME_SAMPLE_OK;
 assert(a->open(NULL,NULL,&out)==0&&out==7);
 assert(a->open(NULL,NULL,&out)==RISC_TIME_SAMPLE_BUSY&&opens==2);
 risc_time_sample_v1 sample={.struct_size=sizeof(sample),.utc_seconds=123};
 assert(a->step(NULL,8,&sample)==RISC_TIME_SAMPLE_INVALID&&steps==0);
 step_result=RISC_TIME_SAMPLE_PENDING;
 assert(a->step(NULL,7,&sample)==step_result&&sample.utc_seconds==123);
 step_result=0;assert(a->step(NULL,7,&sample)==0&&sample.utc_seconds==1800000000);
 assert(a->close(NULL,8)==RISC_TIME_SAMPLE_INVALID&&closes==0);
 close_result=RISC_TIME_SAMPLE_PENDING;
 assert(a->close(NULL,7)==RISC_TIME_SAMPLE_PENDING&&token==7);
 closes=0;
 close_result=RISC_TIME_SAMPLE_CONTEXT;
 assert(!d->quiesce()&&closes==1);d->stop();assert(native&&closes==2);
 close_result=0;assert(a->close(NULL,7)==0&&closes==3);
 assert(a->close(NULL,7)==0&&closes==3&&d->quiesce());
 d->stop();assert(!native&&a->open(NULL,NULL,&out)==RISC_TIME_SAMPLE_CLOSED);
 assert(d->start(&dep,1));
 if(argc==2){
  next_token=0;out=99;
  assert(a->open(NULL,NULL,&out)==RISC_TIME_SAMPLE_RETAINED&&out==99);
  assert(!d->quiesce());d->stop();assert(native);
  puts("time sampler malformed-open fencing PASS");return 0;
 }
 assert(a->open(NULL,NULL,&out)==0);
 if(argc==3){
  step_result=RISC_TIME_SAMPLE_RETAINED;
  assert(a->step(NULL,7,&sample)==RISC_TIME_SAMPLE_RETAINED);
  unsigned before=closes;
  assert(!d->quiesce());d->stop();
  assert(a->close(NULL,7)==RISC_TIME_SAMPLE_RETAINED&&closes==before);
  puts("time sampler retained-step fencing PASS");return 0;
 }
 close_result=RISC_TIME_SAMPLE_RETAINED;
 assert(a->close(NULL,7)==RISC_TIME_SAMPLE_RETAINED);unsigned before=closes;
 assert(!d->quiesce());d->stop();
 assert(a->close(NULL,7)==RISC_TIME_SAMPLE_RETAINED&&closes==before);
 assert(a->step(NULL,7,&sample)==RISC_TIME_SAMPLE_RETAINED);
 assert(a->open(NULL,NULL,&out)==RISC_TIME_SAMPLE_RETAINED);
 assert(!d->start(&dep,1)&&native);
 puts("time sampler facade lifecycle PASS");return 0;
}

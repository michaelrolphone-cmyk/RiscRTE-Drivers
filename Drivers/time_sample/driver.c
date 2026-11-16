/* Capability facade: sampling is read-only; clock mutation remains a separate
 * authorized application transaction. Never release uncertain native custody. */
#include "RiscProviderV2.h"
#include "RiscTimeSampleV1.h"
#include <string.h>
static const risc_time_sample_api_v1 *native;
static uint64_t token, last_closed;
static bool retained;
static int32_t open_sample(void *context,const risc_time_sample_request_v1 *request,uint64_t *out){
 (void)context;
 if(retained)return RISC_TIME_SAMPLE_RETAINED;
 if(!native)return RISC_TIME_SAMPLE_CLOSED;
 if(!out)return RISC_TIME_SAMPLE_INVALID;
 if(token)return RISC_TIME_SAMPLE_BUSY;
 uint64_t opened=0;
 int32_t rc=native->open(native->context,request,&opened);
 if(rc==RISC_TIME_SAMPLE_RETAINED || ((rc==RISC_TIME_SAMPLE_OK)!=(opened!=0))){
  token=opened;retained=true;return RISC_TIME_SAMPLE_RETAINED;
 }
 if(rc==RISC_TIME_SAMPLE_OK){token=opened;last_closed=0;*out=opened;}
 return rc;
}
static int32_t step_sample(void *context,uint64_t id,risc_time_sample_v1 *out){
 (void)context;
 if(retained)return RISC_TIME_SAMPLE_RETAINED;
 if(!native)return RISC_TIME_SAMPLE_CLOSED;
 if(!id||id!=token)return RISC_TIME_SAMPLE_INVALID;
 int32_t rc=native->step(native->context,id,out);
 if(rc==RISC_TIME_SAMPLE_RETAINED)retained=true;
 return rc;
}
static int32_t close_sample(void *context,uint64_t id){
 (void)context;
 if(retained)return RISC_TIME_SAMPLE_RETAINED;
 if(!native)return RISC_TIME_SAMPLE_CLOSED;
 if(!token&&id&&id==last_closed)return RISC_TIME_SAMPLE_OK;
 if(!id||id!=token)return RISC_TIME_SAMPLE_INVALID;
 int32_t rc=native->close(native->context,id);
 if(rc==RISC_TIME_SAMPLE_OK){last_closed=token;token=0;}
 else if(rc==RISC_TIME_SAMPLE_RETAINED)retained=true;
 return rc;
}
static bool start(const risc_provider_dependency_v1 *deps,size_t count){
 if(native||retained||!deps||count!=1||!deps[0].capability_id||
    strcmp(deps[0].capability_id,RISC_TIME_SAMPLE_CAPABILITY)||
    deps[0].api_version!=RISC_TIME_SAMPLE_API_V1||!deps[0].api)return false;
 const risc_time_sample_api_v1 *api=deps[0].api;
 if(api->api_version!=RISC_TIME_SAMPLE_API_V1||api->struct_size<sizeof(*api)||
    !api->open||!api->step||!api->close)return false;
 native=api;token=last_closed=0;return true;
}
static bool quiesce(void){
 if(retained)return false;
 return !token||close_sample(NULL,token)==RISC_TIME_SAMPLE_OK;
}
static void stop(void){if(quiesce()){native=NULL;last_closed=0;}}
static const risc_time_sample_api_v1 api={1,sizeof(api),NULL,open_sample,step_sample,close_sample};
static const risc_driver_v2 driver={2,sizeof(driver),"time-sample","net.time-sample",1,&api,start,stop,quiesce};
__attribute__((visibility("default"))) const risc_driver_v2 *t5_driver_get(uint32_t abi){return abi==2?&driver:NULL;}

/* App-local, read-only use of the shared confirmed idle preference. The KV
 * capability itself is not read-only. No fallback overwrites saved bytes. */
#include "PortableForegroundIdle.h"
#include "PortableSleepPolicy.h"
#include <limits.h>
#ifndef PORTABLE_FOREGROUND_IDLE_FALLBACK_SECONDS
#error Select an explicit foreground inactivity fallback
#endif
#if PORTABLE_FOREGROUND_IDLE_FALLBACK_SECONDS < 5 || PORTABLE_FOREGROUND_IDLE_FALLBACK_SECONDS > 3600
#error Foreground idle fallback must be 5..3600 seconds
#endif
static uint32_t preference_timeout;
static bool preference_retained;
typedef struct {const risc_key_value_v1 *api;int32_t result;} idle_read_observation;
static int32_t idle_observed_get(void *context,const char *key,void *data,uint32_t capacity,uint32_t *size){
 idle_read_observation *o=context;
 o->result=o->api->get(o->api->context,key,data,capacity,size);return o->result;
}
static int32_t idle_never_put(void *context,const char *key,const void *data,uint32_t size){
 (void)context;(void)key;(void)data;(void)size;return RISC_KEY_VALUE_INVALID;
}
static int idle_preference_retain(const risc_runtime_api_v1 *rt){
 preference_timeout=0;preference_retained=true;
 if(!rt->retain_invocation())for(;;){}
 return -2;
}
uint32_t portable_foreground_idle_timeout_ms(void){return preference_retained?0:preference_timeout;}
void portable_foreground_idle_timeout_changed(uint32_t milliseconds){
 if(preference_retained)return;
 preference_timeout=milliseconds>=5000u&&milliseconds<=3600000u&&milliseconds%1000u==0?milliseconds:0;
}
int portable_foreground_idle_configure(const risc_runtime_api_v1 *rt){
 preference_timeout=0;if(preference_retained)return -2;
 if(!rt||rt->api_version!=RISC_RUNTIME_API_V1||rt->struct_size<RISC_RUNTIME_RETAIN_INVOCATION_V1_SIZE||
    !rt->retain_invocation||!rt->acquire||!rt->release)return -1;
 risc_runtime_capability_v1 grant={.struct_size=sizeof(grant)};
 const bool acquired=rt->acquire(RISC_KEY_VALUE_CAPABILITY,1,PORTABLE_SLEEP_STORE_INSTANCE,&grant);
 if(!acquired){
  /* Canonical Runtime KV acquisition creates no external provider. Its exit
   * barrier revokes the getter on retained custody; empty + still-active is
   * therefore a clean unavailable namespace, not an invented timer default. */
  if(grant.struct_size==sizeof(grant)&&!grant.api&&!grant.slot&&!grant.generation&&
     risc_runtime_get_api(1))return -1;
  return idle_preference_retain(rt);
 }
 if(grant.struct_size!=sizeof(grant)||!grant.api||!grant.slot||!grant.generation)return idle_preference_retain(rt);
 const risc_key_value_v1 *kv=grant.api;uint32_t timeout=0;int status=PORTABLE_SLEEP_UNAVAILABLE;
 idle_read_observation observation={kv,INT32_MIN};
 if(kv->api_version==RISC_KEY_VALUE_API_V1&&kv->struct_size>=sizeof(*kv)&&kv->get){
  /* Reuse the shared codec unchanged, while preserving the raw custody result
   * that its convenience status deliberately abstracts away. */
  risc_key_value_v1 observed=*kv;observed.context=&observation;observed.get=idle_observed_get;observed.put=idle_never_put;
  status=portable_sleep_timer_load(&observed,false,&timeout);
  switch(observation.result){
   case RISC_KEY_VALUE_OK:case RISC_KEY_VALUE_NOT_FOUND:case RISC_KEY_VALUE_BUFFER_SMALL:
   case RISC_KEY_VALUE_INVALID:case RISC_KEY_VALUE_IO:case RISC_KEY_VALUE_BUSY:break;
   default:return idle_preference_retain(rt);
  }
 }
 if(!rt->release(&grant)||grant.struct_size!=sizeof(grant)||grant.api||grant.slot||grant.generation)return idle_preference_retain(rt);
 if(observation.result==RISC_KEY_VALUE_BUSY||observation.result==RISC_KEY_VALUE_IO)return -3;
 if(status==PORTABLE_SLEEP_MISSING)timeout=PORTABLE_FOREGROUND_IDLE_FALLBACK_SECONDS*1000u;
 else if(status!=PORTABLE_SLEEP_LOADED)return -1;
 portable_foreground_idle_timeout_changed(timeout);return preference_timeout?0:-1;
}

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../components/portable-light-sleep/foreground_idle_preferences.c"
static unsigned acquisitions,reads,writes,releases,retains;
static bool acquire_ok=true,release_ok=true,release_clear=true,owner_alive=true,partial_acquire=false;
static int32_t read_result=RISC_KEY_VALUE_NOT_FOUND;
static uint8_t value[5];static uint32_t length=5;
static int32_t get(void *context,const char *key,void *buffer,uint32_t capacity,uint32_t *size){
 (void)context;assert(!retains);++reads;assert(!strcmp(key,"sleep_idle")&&capacity==5);*size=0;
 if(read_result==RISC_KEY_VALUE_OK){if(length>capacity){*size=length;return RISC_KEY_VALUE_BUFFER_SMALL;}memcpy(buffer,value,length);*size=length;}
 return read_result;
}
static int32_t put(void*c,const char*k,const void*d,uint32_t n){(void)c;(void)k;(void)d;(void)n;++writes;assert(false);return -1;}
static risc_key_value_v1 kv={1,sizeof(kv),NULL,get,put};
static bool acquire(const char *id,uint32_t api,uint64_t instance,risc_runtime_capability_v1 *g){
 assert(!retains);++acquisitions;assert(!strcmp(id,"storage.key-value")&&api==1&&instance==1);
 if(!acquire_ok){if(partial_acquire)g->slot=1;return false;}
 *g=(risc_runtime_capability_v1){.struct_size=sizeof(*g),.api=&kv,.slot=1,.generation=2};return true;
}
static bool release(risc_runtime_capability_v1 *g){assert(!retains);++releases;if(!release_ok)return false;if(release_clear)*g=(risc_runtime_capability_v1){.struct_size=sizeof(*g)};return true;}
static bool retain(void){++retains;return true;}
static const risc_runtime_api_v1 runtime={.api_version=1,.struct_size=sizeof(runtime),.acquire=acquire,.release=release,.retain_invocation=retain};
const risc_runtime_api_v1 *risc_runtime_get_api(uint32_t version){return version==1&&owner_alive?&runtime:NULL;}
static void reset(void){acquisitions=reads=writes=releases=retains=0;acquire_ok=release_ok=release_clear=owner_alive=true;partial_acquire=false;read_result=RISC_KEY_VALUE_NOT_FOUND;length=5;kv.struct_size=sizeof(kv);kv.api_version=1;kv.get=get;preference_timeout=0;preference_retained=false;}
static void encoded(unsigned seconds){value[0]=0x54;value[1]=1;value[2]=seconds;value[3]=seconds>>8;value[4]=value[2]^value[3]^0xa5;read_result=RISC_KEY_VALUE_OK;}
int main(void){
 reset();assert(portable_foreground_idle_configure(&runtime)==0);assert(portable_foreground_idle_timeout_ms()==PORTABLE_FOREGROUND_IDLE_FALLBACK_SECONDS*1000u);assert(reads==1&&releases==1&&!writes);
 for(unsigned second=5;second<=3600;second++){
  reset();encoded(second);assert(portable_foreground_idle_configure(&runtime)==0);assert(portable_foreground_idle_timeout_ms()==second*1000u);assert(reads==1&&acquisitions==1&&releases==1&&!writes);
  unsigned calls=reads+acquisitions+releases;for(unsigned n=0;n<10;n++)assert(portable_foreground_idle_timeout_ms()==second*1000u);assert(calls==reads+acquisitions+releases);
 }
 for(unsigned i=0;i<5;i++){reset();encoded(120);value[i]^=0x80;assert(portable_foreground_idle_configure(&runtime)==-1);assert(!portable_foreground_idle_timeout_ms()&&releases==1&&!retains&&!writes);}
 for(unsigned n=0;n<=7;n++){if(n==5)continue;reset();encoded(120);length=n;assert(portable_foreground_idle_configure(&runtime)==-1);assert(!portable_foreground_idle_timeout_ms()&&releases==1&&!retains);}
 const int32_t clean[]={RISC_KEY_VALUE_BUFFER_SMALL,RISC_KEY_VALUE_INVALID,RISC_KEY_VALUE_IO,RISC_KEY_VALUE_BUSY};
 for(unsigned i=0;i<sizeof(clean)/sizeof(clean[0]);i++){reset();read_result=clean[i];assert(portable_foreground_idle_configure(&runtime)==((clean[i]==RISC_KEY_VALUE_IO||clean[i]==RISC_KEY_VALUE_BUSY)?-3:-1));assert(releases==1&&!retains&&!portable_foreground_idle_timeout_ms());
  if(clean[i]==RISC_KEY_VALUE_IO||clean[i]==RISC_KEY_VALUE_BUSY){encoded(45);assert(portable_foreground_idle_configure(&runtime)==0&&portable_foreground_idle_timeout_ms()==45000&&releases==2&&!retains);}}
 const int32_t terminal[]={RISC_KEY_VALUE_CONTEXT,-99,42};
 for(unsigned i=0;i<sizeof(terminal)/sizeof(terminal[0]);i++){reset();read_result=terminal[i];assert(portable_foreground_idle_configure(&runtime)==-2);assert(retains==1&&!releases);unsigned calls=reads+acquisitions;assert(portable_foreground_idle_configure(&runtime)==-2);portable_foreground_idle_timeout_changed(30000);assert(!portable_foreground_idle_timeout_ms()&&calls==reads+acquisitions);}
 reset();acquire_ok=false;assert(portable_foreground_idle_configure(&runtime)==-1&&!retains&&!reads&&!releases&&!portable_foreground_idle_timeout_ms());
 reset();acquire_ok=false;owner_alive=false;assert(portable_foreground_idle_configure(&runtime)==-2&&retains==1&&!reads&&!releases);
 reset();acquire_ok=false;partial_acquire=true;assert(portable_foreground_idle_configure(&runtime)==-2&&retains==1&&!reads&&!releases);
 reset();release_ok=false;assert(portable_foreground_idle_configure(&runtime)==-2&&retains==1&&releases==1);
 reset();release_clear=false;assert(portable_foreground_idle_configure(&runtime)==-2&&retains==1&&releases==1);
 reset();kv.struct_size=0;assert(portable_foreground_idle_configure(&runtime)==-1&&!reads&&releases==1);
 reset();risc_runtime_api_v1 old=runtime;old.struct_size=RISC_RUNTIME_CAPABILITIES_V1_SIZE;assert(portable_foreground_idle_configure(&old)==-1&&!acquisitions);
 reset();portable_foreground_idle_timeout_changed(45000);assert(portable_foreground_idle_timeout_ms()==45000);assert(!reads&&!writes&&!acquisitions);portable_foreground_idle_timeout_changed(4999);assert(!portable_foreground_idle_timeout_ms());portable_foreground_idle_timeout_changed(3600001);assert(!portable_foreground_idle_timeout_ms());
 puts("PASS idle preference: all3596 valid seconds; exact shared codec; missing-only explicit fallback; corruption disables; I/O/BUSY bounded-retry status and later success; CONTEXT/unknown/release faults retain; namespace1 get-only, no writes, pure cached queries and confirmed-save refresh");
}

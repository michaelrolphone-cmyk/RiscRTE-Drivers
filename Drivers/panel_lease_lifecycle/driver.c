/* Ordinary lifecycle adapter; never reinterpret lease callbacks as power ABI. */
#include "RiscProviderV2.h"
#include "RiscDisplayPowerV1.h"
#include "RiscPanelLeaseV1.h"
#include <string.h>
static const risc_panel_lease_api_v1 *lease;
static bool attempted,calling;
static bool prepare(void*context){
 (void)context;if(!lease||calling||!lease->is_owner(lease->context))return false;
 attempted=true;calling=true;bool ok=lease->prepare_sleep(lease->context);calling=false;return ok;
}
static bool resume(void*context){
 (void)context;if(!lease||calling||!lease->is_owner(lease->context))return false;
 if(!attempted)return true;
 calling=true;bool ok=lease->resume(lease->context);calling=false;if(ok)attempted=false;return ok;
}
static bool start(const risc_provider_dependency_v1*d,size_t n){
 if(lease||attempted||calling||!d||n!=1||!d[0].capability_id||strcmp(d[0].capability_id,RISC_PANEL_LEASE_CAPABILITY)||d[0].api_version!=1||!d[0].api)return false;
 const risc_panel_lease_api_v1*a=d[0].api;
 if(a->api_version!=1||a->struct_size<sizeof(*a)||!a->is_owner||!a->acquire||!a->release||!a->prepare_sleep||!a->resume||!a->is_owner(a->context))return false;
 lease=a;return true;
}
static bool quiesce(void){
 if(!lease)return !attempted&&!calling;
 if(attempted||calling||!lease->is_owner(lease->context))return false;
 lease=NULL;return true;
}
static void stop(void){(void)quiesce();}
/* Light-sleep prefix only. No unproved deep-sleep electrical retention. */
static const risc_display_power_api_v1 api={1,RISC_DISPLAY_POWER_LIGHT_SLEEP_SIZE,NULL,prepare,resume,NULL,NULL};
static const risc_driver_v2 driver={2,sizeof(driver),"panel-lease-lifecycle",RISC_DISPLAY_POWER_CAPABILITY,1,&api,start,stop,quiesce};
__attribute__((visibility("default")))
const risc_driver_v2*t5_driver_get(uint32_t abi){return abi==2?&driver:NULL;}

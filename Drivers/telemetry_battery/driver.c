/* A small capability adapter, with no board model, register, or runtime hook. */
#include "RiscTelemetryV1.h"
#include "RiscBatteryGaugeV1.h"
#include <stdatomic.h>
#include <string.h>
static const risc_battery_gauge_api_v1*battery;
static atomic_flag guard=ATOMIC_FLAG_INIT;
static bool enter(void){return !atomic_flag_test_and_set_explicit(&guard,memory_order_acquire);}
static void leave(void){atomic_flag_clear_explicit(&guard,memory_order_release);}
static int32_t enumerate(void*c,uint32_t index,risc_telemetry_field_v1*out){
 (void)c;if(!out||!enter())return -1;int32_t rc=-1;
 static const risc_telemetry_field_v1 fields[]={{1,RISC_TELEMETRY_BATTERY_PERCENT,"Battery"},{2,RISC_TELEMETRY_VOLTAGE_MV,"Battery voltage"},{3,RISC_TELEMETRY_CHARGING,"Charging"}};
 if(battery){rc=index<3?1:0;if(rc)*out=fields[index];}leave();return rc;
}
static int32_t read_value(void*c,uint32_t id,int32_t*out){
 (void)c;if(!out||!enter())return -1;int32_t rc=-1;risc_battery_sample_v1 sample;
 if(battery&&id>=1&&id<=3){
  rc=0;if(battery->read(battery->context,&sample)){
   if(id==1&&sample.percent<=100){*out=sample.percent;rc=1;}
   if(id==2&&sample.millivolts){*out=sample.millivolts;rc=1;}
   if(id==3){*out=(sample.flags&RISC_BATTERY_CHARGING)!=0;rc=1;}
  }
 }
 leave();return rc;
}
static bool start(const risc_provider_dependency_v1*d,size_t n){
 if(!enter())return false;bool ok=false;
 if(!battery&&d&&n==1&&d[0].capability_id&&!strcmp(d[0].capability_id,RISC_BATTERY_GAUGE_CAPABILITY)&&d[0].api_version==1&&d[0].api){
  const risc_battery_gauge_api_v1*b=d[0].api;
  if(b->api_version==1&&b->struct_size>=sizeof(*b)&&b->read){battery=b;ok=true;}
 }
 leave();return ok;
}
static bool quiesce(void){if(!enter())return false;leave();return true;}
static void stop(void){if(!enter())return;battery=NULL;leave();}
static const risc_telemetry_v1 api={1,sizeof(api),NULL,enumerate,read_value};
static const risc_driver_v2 driver={2,sizeof(driver),"telemetry-battery",RISC_TELEMETRY_CAPABILITY,1,&api,start,stop,quiesce};
__attribute__((visibility("default"))) const risc_driver_v2*t5_driver_get(uint32_t abi){return abi==2?&driver:NULL;}

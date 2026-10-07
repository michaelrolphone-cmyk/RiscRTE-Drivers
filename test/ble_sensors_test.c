#include "../Drivers/ble_sensors/driver.c"
#include <assert.h>
#include <stdio.h>
static uint64_t now=100;static unsigned claims,releases,sends;
static bool claimed,claim_failure,close_failure,drop_ack,send_failure,restore_failure,oversize;
static uint8_t queued[260];static size_t queued_size;
static uint64_t clock_now(void*c){(void)c;return now;}
static bool hclaim(void*c,uint64_t*out){(void)c;claims++;if(claimed){*out=0;return false;}*out=88;claimed=true;return !claim_failure;}
static int32_t hrelease(void*c,uint64_t t){(void)c;assert(t==88);releases++;if(close_failure)return -1;claimed=false;queued_size=0;return restore_failure?0:1;}
static bool hsend(void*c,uint64_t t,uint8_t kind,const uint8_t*p,size_t n){(void)c;assert(t==88&&claimed&&kind==1&&n==3u+p[2]);sends++;if(send_failure)return false;if(!drop_ack){uint8_t ack[]={14,4,1,p[0],p[1],0};memcpy(queued,ack,6);queued_size=6;}return true;}
static int32_t hnext(void*c,uint64_t t,uint8_t*kind,uint8_t*p,size_t cap,size_t*n){(void)c;assert(t==88&&claimed&&cap==1028);*kind=4;*n=oversize?cap+1:queued_size;if(!*n)return 0;memcpy(p,queued,queued_size);queued_size=0;return 1;}
static portable_bluetooth_host_v1 h={{1,sizeof(h),NULL,NULL,NULL,NULL,NULL},hclaim,hsend,hnext,hrelease};
static risc_platform_clock_api_v1 k={1,sizeof(k),NULL,clock_now,NULL};
static risc_provider_dependency_v1 deps[]={{"bluetooth.hci",1,&h},{"platform.clock",1,&k}};
static const risc_bluetooth_sensors_v1 *sensor;
static uint64_t t;
static risc_ble_sensor_status_v1 current(void){risc_ble_sensor_status_v1 s={.struct_size=sizeof(s)};assert(sensor->status(NULL,&s));return s;}
static void run_scan(void){assert(sensor->open(NULL,&t)&&t);for(unsigned i=0;i<5;i++){now+=20;assert(sensor->poll(NULL,t,6));}assert(current().state==RISC_BLE_SENSOR_SCANNING);}
int main(void){
 const risc_driver_v2*d=t5_driver_get(2);assert(d&&!t5_driver_get(1));sensor=d->capability;
 assert(!d->start(NULL,0));risc_provider_dependency_v1 bad[]={deps[0],deps[0]};assert(!d->start(bad,2));
 h.controls.struct_size--;assert(!d->start(deps,2));h.controls.struct_size++;
 assert(d->start(deps,2)&&!claims&&!d->start(deps,2));assert(!sensor->poll(NULL,1,1));
 run_scan();assert(claims==1&&sends==5);uint64_t old=t,extra=9;assert(!sensor->open(NULL,&extra)&&!extra);
 uint8_t report[]={0x3e,20,2,1,0,1,1,2,3,4,5,6,8,7,0x16,0xd2,0xfc,0x40,2,0xc4,9,(uint8_t)-40};
 memcpy(queued,report,sizeof(report));queued_size=sizeof(report);assert(sensor->poll(NULL,t,1));
 risc_ble_sensor_device_v1 out;assert(sensor->device(NULL,0,&out)&&out.has_temperature&&out.temperature==2500);assert(!sensor->device(NULL,1,&out));
 now+=15000;assert(sensor->poll(NULL,t,1)&&current().state==RISC_BLE_SENSOR_COMPLETE);
 close_failure=true;assert(!sensor->close(NULL,t)&&!d->quiesce());d->stop();assert(current().cleanup_pending&&claimed);
 close_failure=false;assert(sensor->close(NULL,t)&&!claimed);assert(sensor->device(NULL,0,&out));assert(!sensor->close(NULL,old));
 claimed=true;assert(!sensor->open(NULL,&t)&&t);assert(sensor->close(NULL,t)&&claimed);claimed=false;
 claim_failure=true;assert(!sensor->open(NULL,&t)&&t&&claimed);assert(sensor->close(NULL,t));claim_failure=false;
 run_scan();assert(!sensor->poll(NULL,old,1));assert(!sensor->poll(NULL,t,0));assert(!sensor->poll(NULL,t,17));
 restore_failure=true;assert(sensor->close(NULL,t)&&current().restore_failed);restore_failure=false;
 assert(sensor->open(NULL,&t));drop_ack=true;assert(sensor->poll(NULL,t,1));now+=2000;assert(!sensor->poll(NULL,t,1)&&current().state==RISC_BLE_SENSOR_ERROR);assert(sensor->close(NULL,t));drop_ack=false;
 assert(sensor->open(NULL,&t));send_failure=true;assert(!sensor->poll(NULL,t,1));assert(sensor->close(NULL,t));send_failure=false;
 assert(sensor->open(NULL,&t));oversize=true;assert(!sensor->poll(NULL,t,1));assert(sensor->close(NULL,t));oversize=false;
 assert(sensor->open(NULL,&t));now=UINT64_MAX;assert(!sensor->poll(NULL,t,1));assert(sensor->close(NULL,t));
 assert(d->quiesce());d->stop();assert(!sensor->device(NULL,0,&out)&&!sensor->status(NULL,&(risc_ble_sensor_status_v1){.struct_size=sizeof(risc_ble_sensor_status_v1)}));
 puts("Sensor provider: passive scan, copied readings, lease exclusion, failure custody, stale tokens and no implicit RF passed");
}

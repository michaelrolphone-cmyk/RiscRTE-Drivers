#include "../Drivers/ble_sensors/driver.c"
#include <assert.h>
#include <stdio.h>
static uint64_t now=100;
static bool claimed,close_failure,state_ok=true,send_failure,receive_failure;
static uint8_t controller_state=PORTABLE_BLUETOOTH_ON,queued[260],queued_type=4;
static size_t queued_size;
static unsigned commands,packets,statuses;
static uint64_t time_now(void*c){(void)c;return now;}
static bool hclaim(void*c,uint64_t*out){(void)c;if(claimed)return false;claimed=true;*out=88;return true;}
static int32_t hrelease(void*c,uint64_t t){(void)c;assert(t==88);if(close_failure)return -1;claimed=false;queued_size=0;return 1;}
static bool hstate(void*c,uint8_t*out){(void)c;statuses++;if(!state_ok)return false;*out=controller_state;return true;}
static bool hsend(void*c,uint64_t t,uint8_t kind,const uint8_t*p,size_t n){
 (void)c;assert(t==88&&claimed&&kind==1&&n==3u+p[2]);commands++;
 if(send_failure)return false;uint8_t ack[]={14,4,1,p[0],p[1],0};memcpy(queued,ack,6);queued_size=6;queued_type=4;return true;
}
static int32_t hnext(void*c,uint64_t t,uint8_t*kind,uint8_t*p,size_t cap,size_t*n){
 (void)c;assert(t==88&&claimed&&cap==1028);packets++;
 if(receive_failure)return -1;*kind=queued_type;*n=queued_size;
 if(!*n)return 0;memcpy(p,queued,queued_size);queued_size=0;return 1;
}
static portable_bluetooth_host_v1 h={{1,sizeof(h),NULL,NULL,NULL,NULL,hstate},hclaim,hsend,hnext,hrelease};
static risc_platform_clock_api_v1 clock_source={1,sizeof(clock_source),NULL,time_now,NULL};
static risc_provider_dependency_v1 deps[]={{"bluetooth.hci",1,&h},{"platform.clock",1,&clock_source}};
static const risc_bluetooth_sensors_v1 *sensor;
static const risc_bluetooth_sensor_link_api_v1 *link_api;
static uint64_t t;
static uint8_t peer[6]={1,2,3,4,5,6};
static risc_ble_sensor_link_state_v1 observation(void){
 risc_ble_sensor_link_state_v1 out={.struct_size=sizeof(out)};
 assert(link_api->link(NULL,t,peer,1,&out));
 assert(out.struct_size==sizeof(out)&&!memcmp(out.address,peer,6)&&out.address_type==1&&!out.reserved);
 return out;
}
static void open_complete(void){
 assert(sensor->open(NULL,&t)&&t);
 for(unsigned i=0;i<5;i++){now+=20;assert(sensor->poll(NULL,t,6));}
 assert(scan.phase==BLE_SCANNING);
}
static void report(void){
 const uint8_t p[]={0x3e,20,2,1,0,1,1,2,3,4,5,6,8,7,0x16,0xd2,0xfc,0x40,2,0xc4,9,(uint8_t)-40};
 memcpy(queued,p,sizeof(p));queued_size=sizeof(p);queued_type=4;assert(sensor->poll(NULL,t,1));
}
static void report_data(const uint8_t *ad,unsigned n){
 assert(n<=31);memset(queued,0,sizeof(queued));queued[0]=0x3e;queued[1]=(uint8_t)(n+12);
 queued[2]=2;queued[3]=1;queued[5]=1;memcpy(queued+6,peer,6);queued[12]=(uint8_t)n;
 memcpy(queued+13,ad,n);queued[13+n]=(uint8_t)-40;queued_size=n+14;queued_type=4;
 assert(sensor->poll(NULL,t,1));
}
static void reopen(void){assert(sensor->close(NULL,t));open_complete();report();}
int main(void){
 unsigned cases=0;
 const risc_driver_v2 *d=t5_driver_get(2);sensor=d->capability;
 assert(risc_bluetooth_sensor_link_bind_v1(sensor,&link_api)==1);cases++;
 risc_bluetooth_sensors_v1 old=*sensor;old.struct_size=sizeof(old);
 const risc_bluetooth_sensor_link_api_v1 *bound=(void*)1;
 assert(risc_bluetooth_sensor_link_bind_v1(&old,&bound)==0&&!bound);cases++;
 risc_bluetooth_sensor_link_api_v1 bad=*link_api;bad.extension_tag=0;
 assert(risc_bluetooth_sensor_link_bind_v1(&bad.sensors,&bound)==0&&!bound);cases++;
 bad=*link_api;bad.extension_version=2;assert(risc_bluetooth_sensor_link_bind_v1(&bad.sensors,&bound)==-1);cases++;
 bad=*link_api;bad.link=NULL;assert(risc_bluetooth_sensor_link_bind_v1(&bad.sensors,&bound)==-1);cases++;
 bad=*link_api;bad.sensors.struct_size=offsetof(risc_bluetooth_sensor_link_api_v1,extension_tag)+4;
 assert(risc_bluetooth_sensor_link_bind_v1(&bad.sensors,&bound)==-1);cases++;
 assert(d->start(deps,2));assert(sensor->open(NULL,&t));
 assert(observation().state==RISC_BLE_LINK_UNKNOWN&&!statuses&&!commands);cases++;
 assert(sensor->close(NULL,t));open_complete();assert(observation().state==RISC_BLE_LINK_UNKNOWN);cases++;
 report();unsigned before_commands=commands,before_packets=packets;
 risc_ble_sensor_link_state_v1 out=observation();
 assert(out.state==RISC_BLE_LINK_NOT_CONNECTED&&out.generation==t&&out.observed_ms==now);
 assert(commands==before_commands&&packets==before_packets);cases++;
 uint8_t missing[6]={9};out=(risc_ble_sensor_link_state_v1){.struct_size=sizeof(out)};
 assert(link_api->link(NULL,t,missing,1,&out)&&out.state==RISC_BLE_LINK_UNKNOWN&&!out.generation);cases++;
 out=(risc_ble_sensor_link_state_v1){.struct_size=sizeof(out),.state=123};risc_ble_sensor_link_state_v1 original=out;
 assert(!link_api->link(NULL,t+1,peer,1,&out)&&!memcmp(&out,&original,sizeof(out)));cases++;
 out.struct_size=1;original=out;assert(!link_api->link(NULL,t,peer,1,&out)&&!memcmp(&out,&original,sizeof(out)));cases++;
 h.controls.status=NULL;assert(observation().state==RISC_BLE_LINK_UNKNOWN);h.controls.status=hstate;cases++;
 close_failure=true;assert(!sensor->close(NULL,t));assert(observation().state==RISC_BLE_LINK_UNKNOWN);close_failure=false;cases++;
 assert(sensor->close(NULL,t));out=(risc_ble_sensor_link_state_v1){.struct_size=sizeof(out),.state=123};original=out;
 assert(!link_api->link(NULL,t,peer,1,&out)&&!memcmp(&out,&original,sizeof(out)));cases++;
 open_complete();report();assert(observation().state==RISC_BLE_LINK_NOT_CONNECTED);
 const uint8_t unexpected[]={0x3e,1,1};memcpy(queued,unexpected,3);queued_size=3;queued_type=4;
 assert(sensor->poll(NULL,t,1));assert(observation().state==RISC_BLE_LINK_UNKNOWN);cases++;
 reopen();queued_type=2;queued_size=4;memset(queued,0,4);assert(sensor->poll(NULL,t,1));
 assert(observation().state==RISC_BLE_LINK_UNKNOWN);cases++;
 reopen();const uint8_t reset[]={14,4,1,3,12,0};memcpy(queued,reset,6);queued_size=6;queued_type=4;
 assert(sensor->poll(NULL,t,1));assert(observation().state==RISC_BLE_LINK_UNKNOWN);cases++;
 reopen();queued[0]=3;queued[1]=0;queued_size=2;queued_type=4;assert(sensor->poll(NULL,t,1));
 assert(observation().state==RISC_BLE_LINK_UNKNOWN);cases++;
 reopen();queued[0]=5;queued[1]=0;queued_size=2;assert(sensor->poll(NULL,t,1));
 assert(observation().state==RISC_BLE_LINK_UNKNOWN);cases++;
 reopen();state_ok=false;out=(risc_ble_sensor_link_state_v1){.struct_size=sizeof(out),.state=123};original=out;
 assert(!link_api->link(NULL,t,peer,1,&out)&&!memcmp(&out,&original,sizeof(out)));
 state_ok=true;assert(observation().state==RISC_BLE_LINK_UNKNOWN);cases++;
 reopen();receive_failure=true;assert(!sensor->poll(NULL,t,1));receive_failure=false;
 assert(observation().state==RISC_BLE_LINK_UNKNOWN);cases++;
 reopen();now=UINT64_MAX;out=(risc_ble_sensor_link_state_v1){.struct_size=sizeof(out)};
 assert(!link_api->link(NULL,t,peer,1,&out));now=500;assert(observation().state==RISC_BLE_LINK_UNKNOWN);cases++;
 reopen();now+=15000;assert(sensor->poll(NULL,t,1));assert(observation().state==RISC_BLE_LINK_UNKNOWN);cases++;
 reopen();now+=15000;assert(observation().state==RISC_BLE_LINK_UNKNOWN);cases++;
 reopen();now--;out=(risc_ble_sensor_link_state_v1){.struct_size=sizeof(out),.state=123};original=out;
 assert(!link_api->link(NULL,t,peer,1,&out)&&!memcmp(&out,&original,sizeof(out)));now+=2;
 assert(observation().state==RISC_BLE_LINK_UNKNOWN);cases++;
 reopen();
 const uint8_t duplicate[][14]={
 {6,0x16,0xd2,0xfc,0x40,1,40,6,0x16,0xd2,0xfc,0x40,1,80},
 {6,0x16,0xd2,0xfc,0x40,1,40,6,0x16,0xd2,0xfc,0x41,1,80},
 {6,0x16,0xd2,0xfc,0x41,1,40,6,0x16,0xd2,0xfc,0x40,1,80},
 {6,0x16,0xd2,0xfc,0x40,1,40,6,0x16,0xd2,0xfc,0x40,1,101},
 {6,0x16,0xd2,0xfc,0x40,1,101,6,0x16,0xd2,0xfc,0x40,1,80}};
 for(unsigned i=0;i<5;i++){
  report_data(duplicate[i],14);risc_ble_sensor_device_v1 value;
  assert(sensor->device(NULL,0,&value)&&value.measurement_invalid&&!value.has_battery&&!value.reading_count);cases++;
 }
 const uint8_t valid[]={6,0x16,0xd2,0xfc,0x40,1,61};report_data(valid,sizeof(valid));
 risc_ble_sensor_device_v1 recovered;assert(sensor->device(NULL,0,&recovered)&&!recovered.measurement_invalid&&recovered.has_battery&&recovered.battery==61);cases++;
 assert(sensor->close(NULL,t));assert(d->quiesce());d->stop();
 printf("sensor receiver-link: %u actual-provider cases passed; endpoint doubles, no RF\n",cases);
}

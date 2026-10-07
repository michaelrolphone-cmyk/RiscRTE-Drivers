#include "../Drivers/telemetry_battery/driver.c"
#include <assert.h>
#include <stdio.h>
static risc_battery_sample_v1 sample={3800,255,RISC_BATTERY_CHARGING};static bool available=true;
static bool fake_read(void*c,risc_battery_sample_v1*out){(void)c;*out=sample;return available;}
int main(void){
 const risc_driver_v2*d=t5_driver_get(2);assert(d&&!t5_driver_get(1));const risc_telemetry_v1*t=d->capability;
 risc_battery_gauge_api_v1 gauge={1,sizeof(gauge),NULL,fake_read};risc_provider_dependency_v1 dep={"board.battery",1,&gauge};
 assert(!d->start(NULL,0));assert(d->start(&dep,1)&&!d->start(&dep,1));
 risc_telemetry_field_v1 f;assert(t->enumerate(NULL,0,&f)==1&&f.id==1&&f.metric==RISC_TELEMETRY_BATTERY_PERCENT);assert(t->enumerate(NULL,3,&f)==0);
 int32_t v=123;assert(t->read(NULL,1,&v)==0&&v==123);sample.percent=0;assert(t->read(NULL,1,&v)==1&&v==0);
 sample.percent=100;assert(t->read(NULL,1,&v)==1&&v==100);sample.percent=101;assert(t->read(NULL,1,&v)==0);
 assert(t->read(NULL,2,&v)==1&&v==3800);sample.millivolts=0;assert(t->read(NULL,2,&v)==0);
 assert(t->read(NULL,3,&v)==1&&v==1);sample.flags=0;assert(t->read(NULL,3,&v)==1&&v==0);assert(t->read(NULL,4,&v)==-1);
 available=false;assert(t->read(NULL,3,&v)==0);assert(d->quiesce());d->stop();assert(t->read(NULL,1,&v)==-1);
 puts("Battery telemetry adapter: capability enumeration, unknown/zero distinction, gauge failures and units passed");
}

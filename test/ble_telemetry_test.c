#include "../Drivers/ble_telemetry/driver.c"
#include <assert.h>
#include <stdio.h>
static uint64_t now=100;static unsigned claims,releases,sends,source_reads;
static bool claimed,claim_failure,close_failure,drop_ack,send_failure,restore_failure,oversize,bad_entropy,bad_value,missing_value,catalog_failure,changed_metric,duplicate_id,too_many;
static uint8_t queued[260],advertised[31];static size_t queued_size;static uint8_t advertised_length;static bool rf;
static uint64_t clock_now(void*c){(void)c;return now;}
static bool hclaim(void*c,uint64_t*out){(void)c;claims++;if(claimed){*out=0;return false;}*out=88;claimed=true;return !claim_failure;}
static int32_t hrelease(void*c,uint64_t t){(void)c;assert(t==88);releases++;if(close_failure)return -1;claimed=rf=false;queued_size=0;return restore_failure?0:1;}
static bool hsend(void*c,uint64_t t,uint8_t kind,const uint8_t*p,size_t n){
 (void)c;assert(t==88&&claimed&&kind==1&&n==3u+p[2]);sends++;if(send_failure)return false;
 unsigned op=p[0]|((unsigned)p[1]<<8);uint8_t ack[]={14,4,1,p[0],p[1],0};memcpy(queued,ack,6);queued_size=6;
 if(op==0x2018){queued[1]=12;for(unsigned i=0;i<8;i++)queued[6+i]=bad_entropy?0:(uint8_t)(0x12+i);queued_size=14;}
 if(op==0x2005){assert(n==9&&(p[8]&0xc0)==0xc0);}
 if(op==0x2006){assert(n==18&&p[7]==3&&p[8]==1&&p[16]==7);}
 if(op==0x2008){assert(n==35&&p[3]<=31);advertised_length=p[3];memcpy(advertised,p+4,31);}
 if(op==0x200a){assert(n==4);rf=p[3]!=0;}
 if(drop_ack)queued_size=0;return true;
}
static int32_t hnext(void*c,uint64_t t,uint8_t*kind,uint8_t*p,size_t cap,size_t*n){(void)c;assert(t==88&&claimed&&cap==1028);*kind=4;*n=oversize?cap+1:queued_size;if(!*n)return 0;memcpy(p,queued,queued_size);queued_size=0;return 1;}
static portable_bluetooth_host_v1 h={{1,sizeof(h),NULL,NULL,NULL,NULL,NULL},hclaim,hsend,hnext,hrelease};
static risc_platform_clock_api_v1 k={1,sizeof(k),NULL,clock_now,NULL};
static int32_t source_enum(void*c,uint32_t i,risc_telemetry_field_v1*out){
 (void)c;if(catalog_failure)return -1;
 if(too_many){*out=(risc_telemetry_field_v1){i+1,RISC_TELEMETRY_BATTERY_PERCENT,"Excess"};return 1;}
 static const risc_telemetry_field_v1 fields[]={{91,RISC_TELEMETRY_TEMPERATURE_CENTIC,"Ambient"},{7,RISC_TELEMETRY_BATTERY_PERCENT,"Fuel"},{30,RISC_TELEMETRY_HUMIDITY_CENTIPERCENT,"Humidity"}};
 if(i>=3)return 0;*out=fields[i];if(changed_metric&&i==0)out->metric=999;if(duplicate_id&&i==1)out->id=91;return 1;
}
static int32_t source_read(void*c,uint32_t id,int32_t*out){(void)c;source_reads++;if(missing_value)return 0;if(id==91)*out=-100;else if(id==7)*out=bad_value?255:73;else if(id==30)*out=5055;else return -1;return 1;}
static risc_telemetry_v1 src={1,sizeof(src),NULL,source_enum,source_read};
static risc_provider_dependency_v1 deps[]={{"bluetooth.hci",1,&h},{"platform.clock",1,&k},{"sensor.telemetry",1,&src}};
static const risc_bluetooth_telemetry_v1 *telemetry;static uint64_t t;
static const uint32_t ids[]={91,7,30};
static risc_ble_telemetry_status_v1 current(void){risc_ble_telemetry_status_v1 s={.struct_size=sizeof(s)};assert(telemetry->status(NULL,&s));return s;}
static void run_publish(void){assert(telemetry->publish(NULL,ids,3,true,&t)&&t);for(unsigned i=0;i<6;i++){now+=20;assert(telemetry->poll(NULL,t,6));}assert(current().state==RISC_BLE_TELEMETRY_PUBLISHING&&rf);}
int main(void){
 const risc_driver_v2*d=t5_driver_get(2);assert(d&&!t5_driver_get(1));telemetry=d->capability;
 assert(!d->start(NULL,0));risc_provider_dependency_v1 bad[]={deps[0],deps[0],deps[2]};assert(!d->start(bad,3));
 assert(d->start(deps,3)&&!claims&&!d->start(deps,3));risc_telemetry_field_v1 f;
 assert(telemetry->enumerate(NULL,0,&f)==1&&f.id==91&&f.metric==RISC_TELEMETRY_TEMPERATURE_CENTIC);
 int32_t value=9;assert(telemetry->read(NULL,91,&value)==1&&value==-100&&telemetry->read(NULL,999,&value)==-1);assert(!claims&&!sends);
 assert(telemetry->enumerate(NULL,3,&f)==0);assert(!telemetry->publish(NULL,ids,3,false,&t)&&!t&&!claims);
 assert(!telemetry->publish(NULL,(uint32_t[]){91,91},2,true,&t));assert(!telemetry->publish(NULL,(uint32_t[]){999},1,true,&t));
 bad_value=true;assert(!telemetry->publish(NULL,ids,3,true,&t)&&!t&&!claims);bad_value=false;
 missing_value=true;assert(!telemetry->publish(NULL,ids,3,true,&t));missing_value=false;
 catalog_failure=true;assert(!telemetry->publish(NULL,ids,3,true,&t));catalog_failure=false;
 duplicate_id=true;assert(telemetry->enumerate(NULL,0,&f)==-1);duplicate_id=false;
 too_many=true;assert(telemetry->enumerate(NULL,0,&f)==-1);too_many=false;
 run_publish();assert(claims==1&&sends==6&&current().updates==1);
 const uint8_t expected[]={2,1,6,12,0x16,0xd2,0xfc,0x40,1,73,2,0x9c,0xff,3,0xbf,0x13};
 assert(advertised_length==sizeof(expected)&&!memcmp(advertised,expected,sizeof(expected)));
 uint64_t old=t,extra=9;assert(!telemetry->publish(NULL,ids,3,true,&extra)&&!extra);
 now+=5000;assert(telemetry->poll(NULL,t,6)&&!rf);assert(telemetry->poll(NULL,t,6));assert(telemetry->poll(NULL,t,6)&&rf&&current().updates==2);
 close_failure=true;assert(!telemetry->close(NULL,t)&&!d->quiesce());d->stop();assert(current().cleanup_pending&&claimed);
 close_failure=false;assert(telemetry->close(NULL,t)&&!claimed&&!rf);assert(!telemetry->close(NULL,old));
 claimed=true;assert(!telemetry->publish(NULL,ids,3,true,&t)&&t);assert(telemetry->close(NULL,t)&&claimed);claimed=false;
 claim_failure=true;assert(!telemetry->publish(NULL,ids,3,true,&t)&&t&&!claimed);assert(telemetry->close(NULL,t));claim_failure=false;
 run_publish();assert(!telemetry->poll(NULL,old,1));assert(!telemetry->poll(NULL,t,0));assert(!telemetry->poll(NULL,t,17));
 missing_value=true;now+=5000;assert(!telemetry->poll(NULL,t,1)&&!rf&&!claimed&&current().state==RISC_BLE_TELEMETRY_FAULT);assert(telemetry->close(NULL,t));missing_value=false;
 run_publish();changed_metric=true;now+=5000;assert(!telemetry->poll(NULL,t,1)&&!rf);assert(telemetry->close(NULL,t));changed_metric=false;
 run_publish();restore_failure=true;assert(telemetry->close(NULL,t)&&current().restore_failed);restore_failure=false;
 assert(telemetry->publish(NULL,ids,3,true,&t));drop_ack=true;assert(telemetry->poll(NULL,t,1));now+=2000;assert(!telemetry->poll(NULL,t,1)&&!claimed);assert(telemetry->close(NULL,t));drop_ack=false;
 assert(telemetry->publish(NULL,ids,3,true,&t));send_failure=true;close_failure=true;assert(!telemetry->poll(NULL,t,1)&&claimed);assert(!d->quiesce());close_failure=false;assert(!telemetry->poll(NULL,t,1)&&!claimed);assert(telemetry->close(NULL,t));send_failure=false;
 assert(telemetry->publish(NULL,ids,3,true,&t));oversize=true;assert(!telemetry->poll(NULL,t,1)&&!claimed);assert(telemetry->close(NULL,t));oversize=false;
 assert(telemetry->publish(NULL,ids,3,true,&t));bad_entropy=true;assert(telemetry->poll(NULL,t,1));assert(!telemetry->poll(NULL,t,1)&&!claimed&&!rf);assert(telemetry->close(NULL,t));bad_entropy=false;
 assert(telemetry->publish(NULL,ids,3,true,&t));now=UINT64_MAX;assert(!telemetry->poll(NULL,t,1)&&!claimed);assert(telemetry->close(NULL,t));
 now=1000;assert(telemetry->publish(NULL,ids,3,true,&t));now+=2000;assert(!telemetry->poll(NULL,t,1)&&!rf&&!claimed);assert(telemetry->close(NULL,t));
 now=1000;assert(telemetry->publish(NULL,ids,3,true,&t));for(unsigned i=0;i<5;i++)assert(telemetry->poll(NULL,t,1));assert(!rf);now+=2000;assert(!telemetry->poll(NULL,t,1)&&!rf&&!claimed);assert(telemetry->close(NULL,t));
 assert(d->quiesce());d->stop();assert(telemetry->enumerate(NULL,0,&f)==-1);
 puts("Telemetry provider: explicit IDs/public consent, capability-only source, exact advertising bytes, random address, refresh and failure custody passed");
}

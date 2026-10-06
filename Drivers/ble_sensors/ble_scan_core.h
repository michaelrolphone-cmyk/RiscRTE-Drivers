#pragma once
/* Bounded, allocation-free passive LE advertising host. No connections,
 * pairing, bonding, ATT/GATT writes or advertised-data execution. */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define BLE_MAX_DEVICES 32u
#define BLE_SCAN_MS 15000u
#define BLE_COMMAND_MS 2000u
#include "RiscBluetoothSensorsV1.h"
typedef risc_ble_sensor_device_v1 ble_device;
typedef enum {BLE_IDLE,BLE_STARTING,BLE_SCANNING,BLE_COMPLETE,BLE_ERROR} ble_phase;
typedef struct {
 ble_device devices[BLE_MAX_DEVICES];unsigned count,dropped,malformed;
 ble_phase phase;unsigned command;uint16_t waiting;uint32_t command_at,scan_at;
 bool pending;uint8_t credits;const char *error;
} ble_scan;
static uint16_t ble_u16(const uint8_t*p){return (uint16_t)(p[0]|((uint16_t)p[1]<<8));}
static void ble_service(ble_device*d,uint16_t value){
 for(unsigned i=0;i<d->service_count;i++)if(d->services[i]==value)return;
 if(d->service_count<8)d->services[d->service_count++]=value;
}
/* BTHome measurements commit as one sample. A truncated known object cannot
 * leave valid-looking earlier fields behind. Unknown future IDs end a bounded
 * supported prefix, as the protocol specifies; this is marked partial. */
static void ble_bthome(ble_device*d,const uint8_t*p,size_t n){
 d->bthome=true;d->measurement_seen=d->seen;d->bthome_version=n?p[0]>>5:0;
 d->encrypted=n&&(p[0]&1)!=0;d->measurement_invalid=false;d->measurement_partial=false;
 d->has_battery=d->has_temperature=d->has_humidity=false;d->reading_count=0;
 memset(d->readings,0,sizeof(d->readings));
 if(!n){d->measurement_invalid=true;return;}
 if(d->bthome_version!=2 || d->encrypted)return;
 if(p[0]&0x1a){d->measurement_invalid=true;return;}
 risc_ble_reading_v1 values[RISC_BLE_MAX_READINGS];unsigned count=0,previous=0;
 bool first=true;
 for(size_t i=1;i<n;){
  unsigned id=p[i++],size=0,metric=0;bool signed_value=false;
  if(!first&&id<previous){d->measurement_invalid=true;return;}first=false;previous=id;
  switch(id){
   case 0:size=1;break;
   case 1:size=1;metric=RISC_TELEMETRY_BATTERY_PERCENT;break;
   case 2:size=2;metric=RISC_TELEMETRY_TEMPERATURE_CENTIC;signed_value=true;break;
   case 3:size=2;metric=RISC_TELEMETRY_HUMIDITY_CENTIPERCENT;break;
   case 4:size=3;metric=RISC_TELEMETRY_PRESSURE_CENTIHPA;break;
   case 5:size=3;metric=RISC_TELEMETRY_ILLUMINANCE_CENTILUX;break;
   case 0x0c:size=2;metric=RISC_TELEMETRY_VOLTAGE_MV;break;
   case 0x16:size=1;metric=RISC_TELEMETRY_CHARGING;break;
   default:d->measurement_partial=true;goto complete;
  }
  if(size>n-i){d->measurement_invalid=true;return;}
  int32_t value=p[i];if(size>=2)value|=(int32_t)p[i+1]<<8;if(size==3)value|=(int32_t)p[i+2]<<16;
  if(signed_value&&value>=32768)value-=65536;
  if((id==1&&value>100)||(id==3&&value>10000)||(id==0x16&&value>1)){d->measurement_invalid=true;return;}
  if(metric){if(count==RISC_BLE_MAX_READINGS){d->measurement_partial=true;goto complete;}
   values[count++]=(risc_ble_reading_v1){metric,value};}
  i+=size;
 }
complete:
 d->reading_count=(uint8_t)count;memcpy(d->readings,values,count*sizeof(values[0]));
 for(unsigned i=0;i<count;i++){
  if(values[i].metric==RISC_TELEMETRY_BATTERY_PERCENT&&!d->has_battery){d->battery=(uint8_t)values[i].value;d->has_battery=true;}
  if(values[i].metric==RISC_TELEMETRY_TEMPERATURE_CENTIC&&!d->has_temperature){d->temperature=(int16_t)values[i].value;d->has_temperature=true;}
  if(values[i].metric==RISC_TELEMETRY_HUMIDITY_CENTIPERCENT&&!d->has_humidity){d->humidity=(uint16_t)values[i].value;d->has_humidity=true;}
 }
}
static bool ble_ad_valid(const uint8_t*p,size_t n){
 for(size_t i=0;i<n;){unsigned size=p[i++];if(!size)return true;if(size>n-i)return false;
  unsigned type=p[i];if((type==2||type==3) && ((size-1)&1))return false;
  if((type==0x16||type==0xff)&&size<3)return false;
  i+=size;}
 return true;
}
static void ble_ad(ble_device*d,const uint8_t*p,size_t n){
 for(size_t i=0;i<n;){unsigned size=p[i++];if(!size)break;unsigned type=p[i++];size--;const uint8_t*v=p+i;i+=size;
  if(type==8||type==9){size_t count=size<31?size:31;for(size_t j=0;j<count;j++)d->name[j]=(v[j]>=32&&v[j]<=126)?(char)v[j]:'?';d->name[count]=0;}
  if(type==2||type==3)for(unsigned j=0;j<size;j+=2)ble_service(d,ble_u16(v+j));
  if(type==0xff){d->company=ble_u16(v);d->has_company=true;}
  if(type==0x16){uint16_t uuid=ble_u16(v);ble_service(d,uuid);if(uuid==0xfcd2)ble_bthome(d,v+2,size-2);}
 }
}
/* Validate every report before mutating the table, including multi-report
 * truncation. Identity is address + address type; selected indexes stay stable. */
static bool ble_reports(ble_scan*s,const uint8_t*p,size_t n,uint32_t now){
 if(n<2||p[0]!=2||!p[1])return false;
 size_t cursor=2;
 for(unsigned r=0;r<p[1];r++){
  if(n-cursor<10)return false;
  unsigned len=p[cursor+8];if(len>31 || len+10>n-cursor || p[cursor]>4 || p[cursor+1]>3)return false;
  if(!ble_ad_valid(p+cursor+9,len))return false;
  cursor+=len+10;
 }
 if(cursor!=n)return false;
 cursor=2;
 for(unsigned r=0;r<p[1];r++){
  const uint8_t*q=p+cursor;unsigned len=q[8],index=0;
  for(;index<s->count;index++)if(s->devices[index].address_type==q[1]&&!memcmp(s->devices[index].address,q+2,6))break;
  if(index==s->count && s->count==BLE_MAX_DEVICES){if(s->dropped!=UINT32_MAX)s->dropped++;cursor+=len+10;continue;}
  ble_device*d=&s->devices[index];if(index==s->count){memset(d,0,sizeof(*d));memcpy(d->address,q+2,6);d->address_type=q[1];s->count++;}
  d->event_type=q[0];d->rssi=(int8_t)q[len+9];d->seen=now;if(d->reports!=UINT32_MAX)d->reports++;
  d->payload_size=(uint8_t)len;memcpy(d->payload,q+9,len);ble_ad(d,q+9,len);cursor+=len+10;
 }
 return true;
}
static void ble_fail(ble_scan*s,const char*why){s->phase=BLE_ERROR;s->pending=false;s->error=why;}
static void ble_begin(ble_scan*s){memset(s,0,sizeof(*s));s->phase=BLE_STARTING;s->credits=1;}
/* At most one outstanding command; ack must match opcode and status. */
static size_t ble_command(ble_scan*s,uint8_t out[16],uint32_t now){
 static const uint16_t opcodes[]={0x0c03,0x0c01,0x2001,0x200b,0x200c};
 static const uint8_t sizes[]={0,8,8,7,2};
 static const uint8_t parameters[][8]={{0},{0x10,0x80,0,0,0,0,0,0x20},{2,0,0,0,0,0,0,0},{0,0x60,0,0x30,0,0,0},{1,0}};
 if(s->phase!=BLE_STARTING||s->pending||!s->credits||s->command>=5)return 0;
 uint16_t opcode=opcodes[s->command];out[0]=(uint8_t)opcode;out[1]=(uint8_t)(opcode>>8);out[2]=sizes[s->command];memcpy(out+3,parameters[s->command],out[2]);
 s->pending=true;s->credits=0;s->waiting=opcode;s->command_at=now;return 3u+out[2];
}
static void ble_event(ble_scan*s,uint8_t type,const uint8_t*p,size_t n,uint32_t now){
 if(type!=4)return;
 if(n<2||n!=2u+p[1]){if(s->malformed!=UINT32_MAX)s->malformed++;return;}
 if(p[0]==0x0e && n>=5)s->credits=p[2];
 if(p[0]==0x0f && n==6)s->credits=p[3];
 if(p[0]==0x10){ble_fail(s,"Controller error");return;}
 if(p[0]==0x0e && n>=6 && s->pending && ble_u16(p+3)==s->waiting){
  if(p[5]){ble_fail(s,"Command rejected");return;}
  s->pending=false;s->command++;s->command_at=now;
  if(s->command==5){s->phase=BLE_SCANNING;s->scan_at=now;}
 }else if(p[0]==0x0f && n==6 && s->pending && ble_u16(p+4)==s->waiting && p[2])ble_fail(s,"Command failed");
 else if(p[0]==0x3e && s->phase==BLE_SCANNING && n>=3 && p[2]==2 && !ble_reports(s,p+2,n-2,now))if(s->malformed!=UINT32_MAX)s->malformed++;
}
static bool ble_expired(ble_scan*s,uint32_t now){
 if(s->phase==BLE_STARTING&&(s->pending||!s->credits)&&(uint32_t)(now-s->command_at)>=BLE_COMMAND_MS){ble_fail(s,"Controller timeout");return true;}
 if(s->phase==BLE_SCANNING&&(uint32_t)(now-s->scan_at)>=BLE_SCAN_MS){s->phase=BLE_COMPLETE;return true;}
 return s->phase==BLE_ERROR;
}

#pragma once
#include "RiscTelemetryV1.h"
#include <string.h>
/* Exact BTHome v2 scalar encoding; no interpretation of source labels/IDs as
 * wire object IDs. Everything advertised is explicitly selected by its ID. */
static bool telemetry_shape(uint32_t metric,uint8_t*object,uint8_t*size,int32_t*min,int32_t*max){
 *min=0;
 switch(metric){
  case RISC_TELEMETRY_BATTERY_PERCENT:*object=1;*size=1;*max=100;break;
  case RISC_TELEMETRY_TEMPERATURE_CENTIC:*object=2;*size=2;*min=-32768;*max=32767;break;
  case RISC_TELEMETRY_HUMIDITY_CENTIPERCENT:*object=3;*size=2;*max=10000;break;
  case RISC_TELEMETRY_PRESSURE_CENTIHPA:*object=4;*size=3;*max=0xffffff;break;
  case RISC_TELEMETRY_ILLUMINANCE_CENTILUX:*object=5;*size=3;*max=0xffffff;break;
  case RISC_TELEMETRY_VOLTAGE_MV:*object=0x0c;*size=2;*max=65535;break;
  case RISC_TELEMETRY_CHARGING:*object=0x16;*size=1;*max=1;break;
  default:return false;
 }
 return true;
}
static bool telemetry_encode(const risc_telemetry_field_v1 *fields,const int32_t *values,uint32_t count,uint8_t out[31],uint8_t*length){
 if(!fields||!values||!out||!length||!count||count>RISC_TELEMETRY_MAX_FIELDS)return false;
 uint8_t result[31]={2,1,6,0,0x16,0xd2,0xfc,0x40};unsigned at=8;
 uint8_t order[RISC_TELEMETRY_MAX_FIELDS],objects[RISC_TELEMETRY_MAX_FIELDS],sizes[RISC_TELEMETRY_MAX_FIELDS];
 for(unsigned i=0;i<count;i++){
  int32_t min,max;if(!fields[i].id||!telemetry_shape(fields[i].metric,&objects[i],&sizes[i],&min,&max)||values[i]<min||values[i]>max)return false;
  for(unsigned j=0;j<i;j++)if(fields[i].id==fields[j].id)return false;
  order[i]=(uint8_t)i;unsigned j=i;
  while(j&&objects[order[j-1]]>objects[order[j]]){uint8_t t=order[j-1];order[j-1]=order[j];order[j]=t;j--;}
 }
 for(unsigned i=0;i<count;i++){
  unsigned j=order[i];if(1u+sizes[j]>31u-at)return false;
  result[at++]=objects[j];uint32_t value=(uint32_t)values[j];
  for(unsigned k=0;k<sizes[j];k++){result[at++]=(uint8_t)value;value>>=8;}
 }
 result[3]=(uint8_t)(at-4);memcpy(out,result,31);*length=(uint8_t)at;return true;
}

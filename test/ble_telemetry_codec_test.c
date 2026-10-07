#include "../Drivers/ble_telemetry/codec.h"
#include <assert.h>
#include <stdio.h>
int main(void){
 risc_telemetry_field_v1 fields[16];int32_t values[16];uint8_t out[31],len;
 for(unsigned i=0;i<16;i++){fields[i]=(risc_telemetry_field_v1){i+1,RISC_TELEMETRY_TEMPERATURE_CENTIC,"Temperature"};values[i]=(int32_t)i-5;}
 assert(telemetry_encode(fields,values,7,out,&len)&&len==29);
 memset(out,0xcc,sizeof(out));len=99;assert(!telemetry_encode(fields,values,8,out,&len)&&out[0]==0xcc&&len==99);
 assert(!telemetry_encode(fields,values,0,out,&len));assert(!telemetry_encode(fields,values,17,out,&len));
 for(uint32_t metric=1;metric<=7;metric++){
  uint8_t object,size;int32_t low,high;assert(telemetry_shape(metric,&object,&size,&low,&high));fields[0].metric=metric;
  values[0]=low;assert(telemetry_encode(fields,values,1,out,&len));values[0]=high;assert(telemetry_encode(fields,values,1,out,&len));
  values[0]=low-1;assert(!telemetry_encode(fields,values,1,out,&len));values[0]=high+1;assert(!telemetry_encode(fields,values,1,out,&len));
 }
 fields[0].metric=999;assert(!telemetry_encode(fields,values,1,out,&len));fields[0].metric=RISC_TELEMETRY_BATTERY_PERCENT;fields[0].id=0;values[0]=50;assert(!telemetry_encode(fields,values,1,out,&len));
 fields[0].id=1;fields[1]=fields[0];values[1]=50;assert(!telemetry_encode(fields,values,2,out,&len));
 puts("Telemetry codec: every metric boundary, budget overflow, source IDs and atomic output passed");
}

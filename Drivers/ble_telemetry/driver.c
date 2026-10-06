#include "RiscBluetoothTelemetryV1.h"
#include "RiscBluetoothHostV1.h"
#include "RiscPlatformClockV1.h"
#include "codec.h"
#include <stdatomic.h>
#define COMMAND_MS 2000u
#define REFRESH_MS 5000u
static const portable_bluetooth_host_v1*host;
static const risc_platform_clock_api_v1*clock_api;
static const risc_telemetry_v1*source;
static atomic_flag guard=ATOMIC_FLAG_INIT;
static bool started,pending,restore_failed;
static uint64_t lease,token,serial,command_at,updated_at;
static uint32_t state,step,selected_count,updates;
static uint16_t waiting;
static uint8_t credits,packet[31],packet_length,address[6];
static risc_telemetry_field_v1 selected[RISC_TELEMETRY_MAX_FIELDS];
static const char*error;
static bool enter(void){return !atomic_flag_test_and_set_explicit(&guard,memory_order_acquire);}
static void leave(void){atomic_flag_clear_explicit(&guard,memory_order_release);}
static bool release_lease(void){
 if(lease){int32_t rc=host->release(host->controls.context,lease);if(rc<0)return false;lease=0;restore_failed=rc==0;}
 return true;
}
static void fault(const char*why){state=RISC_BLE_TELEMETRY_FAULT;error=why;pending=false;(void)release_lease();}
static bool close_impl(uint64_t t){
 if(!started||!token||t!=token||!release_lease())return false;
 token=0;pending=false;state=RISC_BLE_TELEMETRY_OFF;selected_count=0;
 memset(packet,0,sizeof(packet));memset(address,0,sizeof(address));memset(selected,0,sizeof(selected));return true;
}
/* Validate a complete bounded enumeration, not just a requested prefix. A
 * source that cannot prove end-of-list is not silently truncated. */
static int32_t catalog(risc_telemetry_field_v1 fields[RISC_TELEMETRY_MAX_FIELDS]){
 for(unsigned i=0;i<=RISC_TELEMETRY_MAX_FIELDS;i++){
  risc_telemetry_field_v1 field={0};int32_t rc=source->enumerate(source->context,i,&field);
  if(rc==0)return (int32_t)i;
  if(rc!=1||i==RISC_TELEMETRY_MAX_FIELDS||!field.id||!memchr(field.label,0,sizeof(field.label)))return -1;
  for(unsigned j=0;j<i;j++)if(fields[j].id==field.id)return -1;
  fields[i]=field;
 }
 return -1;
}
static int32_t enumerate(void*c,uint32_t index,risc_telemetry_field_v1*out){
 (void)c;if(!out||!enter())return -1;int32_t rc=-1;
 if(started){risc_telemetry_field_v1 fields[RISC_TELEMETRY_MAX_FIELDS];int32_t n=catalog(fields);
  if(n>=0){rc=index<(uint32_t)n?1:0;if(rc)*out=fields[index];}}
 leave();return rc;
}
static int32_t read_value(void*c,uint32_t id,int32_t*out){
 (void)c;if(!out||!enter())return -1;int32_t rc=-1;
 if(started){risc_telemetry_field_v1 fields[RISC_TELEMETRY_MAX_FIELDS];int32_t n=catalog(fields);
  for(int32_t i=0;i<n;i++)if(fields[i].id==id){int32_t value;rc=source->read(source->context,id,&value);if(rc==1)*out=value;else if(rc!=0)rc=-1;break;}}
 leave();return rc;
}
static bool sample(void){
 risc_telemetry_field_v1 fields[RISC_TELEMETRY_MAX_FIELDS];int32_t values[RISC_TELEMETRY_MAX_FIELDS];
 int32_t n=catalog(fields);if(n<0)return false;
 for(unsigned i=0;i<selected_count;i++){
  bool found=false;for(int32_t j=0;j<n;j++)if(selected[i].id==fields[j].id&&selected[i].metric==fields[j].metric){found=true;break;}
  if(!found||source->read(source->context,selected[i].id,&values[i])!=1)return false;
 }
 return telemetry_encode(selected,values,selected_count,packet,&packet_length);
}
static bool publish(void*c,const uint32_t*ids,uint32_t count,bool public_broadcast,uint64_t*out){
 (void)c;if(!out)return false;*out=0;if(!ids||!count||count>RISC_TELEMETRY_MAX_FIELDS||!public_broadcast||!enter())return false;
 bool ok=false;
 if(!started||token||serial==UINT64_MAX)goto done;
 risc_telemetry_field_v1 fields[RISC_TELEMETRY_MAX_FIELDS];int32_t n=catalog(fields);if(n<0)goto done;
 for(unsigned i=0;i<count;i++){
  for(unsigned j=0;j<i;j++)if(ids[i]==ids[j])goto done;
  bool found=false;for(int32_t j=0;j<n;j++)if(fields[j].id==ids[i]){selected[i]=fields[j];found=true;break;}
  if(!found)goto done;
 }
 selected_count=count;if(!sample()){selected_count=0;goto done;}
 uint64_t now=clock_api->monotonic_ms(clock_api->context);if(now==UINT64_MAX)goto done;
 token=++serial;*out=token;state=RISC_BLE_TELEMETRY_STARTING;step=0;credits=1;pending=false;
 error=NULL;restore_failed=false;updates=0;command_at=updated_at=now;
 if(!host->claim(host->controls.context,&lease)||!lease){fault("Bluetooth busy - retry");goto done;}
 ok=true;
done:leave();return ok;
}
/* A bounded HCI advertising transaction reuses the existing exclusive raw-HCI
 * control path. HID's NimBLE host cannot own that controller concurrently.
 * No security-manager, ATT, connection, or GATT stack is added here. */
static bool send_step(uint64_t now){
 uint8_t p[35]={0};uint16_t opcode=0;uint8_t n=0;
 switch(step){
  case 0:opcode=0x0c03;break; /* reset only under our lease */
  case 1:opcode=0x2018;break; /* controller entropy, never libc randomness */
  case 2:opcode=0x2005;n=6;memcpy(p+3,address,6);break;
  case 3:opcode=0x2006;n=15;p[3]=0x40;p[4]=6;p[5]=0x40;p[6]=6;p[7]=3;p[8]=1;p[16]=7;break;
  case 4:case 7:opcode=0x2008;n=32;p[3]=packet_length;memcpy(p+4,packet,31);break;
  case 5:case 8:opcode=0x200a;n=1;p[3]=1;break;
  case 6:opcode=0x200a;n=1;p[3]=0;break;
  default:return false;
 }
 p[0]=(uint8_t)opcode;p[1]=(uint8_t)(opcode>>8);p[2]=n;
 pending=true;waiting=opcode;credits=0;command_at=now;
 if(!host->send_owned(host->controls.context,lease,1,p,3u+n)){fault("Advertising command failed");return false;}
 return true;
}
static void event(const uint8_t*p,size_t n,uint64_t now){
 if(n<2||n!=2u+p[1]){fault("Malformed controller event");return;}
 if(p[0]==0x10){fault("Controller error");return;}
 if(p[0]==0x0f){
  if(n!=6){fault("Malformed command status");return;}
  credits=p[3];if(pending&&((uint16_t)p[4]|((uint16_t)p[5]<<8))==waiting&&p[2])fault("Advertising command rejected");return;
 }
 if(p[0]!=0x0e)return;
 if(n<5){fault("Malformed command complete");return;}
 credits=p[2];uint16_t opcode=(uint16_t)p[3]|((uint16_t)p[4]<<8);
 if(!pending||opcode!=waiting)return;
 if(n<6||p[5]){fault("Advertising command rejected");return;}
 if(step==1){
  if(n!=14){fault("Controller entropy unavailable");return;}
  memcpy(address,p+6,6);address[5]|=0xc0;
  bool zero=true,ones=true;for(unsigned i=0;i<6;i++){uint8_t mask=i==5?0x3f:0xff;zero&=(address[i]&mask)==0;ones&=(address[i]&mask)==mask;}
  if(zero||ones){fault("Invalid random address");return;}
 }else if(n!=6){fault("Unexpected command result");return;}
 pending=false;command_at=now;
 if(step==5||step==8){state=RISC_BLE_TELEMETRY_PUBLISHING;updated_at=now;if(updates!=UINT32_MAX)updates++;step=9;}else step++;
}
static bool poll(void*c,uint64_t t,uint32_t limit){
 (void)c;if(!enter())return false;
 if(!started||!token||t!=token||!limit||limit>16){leave();return false;}
 if(state==RISC_BLE_TELEMETRY_FAULT){(void)release_lease();leave();return false;}
 uint64_t now=clock_api->monotonic_ms(clock_api->context);
 if(now==UINT64_MAX||now<command_at||now<updated_at){fault("Clock unavailable");leave();return false;}
 if(step==9&&now-updated_at>=REFRESH_MS){
  if(!sample()){fault("Selected telemetry unavailable");leave();return false;}
  step=6;command_at=now;
 }
 if(step<9&&!pending&&credits&&!send_step(now)){leave();return false;}
 for(uint32_t i=0;i<limit&&state!=RISC_BLE_TELEMETRY_FAULT;i++){
  uint8_t p[1028],kind=0;size_t n=0;int32_t rc=host->next_owned(host->controls.context,lease,&kind,p,sizeof(p),&n);
  if(rc<0||n>sizeof(p)){fault("Receive failed");break;}if(!rc)break;
  if(kind==4)event(p,n,now);
 }
 if(state!=RISC_BLE_TELEMETRY_FAULT&&step<9&&(pending||!credits)&&now-command_at>=COMMAND_MS)fault("Controller timeout");
 bool ok=state!=RISC_BLE_TELEMETRY_FAULT;leave();return ok;
}
static bool status(void*c,risc_ble_telemetry_status_v1*out){
 (void)c;if(!out||out->struct_size<sizeof(*out)||!enter())return false;bool ok=started;
 if(ok){*out=(risc_ble_telemetry_status_v1){.struct_size=sizeof(*out),.state=state,.cleanup_pending=token!=0,.restore_failed=restore_failed,.updates=updates};
  if(error){size_t n=strlen(error);if(n>=sizeof(out->error))n=sizeof(out->error)-1;memcpy(out->error,error,n);}}
 leave();return ok;
}
static bool close_publication(void*c,uint64_t t){(void)c;if(!enter())return false;bool ok=close_impl(t);leave();return ok;}
static bool start(const risc_provider_dependency_v1*d,size_t n){
 if(!enter())return false;if(started||!d||n!=3){leave();return false;}
 const portable_bluetooth_host_v1*h=NULL;const risc_platform_clock_api_v1*k=NULL;const risc_telemetry_v1*s=NULL;
 for(size_t i=0;i<n;i++){
  if(!d[i].capability_id||d[i].api_version!=1||!d[i].api){leave();return false;}
  if(!strcmp(d[i].capability_id,"bluetooth.hci")&&!h)h=d[i].api;
  else if(!strcmp(d[i].capability_id,"platform.clock")&&!k)k=d[i].api;
  else if(!strcmp(d[i].capability_id,RISC_TELEMETRY_CAPABILITY)&&!s)s=d[i].api;
  else{leave();return false;}
 }
 bool ok=h&&k&&s&&h->controls.api_version==1&&h->controls.struct_size>=sizeof(*h)&&h->claim&&h->release&&h->send_owned&&h->next_owned&&k->api_version==1&&k->struct_size>=sizeof(*k)&&k->monotonic_ms&&s->api_version==1&&s->struct_size>=sizeof(*s)&&s->enumerate&&s->read;
 if(ok){host=h;clock_api=k;source=s;started=true;state=RISC_BLE_TELEMETRY_OFF;error=NULL;restore_failed=false;updates=0;}
 leave();return ok;
}
static bool quiesce(void){if(!enter())return false;bool ok=!token||close_impl(token);leave();return ok;}
static void stop(void){if(!enter())return;if(!token){started=false;host=NULL;clock_api=NULL;source=NULL;}leave();}
static const risc_bluetooth_telemetry_v1 api={1,sizeof(api),NULL,enumerate,read_value,publish,poll,status,close_publication};
static const risc_driver_v2 driver={2,sizeof(driver),"ble-telemetry",RISC_BLUETOOTH_TELEMETRY_CAPABILITY,1,&api,start,stop,quiesce};
__attribute__((visibility("default"))) const risc_driver_v2*t5_driver_get(uint32_t abi){return abi==2?&driver:NULL;}

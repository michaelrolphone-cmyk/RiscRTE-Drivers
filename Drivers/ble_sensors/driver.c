/* Reuses the Utilities passive advertising scanner, under the same exclusive
 * bluetooth.hci lease used by NimBLE HID. This is not a second ATT/GATT host. */
#include "RiscBluetoothHostV1.h"
#include "RiscPlatformClockV1.h"
#include "ble_scan_core.h"
#include <stdatomic.h>
static const portable_bluetooth_host_v1 *host;
static const risc_platform_clock_api_v1 *clock_api;
static atomic_flag guard=ATOMIC_FLAG_INIT;
static bool started,restore_failed;
static uint64_t lease,token,serial;
static ble_scan scan;
static bool enter(void){return !atomic_flag_test_and_set_explicit(&guard,memory_order_acquire);}
static void leave(void){atomic_flag_clear_explicit(&guard,memory_order_release);}
static bool close_impl(uint64_t t){
 if(!started||!token||t!=token)return false;
 if(lease){int32_t rc=host->release(host->controls.context,lease);if(rc<0)return false;lease=0;restore_failed=rc==0;}
 token=0;if(scan.phase==BLE_STARTING||scan.phase==BLE_SCANNING)scan.phase=BLE_COMPLETE;return true;
}
static bool open_scan(void*c,uint64_t*out){
 (void)c;if(!out)return false;*out=0;if(!enter())return false;
 bool ok=false;
 if(started&&!token&&serial!=UINT64_MAX){
  ble_begin(&scan);restore_failed=false;token=++serial;*out=token;
  if(host->claim(host->controls.context,&lease)&&lease)ok=true;else ble_fail(&scan,"Bluetooth busy - retry");
 }
 leave();return ok;
}
static bool poll_scan(void*c,uint64_t t,uint32_t limit){
 (void)c;if(!enter())return false;
 if(!started||!token||t!=token||!lease||!limit||limit>16){leave();return false;}
 if(scan.phase!=BLE_STARTING&&scan.phase!=BLE_SCANNING){bool ok=scan.phase==BLE_COMPLETE;leave();return ok;}
 uint64_t wide=clock_api->monotonic_ms(clock_api->context);uint32_t now=(uint32_t)wide;
 if(wide==UINT64_MAX){ble_fail(&scan,"Clock unavailable");leave();return false;}
 uint8_t command[16];size_t size=ble_command(&scan,command,now);
 if(size&&!host->send_owned(host->controls.context,lease,1,command,size))ble_fail(&scan,"Send failed - retry");
 for(uint32_t i=0;i<limit&&(scan.phase==BLE_STARTING||scan.phase==BLE_SCANNING);i++){
  uint8_t packet[1028],type=0;size_t count=0;
  int32_t rc=host->next_owned(host->controls.context,lease,&type,packet,sizeof(packet),&count);
  if(rc<0||count>sizeof(packet)){ble_fail(&scan,"Receive failed - retry");break;}if(!rc)break;
  ble_event(&scan,type,packet,count,now);
 }
 (void)ble_expired(&scan,now);bool ok=scan.phase!=BLE_ERROR;leave();return ok;
}
static bool status_scan(void*c,risc_ble_sensor_status_v1*out){
 (void)c;if(!out||out->struct_size<sizeof(*out)||!enter())return false;
 bool ok=started;if(ok){*out=(risc_ble_sensor_status_v1){.struct_size=sizeof(*out),.state=scan.phase,
 .count=scan.count,.dropped=scan.dropped,.malformed=scan.malformed,.cleanup_pending=token!=0,.restore_failed=restore_failed};
 if(scan.error){size_t n=strlen(scan.error);if(n>=sizeof(out->error))n=sizeof(out->error)-1;memcpy(out->error,scan.error,n);}}
 leave();return ok;
}
static bool device_scan(void*c,uint32_t index,risc_ble_sensor_device_v1*out){
 (void)c;if(!out||!enter())return false;bool ok=started&&index<scan.count;if(ok){uint64_t now=clock_api->monotonic_ms(clock_api->context);if(now==UINT64_MAX)ok=false;else{*out=scan.devices[index];out->seen_age_ms=(uint32_t)now-out->seen;out->measurement_age_ms=(uint32_t)now-out->measurement_seen;}}leave();return ok;
}
static bool close_scan(void*c,uint64_t t){(void)c;if(!enter())return false;bool ok=close_impl(t);leave();return ok;}
static bool start(const risc_provider_dependency_v1*d,size_t n){
 if(!enter())return false;
 if(started||!d||n!=2){leave();return false;}
 const portable_bluetooth_host_v1*h=NULL;const risc_platform_clock_api_v1*k=NULL;
 for(size_t i=0;i<n;i++){
  if(!d[i].capability_id||d[i].api_version!=1||!d[i].api){leave();return false;}
  if(!strcmp(d[i].capability_id,"bluetooth.hci")&&!h)h=d[i].api;
  else if(!strcmp(d[i].capability_id,"platform.clock")&&!k)k=d[i].api;
  else {leave();return false;}
 }
 bool ok=h&&k&&h->controls.api_version==1&&h->controls.struct_size>=sizeof(*h)&&h->claim&&h->release&&h->send_owned&&h->next_owned&&k->api_version==1&&k->struct_size>=sizeof(*k)&&k->monotonic_ms;
 if(ok){host=h;clock_api=k;scan=(ble_scan){0};restore_failed=false;started=true;}
 leave();return ok;
}
static bool quiesce(void){if(!enter())return false;bool ok=!token||close_impl(token);leave();return ok;}
static void stop(void){if(!enter())return;if(!token){started=false;host=NULL;clock_api=NULL;memset(&scan,0,sizeof(scan));}leave();}
static const risc_bluetooth_sensors_v1 api={1,sizeof(api),NULL,open_scan,poll_scan,status_scan,device_scan,close_scan};
static const risc_driver_v2 driver={2,sizeof(driver),"ble-sensors",RISC_BLUETOOTH_SENSORS_CAPABILITY,1,&api,start,stop,quiesce};
__attribute__((visibility("default"))) const risc_driver_v2*t5_driver_get(uint32_t abi){return abi==2?&driver:NULL;}

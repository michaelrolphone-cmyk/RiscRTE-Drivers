/* Test-only explicit discovery and known single fake-device selection. This
 * host grant/poll helper is NOT an installed monitor or automatic real policy. */
#include "PortableSerialClient.h"
#include "RiscUsbControllerV1.h"
#include <assert.h>
#include <string.h>
extern const char*cdc_test_mode(void);
extern void cdc_test_set_phase(unsigned);
extern void cdc_test_event(const char*);
extern unsigned cdc_test_transfers(void);
static portable_serial_client c;
static const risc_runtime_api_v1 *r;
static void outer_discovery(void){
 risc_runtime_capability_v1 h={0};h.struct_size=sizeof(h);
 assert(r->acquire("usb.host",1,0,&h));
 const risc_usb_host_discovery_v1*host=h.api;size_t n=0;
 assert(host->poll(host->host.context,16,&n));assert(r->release(&h));
}
__attribute__((visibility("default")))int app_module_init(void){r=risc_runtime_get_api(1);assert(portable_serial_init(&c,r));return 0;}
__attribute__((visibility("default")))void app_main(void){
 assert(portable_serial_refresh(&c)&&!c.inventory_count);
 outer_discovery();assert(portable_serial_refresh(&c)&&c.inventory_count==1);
 t5_serial_config_t framing={115200,8,T5_SERIAL_PARITY_NONE,1,T5_SERIAL_FLOW_NONE};
 bool ok=portable_serial_open(&c,0,&framing);
 if(!strcmp(cdc_test_mode(),"open-retained")){assert(!ok&&c.cleanup_required);return;}
 assert(ok&&c.connected);assert(portable_serial_control(&c,true,true));
 if(!strcmp(cdc_test_mode(),"detach")){
  cdc_test_set_phase(1);outer_discovery();uint8_t b[8];assert(!portable_serial_tick(&c,b,sizeof(b),1));
  assert(!c.connected&&c.result==PSC_DISCONNECTED&&!c.cleanup_required);return;
 }
 assert(portable_serial_queue(&c,"native",0));uint8_t data[16]={0};size_t n=0;
 if(!strcmp(cdc_test_mode(),"unknown")){
  cdc_test_set_phase(2);unsigned before=cdc_test_transfers();
  assert(!portable_serial_tick(&c,data,sizeof(data),1)&&c.connected&&c.result==PSC_IO);
  r->yield_ms(1);assert(cdc_test_transfers()==before);
  cdc_test_set_phase(0);
 }
 for(unsigned i=1;i<32&&n<8;i++){n+=portable_serial_tick(&c,data+n,sizeof(data)-n,i);r->yield_ms(1);}
 assert(n==8&&!memcmp(data,"native\r\n",8)&&c.tx_bytes==8&&c.rx_bytes==8);
 ok=portable_serial_close(&c);
 if(!strcmp(cdc_test_mode(),"close-retained")){assert(!ok&&c.cleanup_required);return;}
 assert(ok&&!c.cleanup_required&&!c.session);cdc_test_event("client:complete");
}
__attribute__((visibility("default")))void app_module_fini(void){cdc_test_event("client:fini");}

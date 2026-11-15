/* Software-only controller. Never installed or published. */
#include "RiscUsbControllerV1.h"
#include <assert.h>
#include <string.h>
extern const char *cdc_test_mode(void);
extern unsigned cdc_test_phase(void);
extern void cdc_test_event(const char*);
static bool running,claimed[2];
static unsigned phase;
static uint8_t fifo[1024];static size_t head,tail;
static const uint8_t descriptor[]={9,2,41,0,2,1,0,0x80,50,9,4,0,0,0,2,2,1,0,9,4,1,0,2,10,0,0,0,7,5,0x81,2,64,0,0,7,5,2,2,64,0,0};
static int32_t event(void*c,risc_usb_controller_event_v1*o){(void)c;if(!phase){phase=1;*o=(risc_usb_controller_event_v1){1,17};return 1;}if(cdc_test_phase()==1&&phase==1){phase=2;*o=(risc_usb_controller_event_v1){2,17};return 1;}return 0;}
static bool config(void*c,uint64_t d,uint8_t*b,size_t*n,uint16_t*v,uint16_t*p){(void)c;assert(d==17&&*n>=sizeof(descriptor));if(!strcmp(cdc_test_mode(),"unknown")&&cdc_test_phase()==2)return false;memcpy(b,descriptor,sizeof(descriptor));*n=sizeof(descriptor);*v=1;*p=2;return true;}
static bool claim(void*c,uint64_t d,uint8_t i,uint8_t a,uint64_t*out){(void)c;assert(d==17&&i<2&&!a&&!claimed[i]);claimed[i]=true;*out=100+i;cdc_test_event("claim");return true;}
static bool release(void*c,uint64_t h){(void)c;assert(h>=100&&h<=101&&claimed[h-100]);cdc_test_event("release");if(!strcmp(cdc_test_mode(),"close-retained")||!strcmp(cdc_test_mode(),"open-retained"))return false;claimed[h-100]=false;return true;}
static int32_t control(void*c,uint64_t d,uint8_t t,uint8_t r,uint16_t v,uint16_t i,uint8_t*b,uint16_t n,uint32_t ms){(void)c;(void)v;(void)b;assert(d==17&&t==0x21&&(r==0x20||r==0x22)&&i==0&&ms);if(!strcmp(cdc_test_mode(),"open-retained"))return -1;return n;}
static int32_t read_data(void*c,uint64_t h,uint8_t e,uint8_t*b,size_t n,uint32_t ms){(void)c;assert(h==101&&e==0x81&&claimed[1]&&ms);cdc_test_event("transfer");size_t k=tail-head;if(k>n)k=n;if(k>2)k=2;memcpy(b,fifo+head,k);head+=k;return (int32_t)k;}
static int32_t write_data(void*c,uint64_t h,uint8_t e,const uint8_t*b,size_t n,uint32_t ms){(void)c;assert(h==101&&e==2&&claimed[1]&&ms);cdc_test_event("transfer");if(n>2)n=2;assert(tail+n<=sizeof(fifo));memcpy(fifo+tail,b,n);tail+=n;return (int32_t)n;}
static bool idle(void*c){(void)c;return !claimed[0]&&!claimed[1];}
static bool start(const risc_provider_dependency_v1*d,size_t n){(void)d;if(n||running)return false;running=true;cdc_test_event("controller:start");return true;}
static void stop(void){assert(idle(0));running=false;cdc_test_event("controller:stop");}
static bool quiesce(void){return idle(0);}
static const risc_usb_controller_api_v1 api={1,sizeof(api),0,event,config,claim,release,control,read_data,write_data,idle};
static const risc_driver_v2 driver={2,sizeof(driver),"cdc-test-controller","usb.controller",1,&api,start,stop,quiesce};
__attribute__((visibility("default")))const risc_driver_v2*t5_driver_get(uint32_t abi){return abi==2?&driver:0;}

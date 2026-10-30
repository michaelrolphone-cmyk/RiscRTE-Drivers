#include "RiscUsbControllerV1.h"
#include "RiscSerialPortV1.h"
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
static uint8_t config[256];
static size_t config_len;
static int claims,releases;
static uint8_t last_control,last_data,last_alt,last_in,last_out;
static uint64_t data_lease;
static void begin_config(uint8_t interfaces){const uint8_t header[]={9,2,0,0,0,1,0,0x80,50};memcpy(config,header,sizeof(header));config[4]=interfaces;config_len=sizeof(header);claims=releases=0;last_control=last_data=last_alt=last_in=last_out=0xff;data_lease=0;}
static void append(const uint8_t *item,size_t n){assert(n>=2&&n==item[0]&&n<=sizeof(config)-config_len);memcpy(config+config_len,item,n);config_len+=n;}
#define ADD(...) do {const uint8_t item[]={__VA_ARGS__};append(item,sizeof(item));}while(0)
static void finish_config(void){config[2]=(uint8_t)config_len;config[3]=(uint8_t)(config_len>>8);}
static bool configuration(void *ctx,uint64_t device,uint8_t *dest,size_t *cap,uint16_t *vid,uint16_t *pid){(void)ctx;assert(device&&dest&&cap&&vid&&pid);if(*cap<config_len)return false;memcpy(dest,config,config_len);*cap=config_len;*vid=0x1234;*pid=0x5678;return true;}
static bool claim(void *ctx,uint64_t device,uint8_t iface,uint8_t alternate,uint64_t *out){(void)ctx;assert(device&&out);if(!claims){last_control=iface;assert(alternate==0);}else{last_data=iface;last_alt=alternate;}*out=(uint64_t)(100+iface+256u*alternate);if(claims)data_lease=*out;++claims;return true;}
static bool checked_release(void *ctx,uint64_t id){(void)ctx;assert(id);++releases;return true;}
static void release_claim(void *ctx,uint64_t id){(void)checked_release(ctx,id);}
static int32_t control(void *ctx,uint64_t claim_token,uint8_t type,uint8_t request,uint16_t value,uint16_t index,uint8_t *payload,uint16_t size,uint32_t timeout){(void)ctx;(void)value;(void)payload;assert(claim_token==(uint64_t)(100+last_control)&&type==0x21&&timeout==1000&&index==last_control);assert(request==0x20||request==0x22);return size;}
static int32_t read_data(void *ctx,uint64_t lease,uint8_t ep,uint8_t *dst,size_t cap,uint32_t ms){(void)ctx;(void)ms;assert(lease==data_lease&&ep==last_in&&cap);dst[0]=0x5a;return 1;}
static int32_t write_data(void *ctx,uint64_t lease,uint8_t ep,const uint8_t *src,size_t n,uint32_t ms){(void)ctx;(void)ms;assert(lease==data_lease&&ep==last_out&&src&&n);return (int32_t)n;}
static bool poll_host(void *ctx, size_t budget, size_t *processed) {
    (void)ctx; assert(budget && processed); *processed = 0; return true;
}
static bool devices(void *ctx, uint64_t *tokens, size_t *count) {
    (void)ctx; if (!tokens || !count || *count < 1) return false;
    tokens[0] = 19; *count = 1; return true;
}
static void add_control(uint8_t iface){ADD(9,4,iface,0,1,2,2,1,0);}
static void add_data(uint8_t iface,uint8_t alt,bool endpoints){ADD(9,4,iface,alt,endpoints?2:0,10,0,0,0);if(endpoints){ADD(7,5,0x83,2,64,0,0);ADD(7,5,0x04,2,64,0,0);}}
static void expect_open(const risc_serial_port_api_v1 *cdc,uint8_t ctl,uint8_t data,uint8_t alt){uint64_t session=cdc->open(19);assert(session&&claims==2&&last_control==ctl&&last_data==data&&last_alt==alt);assert(cdc->configure(session,9600,8,0,1)&&cdc->control_lines(session,true,true));uint8_t v=0;last_in=0x83;last_out=0x04;assert(cdc->read(session,&v,1,1)==1&&v==0x5a);assert(cdc->write(session,&v,1,1)==1);assert(cdc->close(session)&&releases==2);}
static void expect_rejected(const risc_serial_port_api_v1 *cdc){assert(!cdc->open(19)&&claims==0&&releases==0);}
int main(int argc,char **argv){assert(argc==2);void *lib=dlopen(argv[1],RTLD_NOW);assert(lib);risc_driver_get_v2_fn get=(risc_driver_get_v2_fn)dlsym(lib,"t5_driver_get");assert(get);const risc_driver_v2 *driver=get(RISC_PROVIDER_DRIVER_ABI_V2);assert(driver);const risc_serial_port_api_v1 *cdc=(const risc_serial_port_api_v1 *)driver->capability;
 risc_usb_host_discovery_v1 host={{RISC_USB_HOST_API_V1,sizeof(host),NULL,configuration,claim,release_claim,control,read_data,write_data},poll_host,devices, 0, 0, checked_release,control};risc_provider_dependency_v1 dep={"usb.host",1,&host.host};assert(driver->start(&dep,1));
 /* Composite/IAD/union: alternate 0 has no endpoints; alternate 1 owns I/O. */
 begin_config(3);ADD(8,11,1,2,2,2,1,0);ADD(9,4,0,0,0,0xff,0,0,0);add_control(1);ADD(5,0x24,6,1,2);add_data(2,0,false);add_data(2,1,true);finish_config();expect_open(cdc,1,2,1);
 /* Union disambiguates multiple data interfaces; absence fails closed. */
 begin_config(4);add_control(1);ADD(5,0x24,6,1,2);add_data(2,0,true);add_data(3,0,true);finish_config();expect_open(cdc,1,2,0);
 begin_config(4);add_control(1);add_data(2,0,true);add_data(3,0,true);finish_config();expect_rejected(cdc);
 /* No multi-function selector or ambiguous alternate binding. */
 begin_config(4);add_control(1);add_data(2,0,true);add_control(3);finish_config();expect_rejected(cdc);
 begin_config(2);add_control(0);add_data(1,0,true);add_data(1,1,true);finish_config();expect_rejected(cdc);
 /* Union to nonexistent target; multi-slave union rejected. */
 begin_config(3);add_control(1);ADD(5,0x24,6,1,3);add_data(2,0,true);ADD(9,4,3,0,0,0xff,0,0,0);finish_config();expect_rejected(cdc);
 begin_config(2);add_control(0);ADD(6,0x24,6,0,1,2);add_data(1,0,true);finish_config();expect_rejected(cdc);
 /* Truncated descriptor and conflicting IAD. */
 begin_config(2);add_control(0);add_data(1,0,true);ADD(2,4);finish_config();expect_rejected(cdc);
 begin_config(2);ADD(8,11,0,1,2,2,1,0);add_control(0);ADD(5,0x24,6,0,1);add_data(1,0,true);finish_config();expect_rejected(cdc);
 assert(driver->quiesce());driver->stop();assert(dlclose(lib)==0);puts("CDC composite, union, IAD, alternate and claim-control cases: PASS");return 0;
}

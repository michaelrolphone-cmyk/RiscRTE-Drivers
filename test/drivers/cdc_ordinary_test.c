#include "RiscUsbControllerV1.h"
#include "RiscSerialPortV1.h"
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
static const uint8_t descriptor[] = {9,2,41,0,2,1,0,0x80,50,9,4,0,0,0,2,2,1,0,9,4,1,0,2,10,0,0,0,7,5,0x81,2,64,0,0,7,5,0x02,2,64,0,0};
static int claims,releases,controls,reads,writes;
static bool reject_data,reject_config,reject_release;
static uint8_t last_request,last_payload[7];
static uint16_t last_value;
static bool configuration(void *ctx,uint64_t device,uint8_t *dst,size_t *size,uint16_t *vid,uint16_t *pid){(void)ctx;if(!device||reject_config||!size||*size<sizeof(descriptor))return false;memcpy(dst,descriptor,sizeof(descriptor));*size=sizeof(descriptor);*vid=0x1234;*pid=0x5678;return true;}
static bool claim(void *ctx,uint64_t device,uint8_t iface,uint8_t alternate,uint64_t *lease){(void)ctx;assert(device&&lease&&alternate==0&&iface<=1);if(reject_data&&iface==1)return false;*lease=(uint64_t)(100+iface);++claims;return true;}
static bool checked_release(void *ctx,uint64_t token){(void)ctx;assert(token==100||token==101);++releases;return !reject_release;}
static void release_claim(void *ctx,uint64_t token){(void)checked_release(ctx,token);}
static int32_t control(void *ctx,uint64_t claim_token,uint8_t type,uint8_t request,uint16_t value,uint16_t iface,uint8_t *payload,uint16_t length,uint32_t timeout){(void)ctx;assert(claim_token==100&&type==0x21&&iface==0&&timeout==1000);assert((request==0x20&&payload&&length==7)||(request==0x22&&!payload&&length==0));last_request=request;last_value=value;if(payload)memcpy(last_payload,payload,7);++controls;return length;}
static int32_t bulk_read(void *ctx,uint64_t token,uint8_t ep,uint8_t *dst,size_t cap,uint32_t timeout){(void)ctx;assert(token==101&&ep==0x81&&timeout==15&&cap>=2);dst[0]='O';dst[1]='K';++reads;return 2;}
static int32_t bulk_write(void *ctx,uint64_t token,uint8_t ep,const uint8_t *src,size_t len,uint32_t timeout){(void)ctx;assert(token==101&&ep==0x02&&timeout==20&&len==2&&src[0]=='H'&&src[1]=='I');++writes;return 2;}
static bool poll(void *ctx,size_t maximum,size_t *processed){(void)ctx;if(!maximum||!processed)return false;*processed=0;return true;}
static bool devices(void *ctx,uint64_t *out,size_t *count){(void)ctx;if(!count)return false;if(*count<1||!out){*count=1;return false;}out[0]=2;*count=1;return true;}
int main(int argc,char **argv){
 assert(argc==2);void *lib=dlopen(argv[1],RTLD_NOW);assert(lib);risc_driver_get_v2_fn get=(risc_driver_get_v2_fn)dlsym(lib,"t5_driver_get");assert(get&&!get(1)&&!get(3));const risc_driver_v2 *driver=get(RISC_PROVIDER_DRIVER_ABI_V2);assert(driver&&driver->struct_size==sizeof(risc_driver_poll_v2));assert(strcmp(driver->driver_id,"usb-cdc-acm")==0&&strcmp(driver->capability_id,"serial.port")==0&&driver->capability_api==RISC_SERIAL_PORT_API_V1);const risc_serial_port_api_v1 *cdc=(const risc_serial_port_api_v1 *)driver->capability;assert(cdc&&cdc->struct_size>=sizeof(*cdc));const risc_serial_port_discovery_v1 *discovery=(const risc_serial_port_discovery_v1 *)cdc;assert(cdc->struct_size>=sizeof(*discovery)&&discovery->probe);const risc_serial_port_inventory_v1 *inventory=(const risc_serial_port_inventory_v1 *)cdc;assert(cdc->struct_size>=sizeof(*inventory)&&inventory->snapshot);
 const risc_driver_poll_v2 *poll_driver=(const risc_driver_poll_v2 *)driver;assert(poll_driver->streams.last_error);char diagnostic[96]={0};assert(!poll_driver->streams.last_error(diagnostic,sizeof(diagnostic)));
 assert(!driver->start(0,0));assert(poll_driver->streams.last_error(diagnostic,sizeof(diagnostic))&&strstr(diagnostic,"missing dependency table"));
 char short_diagnostic[8]={'X','X','X','X','X','X','X','X'};assert(poll_driver->streams.last_error(short_diagnostic,sizeof(short_diagnostic))&&short_diagnostic[7]==0);
 risc_usb_host_api_v1 legacy={RISC_USB_HOST_API_V1,sizeof(legacy),0,configuration,claim,release_claim,control,bulk_read,bulk_write};risc_provider_dependency_v1 dep={"usb.host",1,&legacy};assert(!driver->start(&dep,1));assert(poll_driver->streams.last_error(diagnostic,sizeof(diagnostic))&&strstr(diagnostic,"discovery API too small"));
 risc_usb_host_discovery_v1 host={{RISC_USB_HOST_API_V1,sizeof(host),0,configuration,claim,release_claim,control,bulk_read,bulk_write},0,0, 0, 0, checked_release,control};dep.api=&host.host;assert(!driver->start(&dep,1));assert(poll_driver->streams.last_error(diagnostic,sizeof(diagnostic))&&strstr(diagnostic,"discovery callbacks incomplete"));
 host.poll=poll;host.devices=devices;
 host.host.struct_size=sizeof(host)-1;assert(!driver->start(&dep,1));host.host.struct_size=sizeof(host);
 host.release_checked=0;assert(!driver->start(&dep,1));host.release_checked=checked_release;
 host.control_claim=0;assert(!driver->start(&dep,1));host.control_claim=control;
 dep.api_version=2;assert(!driver->start(&dep,1));dep.api_version=1;
 assert(driver->start(&dep,1)&&!poll_driver->streams.last_error(diagnostic,sizeof(diagnostic)));assert(!driver->start(&dep,1));assert(poll_driver->streams.last_error(diagnostic,sizeof(diagnostic))&&strstr(diagnostic,"already active"));
 risc_serial_device_v1 observed[2]={{0}};size_t observed_count=2;assert(inventory->snapshot(observed,&observed_count)&&observed_count==1&&observed[0].provider_device==2&&observed[0].generation==2&&observed[0].transport==RISC_SERIAL_TRANSPORT_USB);assert(claims==0&&controls==0);
 assert(discovery->probe(2)==1&&claims==0&&controls==0&&releases==0);
 reject_config=true;observed_count=2;assert(!inventory->snapshot(observed,&observed_count)&&claims==0);assert(discovery->probe(2)<0&&claims==0&&controls==0);assert(!cdc->open(2));reject_config=false;reject_data=true;assert(!cdc->open(2)&&claims==1&&releases==1);reject_data=false;
 uint64_t first=cdc->open(2);assert(first&&claims==3&&releases==1);assert(!cdc->configure(first,0,8,0,1)&&!cdc->configure(first,115200,9,0,1));assert(cdc->configure(first,115200,8,0,1));assert(last_request==0x20&&last_payload[0]==0&&last_payload[1]==0xc2&&last_payload[2]==1&&last_payload[3]==0&&last_payload[4]==0&&last_payload[5]==0&&last_payload[6]==8);assert(cdc->control_lines(first,true,true)&&last_request==0x22&&last_value==3&&controls==2);
 uint8_t data[2]={0};assert(cdc->read(first,data,2,15)==2&&data[0]=='O'&&data[1]=='K');assert(cdc->write(first,(const uint8_t *)"HI",2,20)==2&&reads==1&&writes==1);
 reject_release=true;assert(!cdc->close(first)&&!driver->quiesce()&&releases==2);reject_release=false;assert(cdc->close(first)&&driver->quiesce()&&releases==4);assert(!cdc->close(first)&&!cdc->configure(first,9600,8,0,1)&&cdc->read(first,data,2,15)==-1);
 uint64_t next=cdc->open(2);assert(next&&next!=first&&!cdc->control_lines(first,true,false));driver->stop();assert(releases==6&&cdc->read(next,data,2,15)==-1);assert(driver->start(&dep,1));assert(!cdc->close(next));driver->stop();assert(dlclose(lib)==0);puts("CDC claim-scoped control, inventory, release retry, I/O and stale token: PASS");return 0;
}

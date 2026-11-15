#!/usr/bin/env python3
"""Actual host + extracted actual controller at deterministic fake-IDF boundary."""
from pathlib import Path
import hashlib, os, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[1]
source=(ROOT/'Drivers/usb_controller_esp32s3/driver_base.cpp').read_text()
def function(signature):
    start=source.index(signature); i=source.index('{',start); depth=1; end=i+1
    while depth:
        if source[end]=='{': depth+=1
        elif source[end]=='}': depth-=1
        end+=1
    return source[start:end]+'\n'
functions='\n'.join(function(s) for s in ('bool otherClaims(', 'bool pump_transfer(', 'bool drain_bulk(', 'bool wait_completion(', 'bool idle_transfer(', 'bool claim_interface(', 'bool release_interface(', 'int32_t control(', 'int32_t bulk(', 'int32_t bulk_read(', 'int32_t bulk_write('))
mutation=os.environ.get('NEGATIVE','')
if mutation=='timeout-overrun': functions=functions.replace('const TickType_t budget = pdMS_TO_TICKS(milliseconds);','const TickType_t budget = pdMS_TO_TICKS(milliseconds); ticks += 500;')
elif mutation=='halt-skip': functions=functions.replace('if (risc_usb_endpoint_halt_poll(handle, endpoint) != ESP_OK) return false;', '(void)risc_usb_endpoint_halt_poll(handle, endpoint);')
elif mutation=='claim-forget': functions=functions.replace('if (!d) return false;', 'if (!d) return false; if (rejectClose) { *c = {}; return true; }')
elif mutation: raise ValueError(mutation)
preamble=r'''
#include "RiscUsbControllerV1.h"
#include "ClaimReleasePolicy.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#define ESP_IDF_VERSION_VAL(a,b,c) ((a)*10000+(b)*100+(c))
#define ESP_IDF_VERSION ESP_IDF_VERSION_VAL(6,1,0)
using TickType_t=uint32_t; using esp_err_t=int; using usb_device_handle_t=void*;
constexpr int ESP_OK=0, ESP_ERR_TIMEOUT=1, ESP_ERR_NOT_FINISHED=2, USB_TRANSFER_STATUS_COMPLETED=0;
constexpr unsigned kTeardownTicks=500;
struct Device { void *handle; uint64_t token; uint16_t vid,pid; bool attached; };
struct Claim { uint64_t token,physical_device; uint8_t number,alternate; bool interfaceReleased; };
struct Transfer { void *device_handle; uint8_t bEndpointAddress; int status,num_bytes,actual_num_bytes;
 uint8_t *data_buffer; void (*callback)(Transfer*); void *context; };
static Device devices[8]; static Claim claims[16]; static uint8_t bytes[4104];
static Transfer dma; static Transfer *transfer=&dma; static void *client=&dma;
static bool installed=true,running=true,fault,inFlight,completed;
static uint64_t transferClaim,serial=100;
struct BulkDrain { bool active,halted,flushed,resume; }; static BulkDrain bulkDrain;
static uint32_t ticks,callbackAt,haltAt,haltCalls,flushCalls,clearCalls,releaseCalls,closeCalls,dequeueCalls,freeCalls,submitCost,setupCost,submitCalls;
static Transfer *extraTransfer; static bool rejectFree; static uint8_t intrBytes[64]; static Transfer intrDma;
using usb_transfer_t=Transfer; constexpr unsigned RISC_USB_HID_MAX_REPORT=64;
constexpr int USB_TRANSFER_STATUS_STALL=4;
static bool rejectRelease,rejectClose,rejectFlush,rejectClear,rejectPump,ackHalt;
static int submittedStatus,submittedLength; static bool eventPending; static unsigned eventKind;
static TickType_t xTaskGetTickCount(){return ticks;}
static TickType_t pdMS_TO_TICKS(uint32_t ms){return ms;}
static uint64_t token(){return ++serial;}
static Device *device(uint64_t id){for(auto &d:devices)if(!fault&&d.handle&&d.token==id)return &d;return nullptr;}
static Claim *claim(uint64_t id){for(auto &c:claims)if(!fault&&c.token==id)return &c;return nullptr;}
static void complete_transfer(Transfer*){inFlight=false;completed=true;}
static esp_err_t risc_usb_client_transfer_events(void*,TickType_t delay){
 ticks+=delay; if(rejectPump)return 9;
 if(inFlight&&callbackAt&&ticks>=callbackAt){dma.status=submittedStatus;dma.actual_num_bytes=submittedLength;complete_transfer(&dma);}
 if(extraTransfer&&callbackAt&&ticks>=callbackAt){auto t=extraTransfer;extraTransfer=nullptr;t->status=submittedStatus;t->actual_num_bytes=submittedLength;t->callback(t);}return ESP_OK;
}
static void risc_usb_device_control_dequeue(void*){++dequeueCalls;}
static esp_err_t risc_usb_endpoint_halt_poll(void*,uint8_t){++haltCalls;return ackHalt||(haltAt&&ticks>=haltAt)?ESP_OK:ESP_ERR_NOT_FINISHED;}
static esp_err_t risc_usb_endpoint_flush(void*,uint8_t){++flushCalls;return rejectFlush?9:ESP_OK;}
static esp_err_t risc_usb_endpoint_clear(void*,uint8_t){++clearCalls;return rejectClear?9:ESP_OK;}
static esp_err_t usb_host_interface_claim(void*,void*,uint8_t,uint8_t){return ESP_OK;}
static esp_err_t usb_host_interface_release(void*,void*,uint8_t){assert(!inFlight&&!bulkDrain.active);++releaseCalls;return rejectRelease?9:ESP_OK;}
static esp_err_t usb_host_device_close(void*,void*){++closeCalls;return rejectClose?9:ESP_OK;}
static bool endpoint_mps(Device*,uint8_t,uint8_t,uint8_t,uint16_t *n){ticks+=setupCost;*n=64;return true;}
static esp_err_t usb_host_transfer_submit(Transfer*t){++submitCalls;ticks+=submitCost;if(t!=&dma)extraTransfer=t;return ESP_OK;}
static esp_err_t usb_host_transfer_alloc(size_t,int,Transfer **out){intrDma={};intrDma.data_buffer=intrBytes;*out=&intrDma;return ESP_OK;}
static esp_err_t usb_host_transfer_free(Transfer*){++freeCalls;return rejectFree?9:ESP_OK;}
static esp_err_t usb_host_transfer_submit_control(void*,Transfer*){++submitCalls;ticks+=submitCost;return ESP_OK;}
'''
functions += '\n#include "PendingInterrupt.h"\n'
main=r'''
static void reset(){
 ticks=callbackAt=haltAt=haltCalls=flushCalls=clearCalls=releaseCalls=closeCalls=dequeueCalls=freeCalls=0;
 extraTransfer=nullptr;rejectFree=false;submitCost=setupCost=submitCalls=0;for(auto &slot:interrupts)slot={};
 rejectRelease=rejectClose=rejectFlush=rejectClear=rejectPump=ackHalt=fault=inFlight=completed=false;
 bulkDrain={}; transferClaim=0; submittedStatus=0;submittedLength=1;
 dma={&dma,0x81,0,1,0,bytes,complete_transfer,nullptr};
 std::memset(claims,0,sizeof(claims));std::memset(devices,0,sizeof(devices));
 devices[0]={&dma,11,0x1234,0x5678,true}; claims[0]={17,11,0,0,false};
}
static int32_t events(void*,risc_usb_controller_event_v1 *out){if(!eventPending)return 0;*out={eventKind,11};eventPending=false;return 1;}
static bool config(void*,uint64_t,uint8_t *out,size_t *size,uint16_t *vid,uint16_t *pid){
 const uint8_t cfg[]={9,2,32,0,1,1,0,0x80,50,9,4,0,0,2,0xff,0,0,0,7,5,0x81,2,64,0,0,7,5,2,2,64,0,0};
 if(*size<sizeof(cfg))return false;std::memcpy(out,cfg,sizeof(cfg));*size=sizeof(cfg);*vid=0x1234;*pid=0x5678;return true;
}
static bool quiet(void*){for(auto &c:claims)if(c.token)return false;return !inFlight&&!bulkDrain.active;}
int main(int argc,char **argv){
 assert(argc==2);
 reset();inFlight=true;transferClaim=17;
 assert(!wait_completion(1));assert(ticks==1&&inFlight&&bulkDrain.active&&!releaseCalls);
 for(int i=0;i<50;++i)assert(!release_interface(nullptr,17));
 assert(ticks==1&&claims[0].token==17&&!releaseCalls&&!flushCalls);
 reset();inFlight=true;transferClaim=17;ticks=UINT32_MAX-1;assert(!wait_completion(3));assert(ticks==1&&inFlight);
 reset();inFlight=true;transferClaim=17;assert(!wait_completion(1));
 // Natural callback does not acknowledge a concurrently pending halt.
 inFlight=false;completed=true;assert(!release_interface(nullptr,17));assert(claims[0].token==17);
 ackHalt=true;rejectRelease=true;assert(!release_interface(nullptr,17));assert(claims[0].token==17);
 rejectRelease=false;assert(release_interface(nullptr,17));assert(!claims[0].token);
 reset();inFlight=true;transferClaim=17;ackHalt=true;rejectFlush=true;
 assert(!release_interface(nullptr,17));assert(inFlight&&claims[0].token==17);
 rejectFlush=false;callbackAt=1;ticks=1;assert(release_interface(nullptr,17));
 reset();inFlight=true;transferClaim=17;ackHalt=true;rejectPump=true;
 assert(!release_interface(nullptr,17));assert(inFlight&&fault);rejectPump=false;callbackAt=1;ticks=1;
 assert(release_interface(nullptr,17));
 reset();devices[0].attached=false;rejectClose=true;
 assert(!release_interface(nullptr,17));assert(releaseCalls==1&&closeCalls==1&&claims[0].interfaceReleased);
 assert(!release_interface(nullptr,17));assert(releaseCalls==1&&closeCalls==2);
 rejectClose=false;assert(release_interface(nullptr,17));assert(releaseCalls==1&&closeCalls==3&&!devices[0].handle);
 reset();inFlight=true;transferClaim=18;assert(!release_interface(nullptr,17));assert(!haltCalls);
 reset();uint8_t b=0;bytes[0]='X';callbackAt=1;assert(bulk_read(nullptr,17,0x81,&b,1,1)==1&&b=='X'&&ticks==1);
 reset();setupCost=3;assert(bulk_write(nullptr,17,2,&b,1,3)<0&&ticks==3&&!inFlight&&!submitCalls);
 reset();submitCost=2;assert(bulk_write(nullptr,17,2,&b,1,3)<0&&ticks==3&&inFlight);
 reset();callbackAt=1;assert(bulk_write(nullptr,17,2,&b,1,2)==1&&ticks==1);
 reset();callbackAt=1;submittedLength=8;assert(control(nullptr,11,0x21,0x22,0,0,nullptr,0,2)==0);
 reset();assert(control(nullptr,11,0x21,0x22,0,0,nullptr,0,1)<0);assert(ticks==1&&inFlight&&bulkDrain.active&&dma.bEndpointAddress==0);
 ackHalt=true;callbackAt=2;ticks=2;assert(release_interface(nullptr,17));
 // Never forward a stale EP0 handle after successful close.
 reset();callbackAt=1;submittedLength=8;assert(control(nullptr,11,0x21,0x22,0,0,nullptr,0,2)==0);
 devices[0].attached=false;assert(release_interface(nullptr,17));auto dequeues=dequeueCalls;assert(pump_transfer(0));assert(dequeueCalls==dequeues);
 // Actual PendingInterrupt implementation, including callback-before-halt.
 reset();uint8_t report[64]={};assert(read_interrupt(17,&dma,0x83,64,report,1)==0);assert(interrupts[0].pending);
 assert(!drain_interrupt(interrupts[0]));callbackAt=1;ticks=1;assert(pump_transfer(0));assert(!interrupts[0].pending);
 assert(!drain_interrupt(interrupts[0])&&!freeCalls);ackHalt=true;rejectFree=true;
 assert(!drain_interrupt(interrupts[0])&&interrupts[0].dma);rejectFree=false;
 assert(drain_interrupt(interrupts[0])&&!interrupts[0].dma&&freeCalls==2);
 // Exact host source above exact controller release body, including detach.
 reset();claims[0]={};void *elf=dlopen(argv[1],RTLD_NOW);assert(elf);
 auto get=(risc_driver_get_v2_fn)dlsym(elf,"t5_driver_get");assert(get);auto driver=get(2);
 auto host=(const risc_usb_host_discovery_v1*)driver->capability;
 risc_usb_controller_api_v1 ctl={1,sizeof(ctl),nullptr,events,config,claim_interface,release_interface,control,bulk_read,bulk_write,quiet};
 risc_provider_dependency_v1 dep={"usb.controller",1,&ctl};assert(driver->start(&dep,1));
 eventPending=true;eventKind=1;size_t count=0;assert(host->poll(nullptr,8,&count));uint64_t dev=0;count=1;assert(host->devices(nullptr,&dev,&count));
 uint64_t c=0;assert(host->host.claim(nullptr,dev,0,0,&c));
 inFlight=true;transferClaim=claims[0].token;assert(!host->release_checked(nullptr,c));
 assert(host->host.bulk_read(nullptr,c,0x81,&b,1,1)<0);
 devices[0].attached=false;eventPending=true;eventKind=2;assert(host->poll(nullptr,8,&count));
 ackHalt=true;callbackAt=1;ticks=1;rejectClose=true;assert(!host->release_checked(nullptr,c));
 assert(claims[0].interfaceReleased&&releaseCalls==1);assert(!driver->quiesce());
 rejectClose=false;assert(host->release_checked(nullptr,c));assert(releaseCalls==1);assert(driver->quiesce());driver->stop();assert(dlclose(elf)==0);
 std::puts("PASS actual host/controller: bounded timeout, delayed halt/callback, errors, EP0, read/write, detach, checked retry, failed physical close");
}
'''
flags=['-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-g']
if os.environ.get('SANITIZE')=='1':flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-sanitize-recover=all']
print('test_sha256='+hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),flush=True)
print('controller_sha256='+hashlib.sha256(source.encode()).hexdigest(),flush=True)
for path in ['Drivers/usb_host_v2/driver.c','Drivers/usb_controller_esp32s3/PendingInterrupt.h','Drivers/usb_controller_esp32s3/ClaimReleasePolicy.h']:
    print(path+' sha256='+hashlib.sha256((ROOT/path).read_bytes()).hexdigest(),flush=True)
print('actual_body_sha256='+hashlib.sha256(functions.encode()).hexdigest(),flush=True)
with tempfile.TemporaryDirectory(prefix='usb-checked-') as tmp:
    p=Path(tmp);(p/'test.cpp').write_text(preamble+functions+main)
    subprocess.run(['cc','-std=c11',*flags,'-fPIC','-shared','-I'+str(ROOT/'sdk/driver'),str(ROOT/'Drivers/usb_host_v2/driver.c'),'-o',str(p/'host.so')],check=True)
    subprocess.run(['c++','-std=c++17',*flags,'-I'+str(ROOT/'sdk/driver'),'-I'+str(ROOT/'Drivers/usb_controller_esp32s3'),str(p/'test.cpp'),'-ldl','-o',str(p/'test')],check=True)
    result=subprocess.run([str(p/'test'),str(p/'host.so')])
    if mutation:
        assert result.returncode != 0, 'negative control escaped: '+mutation
        print('REJECTED negative control: '+mutation)
    else: result.check_returncode()

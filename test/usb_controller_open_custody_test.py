#!/usr/bin/env python3
"""Actual discovery/retained-handle bodies at a fake IDF resource boundary."""
from pathlib import Path
import os,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
s=(ROOT/'Drivers/usb_controller_esp32s3/driver_base.cpp').read_text()
def body(sig):
 a=s.index(sig);b=s.index('{',a)+1;n=1
 while n:
  n+=(s[b]=='{')-(s[b]=='}');b+=1
 return s[a:b]
functions=body('void reapDetachedUnclaimed(')+'\n'+body('int32_t next_event(')
preamble=r'''
#include "RiscUsbControllerV1.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
using esp_err_t=int;using usb_device_handle_t=void*;constexpr int ESP_OK=0;
struct Device{void*handle;uint64_t token;uint16_t vid,pid;bool attached;};
struct Event{uint8_t kind,address;void*handle;};struct usb_device_desc_t{uint16_t idVendor,idProduct;};
struct UsbRoleSwitch{enum class State{Off,Failed,Cleanup,Sense,Host};State state(){return State::Host;}};static UsbRoleSwitch role;
static bool fault,installed,descriptorFailure,closeFailure,openFailure,inFlight;static struct{bool active;}bulkDrain;
static void *client=(void*)1;static Device devices[8];static Event queue[16];static unsigned queueHead,queueCount;static constexpr unsigned kEvents=16;
static unsigned closeCalls,openCalls;static uint64_t nextToken=1;static usb_device_desc_t desc={1,2};
static void service_role(){}static bool pump(unsigned){return true;}static bool claimed(uint64_t){return false;}
static int usb_host_device_open(void*,unsigned,void**out){++openCalls;*out=(void*)42;return openFailure?9:0;}
static int usb_host_get_device_descriptor(void*,const usb_device_desc_t**out){*out=&desc;return descriptorFailure?9:0;}
static int usb_host_device_close(void*,void*){++closeCalls;return closeFailure?9:0;}
static uint64_t token(){return nextToken;}
'''
main=r'''
static void reset(){fault=descriptorFailure=closeFailure=openFailure=false;closeCalls=openCalls=queueHead=0;queueCount=1;nextToken=1;queue[0]={1,3,nullptr};std::memset(devices,0,sizeof(devices));}
int main(){risc_usb_controller_event_v1 out={};reset();descriptorFailure=closeFailure=true;
 assert(next_event(nullptr,&out)==-1&&devices[0].handle&&!devices[0].attached&&fault&&!out.kind);
 closeFailure=false;reapDetachedUnclaimed();assert(!devices[0].handle&&closeCalls==2);
 reset();nextToken=0;assert(next_event(nullptr,&out)==-1&&!openCalls&&!devices[0].handle);
 reset();openFailure=closeFailure=true;assert(next_event(nullptr,&out)==-1&&devices[0].handle&&!devices[0].attached);
 closeFailure=false;reapDetachedUnclaimed();assert(!devices[0].handle);
 reset();assert(next_event(nullptr,&out)==1&&out.kind==1&&out.physical_device==1&&devices[0].attached);
 puts("PASS actual discovery: descriptor/open failure retains exact handle until checked close; exhaustion acquires nothing");}
'''
flags=['-std=c++17','-Wall','-Wextra','-Werror','-g']
if os.environ.get('SANITIZE')=='1':flags+=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer']
with tempfile.TemporaryDirectory(prefix='usb-open-custody-') as t:
 p=Path(t);(p/'test.cpp').write_text(preamble+functions+main)
 subprocess.run(['c++',*flags,'-I'+str(ROOT/'sdk/driver'),str(p/'test.cpp'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)

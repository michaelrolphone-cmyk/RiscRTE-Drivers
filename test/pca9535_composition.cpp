// Real PCA + current scoped-I2C providers and current Runtime graph. Only the
// controller registers and native owner guard are simulated; no hardware claim.
#include "runtime/drivers/ProviderGraphV2.h"
#include "RiscI2cBusV1.h"
#include "RiscGpioExpanderV1.h"
#include "RiscProviderSyncV1.h"
#include "TWatchHardwareV1.h"
#include "TWatchPlatformV1.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
using namespace RuntimeProviders;
struct Guard {uint64_t token=0;bool held=false;unsigned unlockFails=0,destroyFails=0;bool failCreateWithToken=false;};
static bool owned=true;static uint64_t serial=100;static unsigned calls=0,writes=0,writeFail=0,readFail=0,closeFail=0;
static uint8_t expectedAddress=0x23;
static uint64_t nativeToken=0;static uint16_t registers_[4]={0xa55a,0xffff,0,0xffff};
static bool owner(void*){return owned;}
static bool create(void*c,uint64_t*t){auto&g=*static_cast<Guard*>(c);*t=0;if(!owned||g.token)return false;*t=g.token=++serial;return !g.failCreateWithToken;}
static bool lock(void*c,uint64_t t){auto&g=*static_cast<Guard*>(c);if(!owned||!t||t!=g.token||g.held)return false;g.held=true;return true;}
static bool unlock(void*c,uint64_t t){auto&g=*static_cast<Guard*>(c);if(!owned||t!=g.token||!g.held)return false;if(g.unlockFails){--g.unlockFails;return false;}g.held=false;return true;}
static bool destroy(void*c,uint64_t t){auto&g=*static_cast<Guard*>(c);if(!owned||!t||t!=g.token||g.held)return false;if(g.destroyFails){--g.destroyFails;return false;}g.token=0;return true;}
static bool open(void*,uint8_t u,uint8_t sda,uint8_t scl,uint32_t hz,uint64_t*t){++calls;assert(u==0&&sda==10&&scl==11&&hz==100000&&!nativeToken);*t=nativeToken=++serial;return true;}
static bool transfer(void*,uint64_t t,uint8_t address,const uint8_t*tx,size_t tn,uint8_t*rx,size_t rn,uint32_t ms){
 ++calls;assert(owned&&nativeToken==t&&address==expectedAddress&&ms==50&&tx&&tn);
 assert(tx[0]<=6&&!(tx[0]&1));auto&r=registers_[tx[0]/2];
 if(tn==1&&rn==2){if(readFail){--readFail;return false;}rx[0]=r;rx[1]=r>>8;return true;}
 assert(tn==3&&!rn);r=uint16_t(tx[1]|uint16_t(tx[2])<<8);++writes;
 if(writeFail){--writeFail;return false;}return true;
}
static bool close(void*,uint64_t t){++calls;assert(t==nativeToken);if(closeFail){--closeFail;return false;}nativeToken=0;return true;}
static risc_hw_bus_v1 busconfig(){risc_hw_bus_v1 b{};b.struct_size=sizeof(b);b.instance_id=22;b.kind=RISC_HW_BUS_I2C;b.controller=0;b.frequency_hz=100000;b.sda=10;b.scl=11;b.sclk=b.mosi=b.miso=-1;return b;}
int main(int argc,char**argv){
 assert(argc==4);const char*mode=argv[3];Guard bg,pg;
 risc_provider_sync_api_v1 bs{1,sizeof(bs),&bg,owner,create,lock,unlock,destroy},ps{1,sizeof(ps),&pg,owner,create,lock,unlock,destroy};
 twatch_i2c_controller_v1 controller{1,sizeof(controller),nullptr,open,transfer,close};
 tw_hw_i2c_controller_v1 bc{};bc.struct_size=sizeof(bc);bc.bus=busconfig();
 tw_hw_i2c_device_v1 pc{};pc.struct_size=sizeof(pc);pc.bus=busconfig();pc.address=!strcmp(mode,"address-20")?0x20:!strcmp(mode,"address-27")?0x27:0x23;expectedAddress=pc.address;pc.irq=-1;
 risc_hardware_device_v1 bh{1,sizeof(bh),22,"espressif,esp32s3-i2c","unspecified","controller.i2c",1,sizeof(bc),&bc};
 risc_hardware_device_v1 ph{1,sizeof(ph),45,"nxp,pca9535","unspecified","peripheral.i2c",1,sizeof(pc),&pc};
 RequirementV2 br[]={{"hardware.device",1},{"platform.i2c.controller",1,nullptr,0,&controller},{"platform.sync",1,nullptr,0,&bs}};
 if(!strcmp(mode,"wrong-bus"))pc.bus.instance_id=99;
 RequirementV2 pr[]={{"hardware.device",1},{"i2c.bus",1,"twatch-i2c",pc.bus.instance_id},{"platform.sync",1,nullptr,0,&ps}};
 SpecV2 b{"twatch-i2c",argv[2],"i2c.bus",1,br,3};b.hardware=&bh;
 SpecV2 p{"pca9535-gpio",argv[1],"gpio.expander",1,pr,3};p.hardware=&ph;
 GraphV2 graph;assert(graph.addVerified(b));assert(graph.addVerified(p));assert(calls==0);
 if(!strcmp(mode,"admission")){
  auto root=graph.acquireFrom("twatch-i2c","i2c.bus",1,22);assert(root.slot);
  auto*ib=static_cast<const risc_i2c_bus_api_v1*>(graph.interfaceFor(root));
  void*image=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);assert(image);
  auto get=reinterpret_cast<const risc_driver_v2*(*)(uint32_t)>(dlsym(image,"t5_driver_get"));assert(get);auto*d=get(2);
  risc_provider_dependency_v1 deps[]={{"hardware.device",1,&ph},{"i2c.bus",1,ib},{"platform.sync",1,&ps}};
  const unsigned before=calls;
  assert(!d->start(nullptr,3));assert(!d->start(deps,2));
  for(unsigned i=0;i<3;++i){auto old=deps[i];deps[i].api=nullptr;assert(!d->start(deps,3));deps[i]=old;deps[i].api_version=2;assert(!d->start(deps,3));deps[i]=old;}
  ps.struct_size=0;assert(!d->start(deps,3));ps.struct_size=sizeof(ps);ph.api_version=2;assert(!d->start(deps,3));ph.api_version=1;
  auto old=deps[2];deps[2]=deps[1];assert(!d->start(deps,3));deps[2]=old;
  pc.address=0x28;assert(!d->start(deps,3));pc.address=0x23;
  ph.config_version=2;assert(!d->start(deps,3));ph.config_version=1;
  ph.config_type="power.axp2101";assert(!d->start(deps,3));ph.config_type="peripheral.i2c";
  owned=false;assert(!d->start(deps,3));owned=true;
  assert(calls==before&&!pg.token);assert(d->quiesce());
  assert(d->start(deps,3));auto*pa=static_cast<const risc_gpio_expander_api_v1*>(d->capability);uint64_t oldToken=0,newToken=0;
  assert(pa->claim(pa->context,1,0,0,&oldToken));assert(pa->release(pa->context,oldToken));assert(d->quiesce());
  assert(d->start(deps,3));assert(pa->claim(pa->context,1,0,0,&newToken));assert(newToken>oldToken);
  assert(!pa->release(pa->context,oldToken));assert(pa->release(pa->context,newToken));assert(d->quiesce());dlclose(image);assert(graph.release(root));assert(graph.shutdown());puts("PCA admission controls PASS");return 0;
 }
 if(!strcmp(mode,"create-token"))pg.failCreateWithToken=true;
 if(!strcmp(mode,"start-read"))readFail=1;
 auto grant=graph.acquireFrom("pca9535-gpio","gpio.expander",1,45);if(!strcmp(mode,"wrong-bus")){assert(!grant.slot&&!calls&&!nativeToken);assert(graph.shutdown());puts("PCA selected bus mismatch refused before I/O PASS");return 0;}
 if(!strcmp(mode,"create-token")||!strcmp(mode,"start-read")){assert(!grant.slot);assert(graph.shutdown());assert(!nativeToken&&!pg.token&&!bg.token);puts("PCA failed-start cleanup PASS");return 0;}assert(grant.slot);
 auto*a=static_cast<const risc_gpio_expander_api_v1*>(graph.interfaceFor(grant));assert(a);
 if(!strcmp(mode,"same-address")){
  auto independent=graph.acquireFrom("twatch-i2c","i2c.bus",1,22);assert(independent.slot);
  auto*ib=static_cast<const risc_i2c_bus_api_v1*>(graph.interfaceFor(independent));assert(ib);
  uint64_t duplicate=999;unsigned before=calls;
  assert(!ib->claim_device(ib->context,pc.address,&duplicate)&&!duplicate&&calls==before);
  assert(graph.release(independent));assert(nativeToken&&pg.token);
  assert(graph.release(grant));assert(graph.shutdown());assert(!nativeToken&&!pg.token&&!bg.token);
  puts("PCA independent current I2C same-address claim refused PASS");return 0;
 }
 uint64_t first=0,other=0;uint16_t levels=0;
 if(!strcmp(mode,"partial")){writeFail=1;assert(!a->claim(a->context,3,0,0,&first)&&first);}
 else {assert(a->claim(a->context,3,0,0,&first)&&first);assert((registers_[1]&3)==0&&(registers_[3]&3)==0);}
 if(!strcmp(mode,"release-write")){assert(a->write(a->context,first,3,3));writeFail=1;assert(!a->release(a->context,first));}
 if(!strcmp(mode,"partial")||!strcmp(mode,"write")||!strcmp(mode,"release-write")){
  if(!strcmp(mode,"write")){writeFail=1;assert(!a->write(a->context,first,1,1));}
  unsigned before=calls;assert(!a->release(a->context,first));assert(!a->claim(a->context,4,0,0,&other));assert(calls==before);
  assert(!graph.release(grant));assert(nativeToken&&bg.token&&pg.token);assert(!graph.activationSafe());assert(!graph.shutdown());puts("PCA uncertain-write custody PASS; graph destructor must refuse disposal");fflush(stdout);return 0;
 }
 if(!strcmp(mode,"read-failure")){readFail=1;assert(!a->claim(a->context,4,0,0,&other)&&other);assert(a->release(a->context,other));}
 assert(!a->claim(a->context,2,0,0,&other)&&!other);
 assert(a->claim(a->context,0x30,0x30,0,&other)&&other!=first);
 assert(a->read(a->context,other,&levels)&&levels==(registers_[0]&0x30));
 assert(!a->write(a->context,other,0x10,0));assert(!a->write(a->context,first,4,0));
 unsigned before=calls;owned=false;assert(!a->write(a->context,first,1,1));assert(!a->release(a->context,first));owned=true;assert(calls==before);
 assert(!a->release(a->context,first+1000));assert(a->write(a->context,first,3,3));
 if(!strcmp(mode,"release-read")){readFail=1;assert(!a->release(a->context,first));assert(!graph.release(grant));}
 if(!strcmp(mode,"unlock")){pg.unlockFails=1;assert(!a->release(a->context,first));}
 assert(a->release(a->context,first));assert(!a->release(a->context,first));assert(a->release(a->context,other));
 if(!strcmp(mode,"destroy"))pg.destroyFails=1;
 if(!strcmp(mode,"close"))closeFail=1;
 bool released=graph.release(grant);
 if(!strcmp(mode,"release-read")) { /* release was already revoked, graph retry is shutdown */ (void)released; }
 else if(!strcmp(mode,"destroy")||!strcmp(mode,"close")){assert(!released);assert(graph.release(grant));}
 else assert(released);
 assert(graph.shutdown());assert(!nativeToken&&!bg.token&&!pg.token);
 puts("PCA real I2C/graph ownership and cleanup PASS");
}

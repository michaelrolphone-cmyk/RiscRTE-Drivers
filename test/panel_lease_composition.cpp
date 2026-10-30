// Actual installed profile/TPS/lease-lifecycle/PCA/I2C bodies in current graph.
// Only native owner/scheduler and lowest controller registers are simulated.
#include "runtime/drivers/ProviderGraphV2.h"
#include "RiscPanelLeaseV1.h"
#include "RiscTps65185ProfileV1.h"
#include "RiscDisplayPowerV1.h"
#include "RiscDisplayOutputV1.h"
#include "RiscGpioExpanderV1.h"
#include "RiscI2cBusV1.h"
#include "RiscProviderSyncV1.h"
#include "RiscPlatformClockWaitV1.h"
#include "TWatchHardwareV1.h"
#include "TWatchPlatformV1.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <string>
using namespace RuntimeProviders;
static_assert(RISC_DISPLAY_INFO_ROUND==(1u<<6),"Current ROUND meaning must not change");
struct Guard{uint64_t token=0;bool held=false;unsigned unlockFails=0,destroyFails=0;bool createWithTokenFails=false;};
static bool owned=true,externalGood=true,internalGood=true,stalled=false;
static uint64_t serial=100,nativeToken=0,nowMs=10;
static unsigned pcaReadFail=0;
static const risc_i2c_bus_api_v1*actualBus=nullptr;
static bool falseClaim(void*,uint8_t address,uint64_t*out){bool ok=actualBus->claim_device(actualBus->context,address,out);assert(ok&&*out);return false;}
static unsigned calls=0,pcaWrites=0,tpsWrites=0,waits=0,waitFail=0,pcaWriteFail=0,tpsWriteFail=0;
static uint16_t pcaRegs[4]={0x4000,0xffff,0,0xffff};static uint8_t tpsRegs[32]{};
static bool owner(void*){return owned;}
static bool create(void*c,uint64_t*t){auto&g=*static_cast<Guard*>(c);*t=0;if(!owned||g.token)return false;*t=g.token=++serial;return !g.createWithTokenFails;}
static bool lock(void*c,uint64_t t){auto&g=*static_cast<Guard*>(c);if(!owned||!t||t!=g.token||g.held)return false;g.held=true;return true;}
static bool unlock(void*c,uint64_t t){auto&g=*static_cast<Guard*>(c);if(!owned||!t||t!=g.token||!g.held)return false;if(g.unlockFails){--g.unlockFails;return false;}g.held=false;return true;}
static bool destroy(void*c,uint64_t t){auto&g=*static_cast<Guard*>(c);if(!owned||!t||t!=g.token||g.held)return false;if(g.destroyFails){--g.destroyFails;return false;}g.token=0;return true;}
static bool open(void*,uint8_t u,uint8_t sda,uint8_t scl,uint32_t hz,uint64_t*t){++calls;assert(u==0&&sda==10&&scl==11&&hz==100000&&!nativeToken);*t=nativeToken=++serial;return true;}
static bool transfer(void*,uint64_t t,uint8_t address,const uint8_t*tx,size_t tn,uint8_t*rx,size_t rn,uint32_t ms){
 ++calls;assert(owned&&nativeToken==t&&ms==50&&tx&&tn);
 if(address==0x23){assert(tx[0]<=6&&!(tx[0]&1));auto&r=pcaRegs[tx[0]/2];
  if(tn==1&&rn==2){if(tx[0]==2&&pcaReadFail){--pcaReadFail;return false;}uint16_t v=tx[0]==0?(externalGood?0x4000:0):r;rx[0]=v;rx[1]=v>>8;return true;}
  assert(tn==3&&!rn);r=uint16_t(tx[1]|uint16_t(tx[2])<<8);++pcaWrites;
  if(pcaWriteFail){--pcaWriteFail;return false;}return true;
 }
 assert(address==0x68&&tx[0]<32);
 if(tn==1&&rn==1){*rx=tx[0]==0x0f?(internalGood?0xfa:0):tpsRegs[tx[0]];return true;}
 assert(!rn&&tn>=2&&tn<=3);for(size_t i=1;i<tn;++i)tpsRegs[tx[0]+i-1]=tx[i];++tpsWrites;
 if(tpsWriteFail){--tpsWriteFail;return false;}return true;
}
static bool close(void*,uint64_t t){++calls;assert(t==nativeToken);nativeToken=0;return true;}
static uint64_t now(void*){return nowMs;}
static void forbiddenLegacySleep(void*,uint32_t){assert(false&&"must use scheduler-only wait suffix");}
static bool wait(void*,uint32_t ms){assert(owned&&ms==1);++waits;if(waitFail){--waitFail;return false;}if(!stalled)nowMs+=ms;return true;}
static risc_hw_bus_v1 busconfig(){risc_hw_bus_v1 b{};b.struct_size=sizeof(b);b.instance_id=22;b.kind=RISC_HW_BUS_I2C;b.controller=0;b.frequency_hz=100000;b.sda=10;b.scl=11;b.sclk=b.mosi=b.miso=-1;return b;}
int main(int argc,char**argv){
 assert(argc==3);std::string root=argv[1];const char*mode=argv[2];Guard bg,pg,tg;
 risc_provider_sync_api_v1 bs{1,sizeof(bs),&bg,owner,create,lock,unlock,destroy},ps{1,sizeof(ps),&pg,owner,create,lock,unlock,destroy},ts{1,sizeof(ts),&tg,owner,create,lock,unlock,destroy};
 twatch_i2c_controller_v1 controller{1,sizeof(controller),nullptr,open,transfer,close};
 risc_platform_clock_wait_v1 clock{{1,sizeof(clock),nullptr,now,forbiddenLegacySleep},RISC_PLATFORM_CLOCK_WAIT_TAG_V1,1,wait};
 tw_hw_i2c_controller_v1 bc{};bc.struct_size=sizeof(bc);bc.bus=busconfig();
 tw_hw_i2c_device_v1 pc{};pc.struct_size=sizeof(pc);pc.bus=busconfig();pc.address=0x23;pc.irq=-1;
 auto tc=pc;tc.address=0x68;
 risc_hardware_device_v1 bh{1,sizeof(bh),22,"espressif,esp32s3-i2c","unspecified","controller.i2c",1,sizeof(bc),&bc};
 risc_hardware_device_v1 ph{1,sizeof(ph),45,"nxp,pca9535","unspecified","peripheral.i2c",1,sizeof(pc),&pc};
 risc_hardware_device_v1 th{1,sizeof(th),46,"ti,tps65185","unspecified","peripheral.i2c",1,sizeof(tc),&tc};
 RequirementV2 br[]={{"hardware.device",1},{"platform.i2c.controller",1,nullptr,0,&controller},{"platform.sync",1,nullptr,0,&bs}};
 RequirementV2 pr[]={{"hardware.device",1},{"i2c.bus",1,"twatch-i2c",22},{"platform.sync",1,nullptr,0,&ps}};
 RequirementV2 tr[]={{"hardware.device",1},{"i2c.bus",1,"twatch-i2c",22},{"gpio.expander",1,"pca9535-gpio",45},{"platform.clock",1,nullptr,0,&clock},{"platform.sync",1,nullptr,0,&ts},{RISC_TPS65185_PROFILE_CAPABILITY,1,"t5s3-panel-profile"}};
 RequirementV2 lr[]={{RISC_PANEL_LEASE_CAPABILITY,1,"tps65185-power",46}};
 std::string bp=root+"/i2c.so",pp=root+"/pca.so",tp=root+"/tps.so",fp=root+"/profile.so",lp=root+"/lifecycle.so";
 SpecV2 b{"twatch-i2c",bp.c_str(),"i2c.bus",1,br,3};b.hardware=&bh;
 SpecV2 p{"pca9535-gpio",pp.c_str(),"gpio.expander",1,pr,3};p.hardware=&ph;
 SpecV2 f{"t5s3-panel-profile",fp.c_str(),RISC_TPS65185_PROFILE_CAPABILITY,1,nullptr,0};
 SpecV2 t{"tps65185-power",tp.c_str(),RISC_PANEL_LEASE_CAPABILITY,1,tr,6};t.hardware=&th;
 SpecV2 l{"panel-lease-lifecycle",lp.c_str(),"display.power",1,lr,1};
 GraphV2 graph;assert(graph.addVerified(b)&&graph.addVerified(p)&&graph.addVerified(f)&&graph.addVerified(t)&&graph.addVerified(l));assert(!calls);
 if(!strcmp(mode,"admission")){
  auto busGrant=graph.acquireFrom("twatch-i2c","i2c.bus",1,22),pcaGrant=graph.acquireFrom("pca9535-gpio","gpio.expander",1,45),profileGrant=graph.acquireFrom("t5s3-panel-profile",RISC_TPS65185_PROFILE_CAPABILITY,1);
  assert(busGrant.slot&&pcaGrant.slot&&profileGrant.slot);
  auto profile=*static_cast<const risc_tps65185_profile_api_v1*>(graph.interfaceFor(profileGrant));
  risc_provider_dependency_v1 deps[]={{"hardware.device",1,&th},{"i2c.bus",1,graph.interfaceFor(busGrant)},{"gpio.expander",1,graph.interfaceFor(pcaGrant)},{"platform.clock",1,&clock},{"platform.sync",1,&ts},{RISC_TPS65185_PROFILE_CAPABILITY,1,&profile}};
  void*image=dlopen(tp.c_str(),RTLD_NOW|RTLD_LOCAL);assert(image);auto get=reinterpret_cast<const risc_driver_v2*(*)(uint32_t)>(dlsym(image,"t5_driver_get"));assert(get);auto*d=get(2);unsigned before=calls;
  assert(!d->start(nullptr,6)&&!d->start(deps,5));
  for(unsigned i=0;i<6;++i){auto old=deps[i];deps[i].api=nullptr;assert(!d->start(deps,6));deps[i]=old;deps[i].api_version=2;assert(!d->start(deps,6));deps[i]=old;}
  auto old=deps[5];deps[5]=deps[4];assert(!d->start(deps,6));deps[5]=old;
  profile.inputs=profile.outputs;assert(!d->start(deps,6));profile.inputs=0xc000;
  profile.wakeup=3;assert(!d->start(deps,6));profile.wakeup=0x2000;
  profile.vcom_millivolts=5111;assert(!d->start(deps,6));profile.vcom_millivolts=1600;
  clock.wait_tag=0;assert(!d->start(deps,6));clock.wait_tag=RISC_PLATFORM_CLOCK_WAIT_TAG_V1;
  tc.address=0x69;assert(!d->start(deps,6));tc.address=0x68;
  th.config_type="power.axp2101";assert(!d->start(deps,6));th.config_type="peripheral.i2c";
  owned=false;assert(!d->start(deps,6));owned=true;assert(calls==before&&!tg.token);
  actualBus=static_cast<const risc_i2c_bus_api_v1*>(deps[1].api);auto faulty=*actualBus;faulty.claim_device=falseClaim;auto originalBus=deps[1];deps[1].api=&faulty;
  assert(!d->start(deps,6)&&!tg.token);assert(d->quiesce());deps[1]=originalBus;
  pcaReadFail=1;assert(!d->start(deps,6)&&!tg.token);assert(d->quiesce());
  assert(d->start(deps,6));auto*a=static_cast<const risc_panel_lease_api_v1*>(d->capability);uint64_t x=0,y=0;
  assert(a->acquire(a->context,&x)&&a->release(a->context,x)&&d->quiesce());
  assert(d->start(deps,6)&&a->acquire(a->context,&y)&&y>x);assert(!a->release(a->context,x));assert(a->release(a->context,y)&&d->quiesce());dlclose(image);
  assert(graph.release(profileGrant)&&graph.release(pcaGrant)&&graph.release(busGrant)&&graph.shutdown());puts("Panel strict admission and restart generation PASS");return 0;
 }
 if(!strcmp(mode,"partial-start"))pcaWriteFail=1;
 if(!strcmp(mode,"create-token"))tg.createWithTokenFails=true;
 GrantV2 powerGrant{};if(!strcmp(mode,"eager")){powerGrant=graph.acquireFrom("panel-lease-lifecycle","display.power",1);assert(powerGrant.slot);}
 auto grant=graph.acquireFrom("tps65185-power",RISC_PANEL_LEASE_CAPABILITY,1,46);
 if(!strcmp(mode,"partial-start")){assert(!grant.slot&&nativeToken&&pg.token&&tg.token);assert(!graph.shutdown());puts("Panel retained-chain refusal PASS");fflush(stdout);return 0;}
 if(!strcmp(mode,"create-token")){assert(!grant.slot&&graph.shutdown()&&!nativeToken&&!tg.token&&!pg.token);puts("Panel failed-create custody cleanup PASS");return 0;}
 assert(grant.slot);auto*a=static_cast<const risc_panel_lease_api_v1*>(graph.interfaceFor(grant));assert(a);
 if(!powerGrant.slot){powerGrant=graph.acquireFrom("panel-lease-lifecycle","display.power",1);}
 assert(powerGrant.slot);
 auto*power=static_cast<const risc_display_power_api_v1*>(graph.interfaceFor(powerGrant));assert(power&&power->struct_size==RISC_DISPLAY_POWER_LIGHT_SLEEP_SIZE&&!power->prepare_deep_sleep&&!power->resume_status);
 uint64_t token=0,second=0;unsigned before=calls;
 owned=false;assert(!a->acquire(a->context,&token)&&!token);assert(!power->prepare_sleep(power->context));owned=true;assert(calls==before&&power->resume(power->context));
 if(!strcmp(mode,"external-timeout"))externalGood=false;
 if(!strcmp(mode,"internal-timeout"))internalGood=false;
 if(!strcmp(mode,"stalled-clock"))stalled=true;
 if(!strcmp(mode,"tps-write"))tpsWriteFail=1;
 bool acquired=a->acquire(a->context,&token);
 if(!strcmp(mode,"external-timeout")||!strcmp(mode,"internal-timeout")){assert(!acquired&&!token&&waits<=402&&(pcaRegs[1]&0x3b00)==0);externalGood=internalGood=true;assert(a->acquire(a->context,&token));}
 else if(!strcmp(mode,"stalled-clock")){assert(!acquired&&token&&waits==8);stalled=false;assert(a->release(a->context,token));assert(a->acquire(a->context,&token));}
 else if(strcmp(mode,"tps-write"))assert(acquired&&token);
 if(!strcmp(mode,"tps-write")){assert(!acquired&&token);assert(!a->release(a->context,token));assert(graph.release(powerGrant));assert(!graph.release(grant));assert(nativeToken&&tg.token&&pg.token);assert(!graph.shutdown());puts("Panel retained-chain refusal PASS");fflush(stdout);return 0;}
 assert(tpsRegs[1]==0x3f&&tpsRegs[3]==160&&tpsRegs[4]==0);
 assert(!a->acquire(a->context,&second)&&!second);before=calls;owned=false;assert(!a->release(a->context,token));owned=true;assert(calls==before);
 assert(!a->release(a->context,token+100));
 assert(!power->prepare_sleep(power->context));assert(!a->acquire(a->context,&second)&&!second);assert(power->resume(power->context));
 if(!strcmp(mode,"release-wait"))waitFail=1;
 if(!strcmp(mode,"release-stalled"))stalled=true;
 if(!strcmp(mode,"release-pca-write")){pcaWriteFail=1;assert(!a->release(a->context,token));assert(graph.release(powerGrant));assert(!graph.release(grant)&&!graph.shutdown());assert(nativeToken&&pg.token&&tg.token);puts("Panel retained-chain refusal PASS");fflush(stdout);return 0;}
 if(!strcmp(mode,"release-unlock"))tg.unlockFails=1;
 if(!strcmp(mode,"release-wait")||!strcmp(mode,"release-unlock")||!strcmp(mode,"release-stalled"))assert(!a->release(a->context,token));
 stalled=false;
 assert(a->release(a->context,token));assert(!a->release(a->context,token));assert((pcaRegs[1]&0x3b00)==0);
 if(!strcmp(mode,"release-unlock")){assert(!a->acquire(a->context,&second));assert(graph.release(powerGrant)&&graph.release(grant)&&graph.shutdown());puts("Panel unlock-fault cleanup-only custody PASS");return 0;}
 if(!strcmp(mode,"sleep-wait"))waitFail=1;
 if(!strcmp(mode,"sleep-rollback"))waitFail=2;
 bool prepared=power->prepare_sleep(power->context);assert(prepared==(strcmp(mode,"sleep-wait")!=0&&strcmp(mode,"sleep-rollback")!=0));
 assert(!a->acquire(a->context,&second)&&!second);
 if(!strcmp(mode,"prepared-unload")){assert(!graph.release(powerGrant));assert(graph.release(grant));assert(!graph.shutdown());assert(nativeToken&&pg.token&&tg.token);puts("Panel retained-chain refusal PASS");fflush(stdout);return 0;}
 if(!strcmp(mode,"sleep-rollback")){assert(!power->resume(power->context));assert(!a->acquire(a->context,&second)&&!second);}
 assert(power->resume(power->context));assert(a->acquire(a->context,&second)&&second>token);assert(!a->release(a->context,token));assert(a->release(a->context,second));
 assert(graph.release(powerGrant));assert(graph.release(grant));assert(graph.shutdown());assert(!nativeToken&&!pg.token&&!tg.token&&!bg.token);
 puts("Panel actual lease/lifecycle/PCA/I2C graph PASS");
}

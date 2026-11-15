#ifndef SCOPED_I2C_RUNTIME_TEST
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../components/i2c-scoped/driver.c"

static bool owned = true, held, transferred, reenter;
static uint64_t mutex_token, native_token, next_token = 100;
static unsigned creates, destroys, opens, closes, transfers;
static unsigned create_fault, open_fault, lock_fails, unlock_fails, destroy_fails, close_fails;
static tw_hw_i2c_controller_v1 config;
static risc_hardware_device_v1 hardware;
static risc_provider_dependency_v1 deps[4];

static bool owner(void *context) { assert(context == &owned); return owned; }
static bool create_mutex(void *context, uint64_t *out) {
    assert(context == &owned && out && !mutex_token); ++creates; *out = 0;
    if (!owned || create_fault == 1) return false;
    if (create_fault == 2) return true;
    *out = mutex_token = ++next_token;
    return create_fault != 3;
}
static bool try_mutex(void *context, uint64_t token) {
    assert(context == &owned);
    if (!owned || token != mutex_token || !token || held) return false;
    if (lock_fails) { --lock_fails; return false; }
    held = true; return true;
}
static bool unlock_mutex(void *context, uint64_t token) {
    assert(context == &owned);
    if (!owned || token != mutex_token || !token || !held) return false;
    if (unlock_fails) { --unlock_fails; return false; }
    held = false; return true;
}
static bool destroy_mutex(void *context, uint64_t token) {
    assert(context == &owned); ++destroys;
    if (!owned || token != mutex_token || !token || held) return false;
    assert(!native_token);
    if (destroy_fails) { --destroy_fails; return false; }
    mutex_token = 0; return true;
}
static bool controller_open(void *context, uint8_t unit, uint8_t sda, uint8_t scl,
                            uint32_t hz, uint64_t *out) {
    assert(context == &config && held && !native_token && out); ++opens;
    assert(unit == config.bus.controller && sda == config.bus.sda &&
           scl == config.bus.scl && hz == config.bus.frequency_hz);
    *out = 0;
    if (open_fault == 1) return false;
    if (open_fault == 2) return true;
    *out = native_token = ++next_token;
    return open_fault != 3;
}
static bool controller_transfer(void *context, uint64_t token, uint8_t address,
                                const uint8_t *tx, size_t tn, uint8_t *rx,
                                size_t rn, uint32_t ms) {
    assert(context == &config && held && token == native_token && native_token);
    assert(address >= 8 && address <= 119 && ms && ms <= 1000 && (tn || rn));
    assert((!tn || tx) && (!rn || rx) && tn <= 512 && rn <= 512);
    ++transfers;
    if (reenter) {
        uint64_t child = 99;
        assert(!api.claim_device(NULL, 0x22, &child) && !child);
        assert(!api.release_device(NULL, serial));
        assert(!api.transact(NULL, serial, tx, tn, rx, rn, ms));
        assert(!descriptor.quiesce());
        assert(!descriptor.start(deps, 3));
    }
    if (rn) memset(rx, 0xa5, rn);
    return transferred;
}
static bool controller_close(void *context, uint64_t token) {
    assert(context == &config && held && token == native_token && native_token);
    ++closes;
    if (close_fails) { --close_fails; return false; }
    native_token = 0; return true;
}
static const twatch_i2c_controller_v1 native = {
    1, sizeof(native), &config, controller_open, controller_transfer, controller_close
};
static const risc_provider_sync_api_v1 sync = {
    1, sizeof(sync), &owned, owner, create_mutex, try_mutex, unlock_mutex, destroy_mutex
};
static void fresh(void) {
    assert(!native_token && !mutex_token && !held && descriptor.quiesce());
    owned = transferred = true; reenter = false;
    create_fault = open_fault = lock_fails = unlock_fails = destroy_fails = close_fails = 0;
    config = (tw_hw_i2c_controller_v1){.struct_size=sizeof(config), .bus={
        .struct_size=sizeof(config.bus), .kind=RISC_HW_BUS_I2C, .instance_id=907,
        .controller=1, .frequency_hz=100000, .sda=8, .scl=9, .sclk=-1, .mosi=-1, .miso=-1}};
    hardware = (risc_hardware_device_v1){1, sizeof(hardware), 27, "espressif,esp32s3-i2c",
        "unspecified", "controller.i2c", 1, sizeof(config), &config};
    deps[0]=(risc_provider_dependency_v1){"hardware.device",1,&hardware};
    deps[1]=(risc_provider_dependency_v1){"platform.i2c.controller",1,&native};
    deps[2]=(risc_provider_dependency_v1){"platform.sync",1,&sync};
    deps[3]=deps[2];
}
static void rejected(void) {
    unsigned before_open=opens, before_create=creates;
    assert(!descriptor.start(deps,3));
    assert(opens == before_open && creates == before_create && descriptor.quiesce());
}
static void invalid_dependencies(void) {
    fresh(); unsigned before=opens;
    assert(!descriptor.start(NULL,3) && !descriptor.start(deps,2) && !descriptor.start(deps,4));
    assert(opens==before);
    for (unsigned i=0;i<3;++i) {
        fresh(); deps[i].api=NULL; rejected();
        fresh(); deps[i].api_version=2; rejected();
        fresh(); deps[i].capability_id=NULL; rejected();
        fresh(); deps[i]=deps[(i+1)%3]; rejected();
        uint32_t small[2]={1,8}; fresh(); deps[i].api=small; rejected();
        small[0]=2; fresh(); deps[i].api=small; rejected();
    }
    fresh(); twatch_i2c_controller_v1 n=native; deps[1].api=&n; n.open=NULL; rejected();
    n=native;n.transfer=NULL;rejected();n=native;n.close=NULL;rejected();
    for (unsigned i=0;i<5;++i) {
        fresh(); risc_provider_sync_api_v1 s=sync;deps[2].api=&s;
        if(i==0)s.is_owner=NULL;
        if(i==1)s.create=NULL;
        if(i==2)s.try_lock=NULL;
        if(i==3)s.unlock=NULL;
        if(i==4)s.destroy=NULL;
        rejected();
    }
    fresh(); owned=false; rejected(); owned=true;
}
#define BAD_CONFIG(change) do { fresh(); change; rejected(); } while (0)
static void invalid_hardware(void) {
    BAD_CONFIG(hardware.instance_id=0); BAD_CONFIG(hardware.compatible="wrong");
    BAD_CONFIG(hardware.revision="wrong"); BAD_CONFIG(hardware.config_type="touch.i2c");
    BAD_CONFIG(hardware.config_version=2); BAD_CONFIG(hardware.config=NULL);
    BAD_CONFIG(hardware.config_size=sizeof(config)-1); BAD_CONFIG(config.struct_size=0);
    BAD_CONFIG(config.struct_size=sizeof(config)+1); BAD_CONFIG(config.bus.struct_size=0);
    BAD_CONFIG(config.bus.kind=RISC_HW_BUS_SPI); BAD_CONFIG(config.bus.instance_id=0);
    BAD_CONFIG(config.bus.controller=2); BAD_CONFIG(config.bus.frequency_hz=0);
    BAD_CONFIG(config.bus.frequency_hz=400001); BAD_CONFIG(config.bus.mode=1);
    for(unsigned i=0;i<3;++i) BAD_CONFIG(config.bus.reserved[i]=1);
    BAD_CONFIG(config.bus.sda=-1); BAD_CONFIG(config.bus.sda=49);
    BAD_CONFIG(config.bus.scl=-1); BAD_CONFIG(config.bus.scl=49);
    BAD_CONFIG(config.bus.scl=config.bus.sda); BAD_CONFIG(config.bus.sclk=1);
    BAD_CONFIG(config.bus.mosi=1); BAD_CONFIG(config.bus.miso=1);
}
static void claims_and_transfers(void) {
    fresh(); assert(!t5_driver_get(1) && t5_driver_get(2)==&descriptor);
    assert(descriptor.start(deps,3) && !descriptor.start(deps,3));
    uint64_t claims[8], extra=99; uint8_t bytes[512]={0};
    assert(!api.claim_device(NULL,7,&extra) && !extra);
    assert(!api.claim_device(NULL,120,&extra) && !api.claim_device(NULL,8,NULL));
    for(unsigned i=0;i<8;++i)assert(api.claim_device(NULL,(uint8_t)(8+i),&claims[i]));
    assert(!api.claim_device(NULL,8,&extra) && !extra);
    assert(!api.claim_device(NULL,22,&extra) && !extra);
    unsigned before=closes; assert(!descriptor.quiesce() && closes==before && started);
    unsigned count=transfers;
    assert(!api.transact(NULL,0,bytes,1,NULL,0,1));
    assert(!api.transact(NULL,claims[0],NULL,0,NULL,0,1));
    assert(!api.transact(NULL,claims[0],NULL,1,NULL,0,1));
    assert(!api.transact(NULL,claims[0],bytes,1,NULL,1,1));
    assert(!api.transact(NULL,claims[0],bytes,513,NULL,0,1));
    assert(!api.transact(NULL,claims[0],NULL,0,bytes,513,1));
    assert(!api.transact(NULL,claims[0],bytes,1,NULL,0,0));
    assert(!api.transact(NULL,claims[0],bytes,1,NULL,0,1001));
    assert(!api.transact(NULL,UINT64_MAX,bytes,1,NULL,0,1) && transfers==count);
    reenter=true; assert(api.transact(NULL,claims[7],bytes,512,bytes,512,1000));reenter=false;
    assert(bytes[511]==0xa5); transferred=false;
    assert(!api.transact(NULL,claims[0],bytes,1,NULL,0,1));transferred=true;
    owned=false;assert(!api.transact(NULL,claims[0],bytes,1,NULL,0,1));
    assert(!api.release_device(NULL,claims[0]) && !descriptor.quiesce());owned=true;
    lock_fails=1;assert(!api.claim_device(NULL,99,&extra) && !extra);
    for(unsigned i=0;i<8;++i)assert(api.release_device(NULL,claims[i]));
    assert(!api.release_device(NULL,claims[0]));
    assert(!api.transact(NULL,claims[0],bytes,1,NULL,0,1));
    assert(descriptor.quiesce());fresh();assert(descriptor.start(deps,3));
    assert(api.claim_device(NULL,8,&extra) && extra>claims[7]);
    assert(!api.release_device(NULL,claims[0]));assert(api.release_device(NULL,extra));
    assert(descriptor.quiesce());
}
static void failure_custody(void) {
    for(unsigned f=1;f<=3;++f) {
        fresh();create_fault=f;unsigned before=opens;
        assert(!descriptor.start(deps,3) && opens==before && !mutex_token && !native_token);
        fresh();open_fault=f;assert(!descriptor.start(deps,3) && !mutex_token && !native_token);
    }
    fresh();create_fault=3;destroy_fails=2;
    assert(!descriptor.start(deps,3) && mutex_token && !native_token);
    assert(!descriptor.start(deps,3) && !descriptor.quiesce() && mutex_token);
    assert(descriptor.quiesce() && !mutex_token);
    fresh();lock_fails=2;assert(!descriptor.start(deps,3) && mutex_token && !native_token);
    assert(descriptor.quiesce());
    fresh();open_fault=3;close_fails=2;
    assert(!descriptor.start(deps,3) && native_token && mutex_token && !started);
    assert(!descriptor.start(deps,3) && !descriptor.quiesce() && native_token);
    assert(descriptor.quiesce() && !native_token && !mutex_token);
    fresh();assert(descriptor.start(deps,3));close_fails=2;
    assert(!descriptor.quiesce() && native_token && !started);
    descriptor.stop();assert(native_token && mutex_token);
    assert(descriptor.quiesce());
    fresh();assert(descriptor.start(deps,3));destroy_fails=2;
    assert(!descriptor.quiesce() && !native_token && mutex_token);
    assert(!descriptor.start(deps,3) && !descriptor.quiesce() && mutex_token);
    assert(descriptor.quiesce());
    fresh();unlock_fails=3;assert(!descriptor.start(deps,3) && held && native_token);
    assert(!descriptor.quiesce() && held);assert(descriptor.quiesce());
    fresh();assert(descriptor.start(deps,3));uint64_t token=99;unlock_fails=1;
    assert(!api.claim_device(NULL,50,&token) && !token && held && !started);
    assert(descriptor.quiesce());
    fresh();assert(descriptor.start(deps,3));assert(api.claim_device(NULL,50,&token));
    unlock_fails=1;assert(!api.release_device(NULL,token) && held && !started);
    assert(!descriptor.quiesce() && native_token);assert(api.release_device(NULL,token));
    assert(descriptor.quiesce());
    fresh();assert(descriptor.start(deps,3));assert(api.claim_device(NULL,50,&token));
    uint8_t tx=0;unlock_fails=1;assert(!api.transact(NULL,token,&tx,1,NULL,0,1) && held);
    unsigned before=transfers;assert(!api.transact(NULL,token,&tx,1,NULL,0,1) && transfers==before);
    assert(api.release_device(NULL,token) && descriptor.quiesce());
    fresh();assert(descriptor.start(deps,3));serial=UINT64_MAX;
    assert(!api.claim_device(NULL,50,&token) && !token);assert(descriptor.quiesce());
}
int main(void) {
    invalid_dependencies();invalid_hardware();claims_and_transfers();failure_custody();
    puts("Scoped I2C: typed dependencies, generalized configuration, contention, bounds, stale tokens, retained failure cleanup PASS");
}
#else
/* Real SDK, JSON board admission and CPU-scoped tables. Only the lowest
 * native hardware boundary is modeled, never a fake provider or firmware ABI. */
#include "bootstrap/Runtime.h"
#define private public
#include "ports/esp32s3/CpuPort.h"
#undef private
#include "RiscI2cBusV1.h"
#include "RuntimeFileFixture.h"
#include <cassert>
#include <cstdio>
#include <string>
using namespace RiscCpu;
static Port *active_port;
static const risc_driver_v2 *driver;
static const risc_i2c_bus_api_v1 *bus;
static bool owned=true, close_failure;
static unsigned opens, transfers, closes;
static uint64_t child;
static bool owner() { return owned; }
static bool bind(RiscBoot::Runtime &r) { return active_port->bind(r); }
static Hardware hardware() {
    Hardware h{}; h.owner=owner; h.now=[]()->uint64_t{return 0;};h.sleep=[](uint32_t){};
    h.gpioOpen=[](uint8_t,bool,bool,bool){assert(false);return false;};
    h.gpioWrite=[](uint8_t,bool){assert(false);return false;};
    h.gpioRead=[](uint8_t,bool*){assert(false);return false;};
    h.gpioPwm=[](uint8_t,uint32_t,uint16_t,uint16_t){assert(false);return false;};
    h.gpioClose=[](uint8_t){assert(false);return false;};
    h.i2cOpen=[](uint8_t physical,uint8_t sda,uint8_t scl,uint32_t hz){
        assert(physical==0 && sda==8 && scl==9 && hz==100000);++opens;return true;
    };
    h.i2cTransfer=[](uint8_t physical,uint8_t address,const uint8_t *tx,size_t tn,
                     uint8_t *rx,size_t rn,uint32_t ms){
        assert(physical==0 && address==0x35 && tx && tn==1 && rx && rn==1 && ms==31);
        ++transfers;assert(!active_port->appExitSafe());
        uint64_t nested=99;
        assert(!bus->claim_device(nullptr,0x36,&nested) && !nested);
        assert(!bus->release_device(nullptr,child) && !driver->quiesce());
        *rx=*tx;return true;
    };
    h.i2cClose=[](uint8_t physical){
        assert(physical==0);++closes;if(close_failure)return false;return true;
    };
    h.spiOpen=[](uint8_t,int16_t,int16_t,int16_t){assert(false);return false;};
    h.spiBegin=[](uint8_t,uint8_t,uint32_t,uint8_t,uint32_t){assert(false);return false;};
    h.spiTransfer=[](uint8_t,const uint8_t*,uint8_t*,size_t,uint32_t){assert(false);return false;};
    h.spiEnd=[](uint8_t,uint8_t,uint32_t){assert(false);return false;};
    h.spiClose=[](uint8_t){assert(false);return false;};return h;
}
int main(int argc,char **argv) {
    assert(argc==2);const std::string root=argv[1];
    Port port(hardware());active_port=&port;
    RiscTest::FileSources files({root.c_str()});
    RiscBoot::Runtime runtime({owner,[](risc_runtime_health_v1*){return true;},
        [](uint32_t){},[](const char*){return true;},bind});
    assert(files.bind(runtime));
    if(!runtime.prepare(root.c_str())) {std::fprintf(stderr,"prepare: %s\n",runtime.error());assert(false);}
    assert(port.i2cCount_==1 && port.syncCount_==1 && !opens && !transfers && !closes);
    const auto *selected=runtime.board().device(27);
    assert(selected && runtime.selected(27) && !runtime.selected(28));
    risc_provider_dependency_v1 deps[]={
        {"hardware.device",1,&selected->hardware},
        {"platform.i2c.controller",1,&port.i2cs_[0].api},
        {"platform.sync",1,&port.syncs_[0].api}};
    driver=t5_driver_get(2);assert(driver);bus=static_cast<const risc_i2c_bus_api_v1*>(driver->capability);
    auto changed=selected->hardware;auto changed_config=selected->config.i2cController;
    changed_config.bus.sda=11;changed.config=&changed_config;deps[0].api=&changed;
    // A structurally valid configuration cannot escape the native device scope.
    assert(!driver->start(deps,3) && !opens && port.quiescent());
    deps[0].api=&selected->hardware;assert(driver->start(deps,3) && opens==1);
    assert(!port.quiescent() && port.appExitSafe());
    assert(bus->claim_device(nullptr,0x35,&child));
    assert(!driver->quiesce() && !closes);
    uint8_t tx=0x8a,rx=0;
    assert(bus->transact(nullptr,child,&tx,1,&rx,1,31) && rx==tx && transfers==1);
    owned=false;assert(!bus->release_device(nullptr,child) && !driver->quiesce());owned=true;
    assert(bus->release_device(nullptr,child));close_failure=true;
    assert(!driver->quiesce() && !port.quiescent() && closes==1);
    assert(!driver->start(deps,3));close_failure=false;
    assert(driver->quiesce() && port.quiescent() && closes==2);
    assert(driver->start(deps,3));uint64_t newer=0;
    assert(bus->claim_device(nullptr,0x35,&newer) && newer>child);
    assert(!bus->release_device(nullptr,child));
    assert(bus->release_device(nullptr,newer) && driver->quiesce() && port.quiescent());
    puts("Scoped I2C: real Runtime JSON + CPU Port scoped controller/sync, ownership and retained cleanup PASS");
}
#endif

/* Native typed/owner adaptation of Reader9c8783 TPS65185 source.
 * TPS65185 single register owner and EPD power sequence. Owns only its
 * PCA display pins. It never handles button or radio rail registers. */
#include "RiscProviderV2.h"
#include "RiscPanelLeaseV1.h"
#include "RiscTps65185ProfileV1.h"
#include "RiscProviderSyncV1.h"
#include "TWatchHardwareV1.h"
#include "RiscGpioExpanderV1.h"
#include "RiscI2cBusV1.h"
#include "RiscPlatformClockWaitV1.h"
#include <string.h>
static const risc_i2c_bus_api_v1 *bus;
static const risc_gpio_expander_api_v1 *gpio;
static const risc_platform_clock_wait_v1 *clock_api;
static const risc_provider_sync_api_v1 *sync_api;
static risc_tps65185_profile_api_v1 profile;
static uint64_t state_lock;
static bool unlock_failed,sleep_fence,sleep_dirty;
static uint64_t chip,pins,lease,serial;
static bool active,faulted;
static bool leave(void){
    if(!sync_api||!state_lock||!sync_api->unlock(sync_api->context,state_lock)){
        unlock_failed=true;active=false;return false;
    }
    unlock_failed=false;return true;
}
static bool enter(void){
    if(!sync_api||!state_lock)return false;
    if(unlock_failed&&!leave())return false;
    return sync_api->try_lock(sync_api->context,state_lock);
}
static bool is_owner(void*context){(void)context;return sync_api&&sync_api->is_owner(sync_api->context);}
static uint64_t now(void){return clock_api->base.monotonic_ms(clock_api->base.context);}
static bool wait_one(void){
    const uint64_t began=now();
    if(began==UINT64_MAX)return false;
    for(unsigned i=0;i<4;++i){
        if(!clock_api->scheduler_wait_ms(clock_api->base.context,1))return false;
        uint64_t after=now();if(after==UINT64_MAX||after<began)return false;
        if(after-began>=1)return true;
    }
    return false;
}
static bool outputs(uint16_t mask,uint16_t value){
    bool okay=gpio->write(gpio->context,pins,mask,value);
    if(!okay)faulted=true;
    return okay;
}
static bool write_register(uint8_t reg,const uint8_t*data,size_t n){
    if(n>2)return false;
    uint8_t bytes[3]={reg,0,0};memcpy(bytes+1,data,n);
    bool okay=bus->transact(bus->context,chip,bytes,n+1,NULL,0,50);
    if(!okay)faulted=true;
    return okay;
}
static bool ready(bool internal){
    const uint64_t began=now();if(began==UINT64_MAX)return false;
    // Both work and elapsed bounds; every unsuccessful read cooperates.
    for(unsigned i=0;i<400;++i){
        bool okay;
        if(internal){
            const uint8_t reg=0x0f;uint8_t value=0;
            okay=bus->transact(bus->context,chip,&reg,1,&value,1,50) && (value&0xfa)==0xfa;
        }else{
            uint16_t value=0;
            okay=gpio->read(gpio->context,pins,&value) && (value&profile.power_good);
        }
        if(okay)return true;
        uint64_t current=now();if(current==UINT64_MAX||current<began||current-began>=400)return false;
        if(!wait_one())return false;
    }
    return false;
}
static bool power_off(void){
    // Preserve the established OE/mode/PWRUP/VCOM-before-profile.wakeup order.
    if(!outputs(profile.outputs&~profile.wakeup,0))return false;
    if(!wait_one())return false;
    return outputs(profile.wakeup,0);
}
static bool acquire(void*ctx,uint64_t*out){
    (void)ctx;if(out)*out=0;
    if(!out||!enter())return false;
    bool okay=active&&!faulted&&!sleep_fence&&!lease&&serial!=UINT64_MAX;
    if(okay){
        lease=++serial;*out=lease; // Pin partial power-up before any write.
        okay=outputs(profile.outputs,profile.outputs);
        if(okay)okay=wait_one()&&ready(false);
        const uint16_t code=profile.vcom_millivolts/10u;
        const uint8_t enable=0x3f,vcom[2]={(uint8_t)code,(uint8_t)(code>>8)};
        if(okay)okay=write_register(0x01,&enable,1);
        if(okay)okay=write_register(0x03,vcom,2);
        if(okay)okay=ready(true);
        // Read timeout can recover through confirmed safe outputs. A failed
        // write remains quarantined even if the subsequent shutdown succeeds.
        if(!okay){
            bool off=power_off();
            if(off&&!faulted){lease=0;*out=0;}
        }
    }
    const bool unlocked=leave();return okay&&unlocked;
}
static bool release(void*ctx,uint64_t token){
    (void)ctx;if(!enter())return false;
    bool okay=chip&&pins&&token&&token==lease;
    if(okay)okay=power_off()&&!faulted;
    if(okay)lease=0;
    const bool unlocked=leave();if(okay&&!unlocked)lease=token;return okay&&unlocked;
}
static bool prepare_sleep(void*context){
    (void)context;if(!enter())return false;
    bool okay=active&&!faulted;
    if(okay){sleep_fence=true;okay=!lease;if(okay){sleep_dirty=true;okay=power_off();if(okay)sleep_dirty=false;}}
    const bool unlocked=leave();return okay&&unlocked;
}
static bool resume(void*context){
    (void)context;if(!enter())return false;
    bool okay=active&&!faulted;
    if(okay&&sleep_dirty){okay=power_off();if(okay)sleep_dirty=false;}
    if(okay)sleep_fence=false;
    const bool unlocked=leave();return okay&&unlocked;
}
static bool equal(const char *a,const char *b){return a&&b&&!strcmp(a,b);}
static const void *dependency(const risc_provider_dependency_v1 *d,size_t n,
                              const char *id,size_t size){
    const void *found=NULL;
    for(size_t i=0;i<n;++i){
        if(!equal(d[i].capability_id,id))continue;
        if(found||!d[i].api||d[i].api_version!=1)return NULL;
        const uint32_t *h=d[i].api;
        if(h[0]!=1||h[1]<size)return NULL;
        found=d[i].api;
    }
    return found;
}
static bool admitted(const risc_hardware_device_v1 *d){
    if(!d||!d->instance_id||!equal(d->compatible,"ti,tps65185")||
       !equal(d->revision,"unspecified")||!equal(d->config_type,"peripheral.i2c")||
       d->config_version!=1||d->config_size!=sizeof(tw_hw_i2c_device_v1)||!d->config)return false;
    const tw_hw_i2c_device_v1 *c=d->config;const risc_hw_bus_v1 *b=&c->bus;
    return c->struct_size==sizeof(*c)&&!c->reserved&&!c->chip_id&&
        c->address==0x68&&c->irq==-1&&!c->irq_active_high&&!c->irq_pull_up&&
        b->struct_size==sizeof(*b)&&b->kind==RISC_HW_BUS_I2C&&b->instance_id&&
        b->controller<=1&&b->frequency_hz&&b->frequency_hz<=400000&&!b->mode&&
        !b->reserved[0]&&!b->reserved[1]&&!b->reserved[2]&&
        b->sda>=0&&(uint16_t)b->sda<=RISC_HW_MAX_GPIO&&b->scl>=0&&(uint16_t)b->scl<=RISC_HW_MAX_GPIO&&b->sda!=b->scl&&
        b->sclk==-1&&b->mosi==-1&&b->miso==-1;
}
static bool configure(const risc_tps65185_profile_api_v1 *p){
    if(!p||!p->outputs||!p->inputs||(p->outputs&p->inputs)||p->reserved||
       !p->wakeup||(p->wakeup&(p->wakeup-1))||(p->wakeup&~p->outputs)||
       !(p->outputs&~p->wakeup)||!p->power_good||(p->power_good&(p->power_good-1))||
       (p->power_good&~p->inputs)||p->vcom_millivolts>5110||p->vcom_millivolts%10)return false;
    profile=*p;return true;
}
static bool quiesce_locked(void){
    if(lease||faulted||sleep_fence||sleep_dirty)return false;
    active=false;
    if(pins){if(!gpio||!gpio->release(gpio->context,pins))return false;pins=0;}
    if(chip){if(!bus||!bus->release_device(bus->context,chip))return false;chip=0;}
    return true;
}
static bool quiesce(void){
    if(!sync_api)return !state_lock&&!bus&&!gpio&&!chip&&!pins&&!active;
    if(state_lock){
        if(!enter())return false;
        const bool cleaned=quiesce_locked();const bool unlocked=leave();
        if(!cleaned||!unlocked)return false;
        if(!sync_api->destroy(sync_api->context,state_lock))return false;
        state_lock=0;
    }else if(!quiesce_locked())return false;
    bus=NULL;gpio=NULL;clock_api=NULL;sync_api=NULL;unlock_failed=false;return true;
}
static bool start(const risc_provider_dependency_v1*d,size_t n){
    if(active||chip||pins||lease||sync_api||state_lock||bus||gpio||clock_api||faulted||sleep_fence||!d||n!=6)return false;
    const risc_hardware_device_v1*selected=dependency(d,n,"hardware.device",sizeof(*selected));
    const risc_i2c_bus_api_v1*b=dependency(d,n,"i2c.bus",sizeof(*b));
    const risc_gpio_expander_api_v1*g=dependency(d,n,"gpio.expander",sizeof(*g));
    const risc_platform_clock_api_v1*c=dependency(d,n,"platform.clock",sizeof(*c));
    const risc_platform_clock_wait_v1*w=risc_platform_clock_wait_from_v1(c);
    const risc_provider_sync_api_v1*lock=dependency(d,n,"platform.sync",sizeof(*lock));
    const risc_tps65185_profile_api_v1*p=dependency(d,n,RISC_TPS65185_PROFILE_CAPABILITY,sizeof(*p));
    if(!admitted(selected)||!b||!b->claim_device||!b->transact||!b->release_device||
       !g||!g->claim||!g->read||!g->write||!g->release||!w||!c->monotonic_ms||
       !lock||!lock->is_owner||!lock->create||!lock->try_lock||!lock->unlock||!lock->destroy||
       !lock->is_owner(lock->context)||!configure(p))return false;
    bus=b;gpio=g;clock_api=w;sync_api=lock;
    bool created=sync_api->create(sync_api->context,&state_lock);
    if(!created||!state_lock||!enter()){(void)quiesce();return false;}
    const tw_hw_i2c_device_v1*config=selected->config;
    bool okay=bus->claim_device(bus->context,config->address,&chip)&&chip&&
        gpio->claim(gpio->context,profile.outputs|profile.inputs,profile.inputs,0,&pins)&&pins;
    active=okay;if(!leave())okay=false;if(!okay)(void)quiesce();return okay;
}
static void stop(void){(void)quiesce();}
static const risc_panel_lease_api_v1 api={1,sizeof(api),NULL,is_owner,acquire,release,prepare_sleep,resume};
static const risc_driver_v2 driver={2,sizeof(driver),"tps65185-power",RISC_PANEL_LEASE_CAPABILITY,1,&api,start,stop,quiesce};
__attribute__((visibility("default")))
const risc_driver_v2*t5_driver_get(uint32_t abi){return abi==2?&driver:NULL;}

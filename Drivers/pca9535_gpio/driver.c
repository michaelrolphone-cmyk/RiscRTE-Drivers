/* Native typed/owner adaptation of Reader9c8783 PCA9535 (see source-map.json).
 * Sole PCA9535 register owner. A display, button or radio consumer receives
 * a nonoverlapping pin grant, never the underlying address/register access. */
#include "RiscGpioExpanderV1.h"
#include "RiscI2cBusV1.h"
#include "RiscProviderSyncV1.h"
#include "TWatchHardwareV1.h"
#include <stddef.h>
#include <string.h>
#define MAX_GRANTS 8u
static const risc_i2c_bus_api_v1 *bus;
static const risc_provider_sync_api_v1 *sync_api;
static uint64_t state_lock;
static bool unlock_failed;
static uint64_t claim, next_token;
static bool started, uncertain;
typedef struct { uint64_t token; uint16_t pins, inputs, safe; } pin_grant;
static pin_grant grants[MAX_GRANTS];
/* Owner-only native guard. No atomics in provider PSRAM or nested polling. */
static bool leave(void) {
    if (!sync_api || !state_lock || !sync_api->unlock(sync_api->context,state_lock)) {
        unlock_failed=true;started=false;return false;
    }
    unlock_failed=false;return true;
}
static bool enter(void) {
    if (!sync_api || !state_lock) return false;
    if (unlock_failed && !leave()) return false;
    return sync_api->try_lock(sync_api->context,state_lock);
}
static pin_grant *find(uint64_t token) {
    for (unsigned i=0;token && i<MAX_GRANTS;++i)
        if(grants[i].token==token) return &grants[i];
    return NULL;
}
static bool read_register(uint8_t reg,uint16_t *value) {
    uint8_t data[2];
    if (!bus->transact(bus->context,claim,&reg,1,data,2,50)) return false;
    *value=(uint16_t)(data[0]|((uint16_t)data[1]<<8)); return true;
}
/* Caller owns serialization across the entire RMW / claim / release. */
static bool modify(uint8_t reg,uint16_t mask,uint16_t bits) {
    if (!mask) return true;
    uint16_t old;
    if (!read_register(reg,&old)) return false;
    const uint16_t next=(old&~mask)|(bits&mask);
    if (next==old) return true;
    uint8_t data[3]={reg,(uint8_t)next,(uint8_t)(next>>8)};
    bool okay=bus->transact(bus->context,claim,data,3,NULL,0,50);
    if (!okay) uncertain=true; // Never retry an unconfirmed physical write.
    return okay;
}
static bool claim_pins(void *context,uint16_t pins,uint16_t inputs,uint16_t safe,uint64_t *out) {
    (void)context;
    if(out) *out=0;
    if(!out || !pins || (inputs&~pins) || (safe&~(pins&~inputs)) || !enter()) return false;
    pin_grant *free_slot=NULL;
    bool okay=started && !uncertain && next_token!=UINT64_MAX;
    for(unsigned i=0;i<MAX_GRANTS;++i) {
        if(grants[i].token && (grants[i].pins&pins)) okay=false;
        if(!grants[i].token && !free_slot) free_slot=&grants[i];
    }
    if(okay && free_slot) {
        *free_slot=(pin_grant){++next_token,pins,inputs,safe};
        // Retain even a failed setup: some pin changes may have completed.
        *out=free_slot->token;
        okay=modify(2,pins&~inputs,safe) && modify(6,pins,inputs);
    } else okay=false;
    const bool unlocked=leave();return okay && unlocked;
}
static bool read_levels(void *context,uint64_t token,uint16_t *value) {
    (void)context;
    if (!value || !enter()) return false;
    pin_grant *grant=find(token); uint16_t levels;
    bool okay=started && !uncertain && grant && read_register(0,&levels);
    if(okay) *value=levels&grant->pins;
    const bool unlocked=leave();return okay && unlocked;
}
static bool write_levels(void *context,uint64_t token,uint16_t mask,uint16_t bits) {
    (void)context;
    if(!mask || !enter()) return false;
    pin_grant *grant=find(token);
    bool okay=started && !uncertain && grant && !(mask&~(grant->pins&~grant->inputs));
    if(okay) okay=modify(2,mask,bits);
    const bool unlocked=leave();return okay && unlocked;
}
static bool release_pins(void *context,uint64_t token) {
    (void)context;
    if(!enter()) return false;
    pin_grant *grant=find(token);
    bool okay=!uncertain && grant;
    if(okay) okay=modify(2,grant->pins&~grant->inputs,grant->safe);
    pin_grant saved={0};
    if(okay){saved=*grant;memset(grant,0,sizeof(*grant));}
    const bool unlocked=leave();
    if(okay && !unlocked)*grant=saved;
    return okay && unlocked;
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
    if(!d||!d->instance_id||!equal(d->compatible,"nxp,pca9535")||
       !equal(d->revision,"unspecified")||!equal(d->config_type,"peripheral.i2c")||
       d->config_version!=1||d->config_size!=sizeof(tw_hw_i2c_device_v1)||!d->config)return false;
    const tw_hw_i2c_device_v1 *c=d->config;const risc_hw_bus_v1 *b=&c->bus;
    return c->struct_size==sizeof(*c)&&!c->reserved&&!c->chip_id&&
        c->address>=0x20&&c->address<=0x27&&c->irq==-1&&!c->irq_active_high&&!c->irq_pull_up&&
        b->struct_size==sizeof(*b)&&b->kind==RISC_HW_BUS_I2C&&b->instance_id&&
        b->controller<=1&&b->frequency_hz&&b->frequency_hz<=400000&&!b->mode&&
        !b->reserved[0]&&!b->reserved[1]&&!b->reserved[2]&&
        b->sda>=0&&(uint16_t)b->sda<=RISC_HW_MAX_GPIO&&b->scl>=0&&(uint16_t)b->scl<=RISC_HW_MAX_GPIO&&b->sda!=b->scl&&
        b->sclk==-1&&b->mosi==-1&&b->miso==-1;
}
static bool quiesce_locked(void){
    if(uncertain)return false;
    for(unsigned i=0;i<MAX_GRANTS;++i)if(grants[i].token)return false;
    started=false; /* Cleanup-only after any attempted teardown. */
    if(claim){if(!bus||!bus->release_device(bus->context,claim))return false;claim=0;}
    return true;
}
static bool quiesce(void){
    if(!sync_api)return !state_lock&&!bus&&!claim&&!started;
    if(state_lock){
        if(!enter())return false;
        const bool cleaned=quiesce_locked();const bool unlocked=leave();
        if(!cleaned||!unlocked)return false;
        if(!sync_api->destroy(sync_api->context,state_lock))return false;
        state_lock=0;
    }else if(!quiesce_locked())return false;
    bus=NULL;sync_api=NULL;unlock_failed=false;return true;
}
static bool start(const risc_provider_dependency_v1 *d,size_t n){
    if(started||claim||bus||sync_api||state_lock||uncertain||!d||n!=3)return false;
    const risc_hardware_device_v1 *selected=dependency(d,n,"hardware.device",sizeof(*selected));
    const risc_i2c_bus_api_v1 *selected_bus=dependency(d,n,"i2c.bus",sizeof(*selected_bus));
    const risc_provider_sync_api_v1 *selected_sync=dependency(d,n,"platform.sync",sizeof(*selected_sync));
    if(!admitted(selected)||!selected_bus||!selected_bus->claim_device||!selected_bus->transact||
       !selected_bus->release_device||!selected_sync||!selected_sync->is_owner||!selected_sync->create||
       !selected_sync->try_lock||!selected_sync->unlock||!selected_sync->destroy||
       !selected_sync->is_owner(selected_sync->context))return false;
    bus=selected_bus;sync_api=selected_sync;
    bool created=sync_api->create(sync_api->context,&state_lock);
    if(!created||!state_lock||!enter()){(void)quiesce();return false;}
    const tw_hw_i2c_device_v1 *config=selected->config;uint16_t probe;
    bool okay=bus->claim_device(bus->context,config->address,&claim)&&claim&&read_register(6,&probe);
    started=okay;
    if(!leave())okay=false;
    if(!okay)(void)quiesce();
    return okay;
}
static void stop(void) { (void)quiesce(); }
static const risc_gpio_expander_api_v1 api={1,sizeof(api),NULL,claim_pins,read_levels,write_levels,release_pins};
static const risc_driver_v2 driver={2,sizeof(driver),"pca9535-gpio","gpio.expander",1,&api,start,stop,quiesce};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) { return abi==2?&driver:NULL; }

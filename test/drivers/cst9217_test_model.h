#ifndef CST9217_TEST_MODEL_H
#define CST9217_TEST_MODEL_H
/* Test boundary model for the real provider, not a board preparation adapter.
 * Every admitted dependency has its production ABI and its own context. */
#include "RiscTouchV1.h"
#include "RiscI2cBusV1.h"
#include "RiscPlatformClockV1.h"
#include "GardenPlatformV1.h"
#include "RiscTouchI2cV2.h"
#include "RiscProviderSyncV1.h"
#include "RiscPowerRailV1.h"
#include <assert.h>
#include <string.h>
enum cst_test_operation {
    CST_TEST_OWNER, CST_TEST_CREATE, CST_TEST_LOCK, CST_TEST_UNLOCK, CST_TEST_DESTROY,
    CST_TEST_SUPPLY, CST_TEST_RESET_CLAIM, CST_TEST_RESET_READ, CST_TEST_RESET_WRITE,
    CST_TEST_RESET_DELAY, CST_TEST_RESET_RELEASE, CST_TEST_TOUCH_CLAIM,
    CST_TEST_TRANSFER, CST_TEST_TOUCH_RELEASE, CST_TEST_CLOCK, CST_TEST_OP_COUNT
};
enum cst_test_fault { CST_TEST_OK, CST_TEST_FAIL, CST_TEST_FALSE_RETAINED,
                      CST_TEST_TRUE_ZERO, CST_TEST_WRONG };
typedef struct {
    uint64_t clock_ms, serial, bus_token, reset_token, sync_token, native_token;
    uint64_t selected_at, reset_asserted_at, reset_released_at;
    bool reset_high, reset_recovered, sync_held, reenter;
    bool asleep, wrong_mode, stalled_delay;
    unsigned command_busy_transfers;
    unsigned sleeps, wake_resets;
    uint8_t mode;
    bool fail_bus_release, fail_reset_release, fail_sync_unlock, fail_sync_destroy;
    unsigned counts[CST_TEST_OP_COUNT], fail_nth;
    enum cst_test_operation fail_op;
    enum cst_test_fault fault;
    uint16_t reg, width, height;
    uint8_t raw[10];
    const risc_touch_api_v1 *touch;
    risc_i2c_bus_api_v1 bus;
    risc_platform_clock_api_v1 clock;
    garden_gpio_v1 gpio;
    risc_provider_sync_api_v1 sync;
    risc_power_rail_api_v1 rail;
    risc_hw_i2c_touch_v2 config;
    risc_hardware_device_v1 hardware;
    risc_provider_dependency_v1 dependencies[6];
} cst_test_model;
static inline enum cst_test_fault cst_test_record(cst_test_model *m, enum cst_test_operation op) {
    ++m->counts[op];
    return op == m->fail_op && m->counts[op] == m->fail_nth ? m->fault : CST_TEST_OK;
}
static inline void cst_test_fail_at(cst_test_model *m, enum cst_test_operation op,
                                   unsigned relative, enum cst_test_fault fault) {
    m->fail_op=op; m->fail_nth=m->counts[op]+relative; m->fault=fault;
}
static inline bool cst_test_owner(void *context) {
    return cst_test_record(context,CST_TEST_OWNER) != CST_TEST_FAIL;
}
static inline bool cst_test_create(void *context, uint64_t *out) {
    cst_test_model *m=context; *out=0;
    enum cst_test_fault fault=cst_test_record(m,CST_TEST_CREATE);
    assert(!m->sync_token);
    if(fault==CST_TEST_FAIL)return false;
    if(fault==CST_TEST_TRUE_ZERO)return true;
    *out=m->sync_token=++m->serial;
    return fault!=CST_TEST_FALSE_RETAINED;
}
static inline bool cst_test_lock(void *context, uint64_t token) {
    cst_test_model *m=context; assert(token&&token==m->sync_token);
    if(cst_test_record(m,CST_TEST_LOCK)==CST_TEST_FAIL||m->sync_held)return false;
    m->sync_held=true; return true;
}
static inline bool cst_test_unlock(void *context,uint64_t token) {
    cst_test_model *m=context; assert(token&&token==m->sync_token&&m->sync_held);
    if(cst_test_record(m,CST_TEST_UNLOCK)==CST_TEST_FAIL||m->fail_sync_unlock)return false;
    m->sync_held=false; return true;
}
static inline bool cst_test_destroy(void *context,uint64_t token) {
    cst_test_model *m=context; assert(token&&token==m->sync_token&&!m->sync_held);
    if(cst_test_record(m,CST_TEST_DESTROY)==CST_TEST_FAIL||m->fail_sync_destroy)return false;
    m->sync_token=0; return true;
}
static inline bool cst_test_supply(void *context,risc_power_rail_info_v1 *out) {
    cst_test_model *m=context; assert(m->sync_held&&!m->reset_token&&!m->bus_token);
    assert(out&&out->struct_size==sizeof(*out));
    enum cst_test_fault fault=cst_test_record(m,CST_TEST_SUPPLY);
    if(fault==CST_TEST_FAIL)return false;
    *out=(risc_power_rail_info_v1){.struct_size=sizeof(*out),
        .configured_millivolts=fault==CST_TEST_WRONG?3200:3300,
        .enabled=true,.configuration_verified=true};
    return true;
}
static inline bool cst_test_gpio_claim(void *context,uint8_t pin,bool output,bool initial,bool pullup,uint64_t *out) {
    cst_test_model *m=context; assert(pin==2&&output&&!initial&&!pullup);
    assert(m->sync_held&&!m->bus_token&&m->counts[CST_TEST_SUPPLY]); *out=0;
    enum cst_test_fault fault=cst_test_record(m,CST_TEST_RESET_CLAIM);
    if(m->reset_token||fault==CST_TEST_FAIL)return false;
    if(fault==CST_TEST_TRUE_ZERO)return true;
    *out=m->reset_token=++m->serial;
    m->reset_high=false; m->reset_recovered=false; m->reset_asserted_at=m->clock_ms;
    return fault!=CST_TEST_FALSE_RETAINED;
}
static inline bool cst_test_gpio_read(void *context,uint64_t token,bool *out) {
    cst_test_model *m=context; assert(token&&token==m->reset_token&&m->sync_held);
    enum cst_test_fault fault=cst_test_record(m,CST_TEST_RESET_READ);
    if(fault==CST_TEST_FAIL)return false;
    *out=m->reset_high^(fault==CST_TEST_WRONG); return true;
}
static inline bool cst_test_gpio_write(void *context,uint64_t token,bool high) {
    cst_test_model *m=context; assert(token&&token==m->reset_token&&m->sync_held);
    if(high)assert(!m->reset_high&&m->clock_ms-m->reset_asserted_at>=10);
    enum cst_test_fault fault=cst_test_record(m,CST_TEST_RESET_WRITE);
    if(fault==CST_TEST_FAIL)return false;
    m->reset_high=high;
    if(high){m->reset_released_at=m->clock_ms;m->asleep=false;m->mode=0;}
    else{m->reset_asserted_at=m->clock_ms;m->reset_recovered=false;++m->wake_resets;}
    return fault!=CST_TEST_FALSE_RETAINED;
}
static inline bool cst_test_gpio_release(void *context,uint64_t token) {
    cst_test_model *m=context; assert(token&&token==m->reset_token&&m->sync_held&&!m->bus_token);
    if(cst_test_record(m,CST_TEST_RESET_RELEASE)==CST_TEST_FAIL||m->fail_reset_release)return false;
    m->reset_token=0;m->reset_recovered=false;return true;
}
static inline uint64_t cst_test_now(void *context) {
    cst_test_model *m=context;
    return cst_test_record(m,CST_TEST_CLOCK)==CST_TEST_FAIL?UINT64_MAX:m->clock_ms;
}
static inline void cst_test_sleep(void *context,uint32_t milliseconds) {
    cst_test_model *m=context; assert(m->sync_held&&m->reset_token);
    assert(milliseconds && milliseconds<=200);
    enum cst_test_fault fault=milliseconds==2?CST_TEST_OK:cst_test_record(m,CST_TEST_RESET_DELAY);
    if(!m->stalled_delay)m->clock_ms+=fault==CST_TEST_FAIL?milliseconds-1:milliseconds;
    if(m->reset_high && m->clock_ms-m->reset_released_at>=50)m->reset_recovered=true;

    if(m->reenter) {
        m->reenter=false;risc_touch_snapshot_v1 snapshot;
        assert(m->touch&&!m->touch->snapshot(NULL,&snapshot));
    }
}
static inline void cst_test_owned(const cst_test_model *m) {
    assert(m->reset_token&&m->reset_high&&m->reset_recovered&&m->native_token==1);
}
static inline bool cst_test_claim(void *context,uint8_t address,uint64_t *out) {
    cst_test_model *m=context;assert(address==0x5a&&m->sync_held);cst_test_owned(m);*out=0;
    enum cst_test_fault fault=cst_test_record(m,CST_TEST_TOUCH_CLAIM);
    if(m->bus_token||fault==CST_TEST_FAIL)return false;
    if(fault==CST_TEST_TRUE_ZERO)return true;
    *out=m->bus_token=++m->serial;return fault!=CST_TEST_FALSE_RETAINED;
}
static inline bool cst_test_release(void *context,uint64_t token) {
    cst_test_model *m=context;assert(token&&token==m->bus_token&&m->sync_held);cst_test_owned(m);
    if(cst_test_record(m,CST_TEST_TOUCH_RELEASE)==CST_TEST_FAIL||m->fail_bus_release)return false;
    m->bus_token=0;return true;
}
static inline bool cst_test_transact(void *context,uint64_t token,const uint8_t *tx,size_t tx_size,
                                    uint8_t *rx,size_t rx_size,uint32_t timeout) {
    cst_test_model *m=context;assert(token&&token==m->bus_token&&m->sync_held&&timeout==20);
    cst_test_owned(m);assert(!(tx_size&&rx_size));
    enum cst_test_fault fault=cst_test_record(m,CST_TEST_TRANSFER);
    if(fault==CST_TEST_FAIL)return false;
    assert(!m->asleep);
    if(tx_size){
        assert(tx&&tx_size==2&&!rx);m->reg=(uint16_t)((tx[0]<<8)|tx[1]);m->selected_at=m->clock_ms;
        if(m->reg==0xd11e && m->command_busy_transfers){--m->command_busy_transfers;return false;}
        if(m->reg==0xd11e||m->reg==0xd101)m->mode=(uint8_t)m->reg;
        if(m->reg==0xd105){assert(m->mode==1);m->asleep=true;++m->sleeps;}
        return fault!=CST_TEST_FALSE_RETAINED;
    }
    assert(!tx&&rx&&m->clock_ms-m->selected_at>=2);
    if(m->reg==0x0002){assert(rx_size==2||rx_size==4);memset(rx,0,rx_size);rx[1]=m->wrong_mode?0:m->mode;return true;}
    if(m->reg==0xd1f8){assert(rx_size==4);rx[0]=m->width;rx[1]=m->width>>8;rx[2]=m->height;rx[3]=m->height>>8;return true;}
    if(m->reg==0xd1fc||m->reg==0xd204){assert(rx_size==4);memset(rx,0,rx_size);return true;}
    assert(m->reg==0xd000&&rx_size==10);memcpy(rx,m->raw,10);return true;
}
static inline void cst_test_report(cst_test_model *m,unsigned x,unsigned y,bool down) {
    memset(m->raw,0,sizeof(m->raw));m->raw[6]=0xab;m->raw[5]=down;m->raw[0]=down?6:0;
    m->raw[1]=x>>4;m->raw[2]=y>>4;m->raw[3]=(x&15)<<4|(y&15);
}
static inline void cst_test_setup(cst_test_model *m) {
    memset(m,0,sizeof(*m));m->clock_ms=100;m->serial=m->native_token=1;m->width=m->height=466;
    m->bus=(risc_i2c_bus_api_v1){1,sizeof(m->bus),m,cst_test_claim,cst_test_transact,cst_test_release};
    m->clock=(risc_platform_clock_api_v1){1,sizeof(m->clock),m,cst_test_now,cst_test_sleep};
    m->gpio=(garden_gpio_v1){.api_version=1,.struct_size=sizeof(m->gpio),.context=m,
        .claim=cst_test_gpio_claim,.write=cst_test_gpio_write,.read=cst_test_gpio_read,.release=cst_test_gpio_release};
    m->sync=(risc_provider_sync_api_v1){1,sizeof(m->sync),m,cst_test_owner,cst_test_create,cst_test_lock,cst_test_unlock,cst_test_destroy};
    m->rail=(risc_power_rail_api_v1){1,sizeof(m->rail),m,cst_test_supply,NULL};
    m->config=(risc_hw_i2c_touch_v2){.base={.struct_size=sizeof(m->config),
        .bus={.struct_size=sizeof(risc_hw_bus_v1),.kind=RISC_HW_BUS_I2C,.instance_id=101,
              .controller=0,.frequency_hz=400000,.sclk=-1,.mosi=-1,.miso=-1,.sda=15,.scl=14},
        .width=466,.height=466,.address=0x5a,.reset=2,.irq=11,.reset_assert_ms=10,.reset_recovery_ms=50},.power=-1};
    m->hardware=(risc_hardware_device_v1){1,sizeof(m->hardware),6,"hynitron,cst9217","unspecified",
        "touch.i2c",2,sizeof(m->config),&m->config};
    m->dependencies[0]=(risc_provider_dependency_v1){"hardware.device",1,&m->hardware};
    m->dependencies[1]=(risc_provider_dependency_v1){"i2c.bus",1,&m->bus};
    m->dependencies[2]=(risc_provider_dependency_v1){"platform.clock",1,&m->clock};
    m->dependencies[3]=(risc_provider_dependency_v1){"platform.gpio",1,&m->gpio};
    m->dependencies[4]=(risc_provider_dependency_v1){"power.rail",1,&m->rail};
    m->dependencies[5]=(risc_provider_dependency_v1){"platform.sync",1,&m->sync};
    cst_test_report(m,0,0,false);
}
static inline void cst_test_clean(const cst_test_model *m) {
    assert(!m->bus_token&&!m->reset_token&&!m->sync_token&&!m->sync_held&&m->native_token==1);
}
#endif

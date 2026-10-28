/* Real production CST owns its reset and teardown. No caller-only preparation
 * wrapper can bypass the subscription/transfer/release interlock. */
#include "cst9217_test_model.h"
#include <stdio.h>
extern const risc_driver_v2 *t5_driver_get(uint32_t);
static const risc_driver_v2 *driver;
static const risc_touch_api_v1 *touch;
static void setup(cst_test_model *m) {
    assert(driver->quiesce());driver->stop();cst_test_setup(m);m->touch=touch;
}
static bool start(cst_test_model *m) { return driver->start(m->dependencies,6); }
static void finish(cst_test_model *m) {
    m->fault=CST_TEST_OK;m->fail_bus_release=m->fail_reset_release=false;
    m->fail_sync_unlock=m->fail_sync_destroy=false;
    assert(driver->quiesce());driver->stop();cst_test_clean(m);
}
static void no_calls(const cst_test_model *m) {
    for(unsigned i=0;i<CST_TEST_OP_COUNT;i++)assert(!m->counts[i]);
    cst_test_clean(m);
}
static void retry(cst_test_model *m) {
    assert(start(m));assert(touch->poll(NULL,1));finish(m);
}
static void admission(void) {
    cst_test_model m;
    setup(&m);assert(!driver->start(NULL,0));assert(!driver->start(m.dependencies,0));no_calls(&m);
    for(unsigned i=0;i<6;i++) {
        setup(&m);risc_provider_dependency_v1 missing[5];unsigned n=0;
        for(unsigned j=0;j<6;j++)if(i!=j)missing[n++]=m.dependencies[j];
        assert(!driver->start(missing,5));no_calls(&m);
        for(unsigned j=0;j<6;j++)if(i!=j) {
            setup(&m);m.dependencies[i]=m.dependencies[j];assert(!start(&m));no_calls(&m);
        }
        setup(&m);m.dependencies[i].api=NULL;assert(!start(&m));no_calls(&m);
        setup(&m);m.dependencies[i].api_version=2;assert(!start(&m));no_calls(&m);
        setup(&m);m.dependencies[i].capability_id="unknown";assert(!start(&m));no_calls(&m);
        setup(&m);uint32_t *header=(uint32_t *)m.dependencies[i].api;header[0]=2;assert(!start(&m));no_calls(&m);
        setup(&m);header=(uint32_t *)m.dependencies[i].api;header[1]=0;assert(!start(&m));no_calls(&m);
    }
#define REJECT(change) do { setup(&m); change; assert(!start(&m)); no_calls(&m); } while(0)
    REJECT(m.hardware.instance_id=0);REJECT(m.hardware.compatible="hynitron,ft3168");
    REJECT(m.hardware.revision="other");REJECT(m.hardware.config_type="display.spi");
    REJECT(m.hardware.config_version=1);REJECT(m.hardware.config_version=3);
    REJECT(m.hardware.config_size=sizeof(m.config.base));REJECT(m.hardware.config_size++);
    REJECT(m.hardware.config=NULL);REJECT(m.config.base.struct_size=sizeof(m.config.base));
    REJECT(m.config.base.bus.struct_size--);REJECT(m.config.base.bus.kind=RISC_HW_BUS_SPI);
    REJECT(m.config.base.bus.instance_id=102);REJECT(m.config.base.bus.controller=1);
    REJECT(m.config.base.bus.frequency_hz=100000);REJECT(m.config.base.bus.mode=1);
    REJECT(m.config.base.bus.reserved[0]=1);REJECT(m.config.base.bus.reserved[1]=1);
    REJECT(m.config.base.bus.reserved[2]=1);REJECT(m.config.base.bus.sda=14);
    REJECT(m.config.base.bus.scl=15);REJECT(m.config.base.bus.sclk=38);
    REJECT(m.config.base.bus.mosi=4);REJECT(m.config.base.bus.miso=5);
    REJECT(m.config.base.width=480);REJECT(m.config.base.height=800);
    REJECT(m.config.base.address=0x38);REJECT(m.config.base.reset=1);
    REJECT(m.config.base.reset=-1);REJECT(m.config.base.reset_active_high=1);
    REJECT(m.config.base.reset_assert_ms=0);REJECT(m.config.base.reset_assert_ms=11);
    REJECT(m.config.base.reset_recovery_ms=51);REJECT(m.config.base.irq=10);
    REJECT(m.config.base.irq_active_high=1);REJECT(m.config.base.irq_pull_up=1);
    REJECT(m.config.power=2);REJECT(m.config.power_active_high=1);REJECT(m.config.irq_output=1);
    REJECT(m.config.alternate_address=0x14);REJECT(m.config.reserved[0]=1);
    REJECT(m.config.reserved[1]=1);REJECT(m.config.reserved[2]=1);
    REJECT(m.bus.claim_device=NULL);REJECT(m.bus.transact=NULL);REJECT(m.bus.release_device=NULL);
    REJECT(m.clock.monotonic_ms=NULL);REJECT(m.clock.sleep_ms=NULL);
    REJECT(m.gpio.claim=NULL);REJECT(m.gpio.write=NULL);REJECT(m.gpio.read=NULL);REJECT(m.gpio.release=NULL);
    REJECT(m.rail.get_info=NULL);REJECT(m.sync.is_owner=NULL);REJECT(m.sync.create=NULL);
    REJECT(m.sync.try_lock=NULL);REJECT(m.sync.unlock=NULL);REJECT(m.sync.destroy=NULL);
#undef REJECT
    setup(&m);m.gpio.struct_size=offsetof(garden_gpio_v1,release)+sizeof(m.gpio.release);
    assert(start(&m));finish(&m); /* Do not require unrelated append-only GPIO features. */
    puts("  admission: missing/duplicate/invalid dependencies and every typed configuration field reject before I/O PASS");
}
static void start_faults(void) {
    static const struct { enum cst_test_operation op;unsigned nth;enum cst_test_fault fault; } cases[]={
        {CST_TEST_OWNER,1,CST_TEST_FAIL},{CST_TEST_CREATE,1,CST_TEST_FAIL},
        {CST_TEST_CREATE,1,CST_TEST_FALSE_RETAINED},{CST_TEST_CREATE,1,CST_TEST_TRUE_ZERO},
        {CST_TEST_LOCK,1,CST_TEST_FAIL},{CST_TEST_SUPPLY,1,CST_TEST_FAIL},{CST_TEST_SUPPLY,1,CST_TEST_WRONG},
        {CST_TEST_RESET_CLAIM,1,CST_TEST_FAIL},{CST_TEST_RESET_CLAIM,1,CST_TEST_FALSE_RETAINED},
        {CST_TEST_RESET_CLAIM,1,CST_TEST_TRUE_ZERO},{CST_TEST_RESET_READ,1,CST_TEST_FAIL},
        {CST_TEST_RESET_READ,2,CST_TEST_FAIL},{CST_TEST_RESET_READ,1,CST_TEST_WRONG},
        {CST_TEST_RESET_READ,2,CST_TEST_WRONG},{CST_TEST_RESET_WRITE,1,CST_TEST_FAIL},
        {CST_TEST_RESET_DELAY,1,CST_TEST_FAIL},{CST_TEST_RESET_DELAY,2,CST_TEST_FAIL},
        {CST_TEST_TOUCH_CLAIM,1,CST_TEST_FAIL},{CST_TEST_TOUCH_CLAIM,1,CST_TEST_FALSE_RETAINED},
        {CST_TEST_TOUCH_CLAIM,1,CST_TEST_TRUE_ZERO},{CST_TEST_UNLOCK,1,CST_TEST_FAIL}
    };
    for(unsigned i=0;i<sizeof(cases)/sizeof(*cases);i++)for(unsigned retain=0;retain<2;retain++) {
        cst_test_model m;setup(&m);cst_test_fail_at(&m,cases[i].op,cases[i].nth,cases[i].fault);
        m.fail_bus_release=m.fail_reset_release=m.fail_sync_destroy=retain;
        if(cases[i].op==CST_TEST_RESET_DELAY){
            assert(start(&m));finish(&m);retry(&m);continue;
        }
        assert(!start(&m));assert(!touch->subscribe(NULL));
        if(m.sync_token||m.reset_token||m.bus_token) {
            assert(!start(&m));assert(!driver->quiesce());driver->stop();
            if(m.bus_token)assert(m.reset_token&&!m.counts[CST_TEST_RESET_RELEASE]);
        }
        finish(&m);retry(&m);
    }
    for(unsigned n=1;n<=8;n++)for(unsigned retain=0;retain<2;retain++) {
        cst_test_model m;setup(&m);cst_test_fail_at(&m,CST_TEST_TRANSFER,n,CST_TEST_FAIL);
        m.fail_bus_release=retain;assert(!start(&m));assert(!touch->subscribe(NULL));
        if(retain){assert(m.bus_token&&m.reset_token);assert(!driver->quiesce());driver->stop();}
        finish(&m);retry(&m);
    }
    /* Measure the successful source path so newly added settling observations
     * cannot silently move the final post-probe sample outside fault coverage. */
    cst_test_model healthy;setup(&healthy);assert(start(&healthy));
    const unsigned startup_clock_calls=healthy.counts[CST_TEST_CLOCK];finish(&healthy);
    for(unsigned n=1;n<=startup_clock_calls;n++)for(unsigned retain=0;retain<2;retain++) {
        cst_test_model m;setup(&m);cst_test_fail_at(&m,CST_TEST_CLOCK,n,CST_TEST_FAIL);
        m.fail_bus_release=m.fail_reset_release=retain;assert(!start(&m));
        assert(!touch->subscribe(NULL));finish(&m);retry(&m);
    }
    for(unsigned i=0;i<3;i++){cst_test_model m;setup(&m);if(i==0)m.width=0;else if(i==1)m.width=480;else m.height=800;assert(!start(&m));finish(&m);}
    puts("  startup: supply/reset/sync faults, false/nonzero claims, clock failures, every probe transfer and cleanup retry PASS");
}
static void lifecycle(void) {
    cst_test_model m;setup(&m);assert(start(&m));
    const uint64_t reset=m.reset_token;assert(m.reset_released_at-m.reset_asserted_at==10);
    assert(m.selected_at-m.reset_released_at>=50);assert(m.counts[CST_TEST_RESET_CLAIM]==1);
    uint64_t subscription=touch->subscribe(NULL);assert(subscription);
    assert(!driver->quiesce());driver->stop();assert(m.reset_token==reset&&!m.counts[CST_TEST_RESET_RELEASE]);
    for(unsigned i=0;i<12;i++){
        cst_test_report(&m,i*30,465-i*30,true);assert(touch->poll(NULL,1));
        assert(m.reset_token==reset&&!m.counts[CST_TEST_RESET_RELEASE]);
    }
    assert(touch->unsubscribe(NULL,subscription));m.fail_bus_release=true;
    assert(!driver->quiesce());driver->stop();assert(m.bus_token&&m.reset_token==reset&&!m.counts[CST_TEST_RESET_RELEASE]);
    m.fail_bus_release=false;m.fail_reset_release=true;assert(!driver->quiesce());driver->stop();
    assert(!m.bus_token&&m.reset_token==reset&&m.sync_token);assert(!start(&m));
    const unsigned releases=m.counts[CST_TEST_TOUCH_RELEASE];
    m.fail_reset_release=false;m.fail_sync_destroy=true;assert(!driver->quiesce());
    assert(!m.bus_token&&!m.reset_token&&m.sync_token);assert(!start(&m));
    finish(&m);assert(m.counts[CST_TEST_TOUCH_RELEASE]==releases);retry(&m);
    /* A failed unlock retains the owner token and pins; a later cleanup
     * retries unlock before attempting to reacquire, release and destroy. */
    assert(start(&m));m.fail_sync_unlock=true;risc_touch_snapshot_v1 snapshot;
    /* Copying state is not a successful API call when releasing its guard fails.
     * Retain custody and make subsequent reads terminal until cleanup retries. */
    assert(!touch->snapshot(NULL,&snapshot));
    const unsigned locks=m.counts[CST_TEST_LOCK],unlocks=m.counts[CST_TEST_UNLOCK];
    assert(!touch->snapshot(NULL,&snapshot));
    assert(m.counts[CST_TEST_LOCK]==locks&&m.counts[CST_TEST_UNLOCK]==unlocks);
    assert(!driver->quiesce());driver->stop();
    assert(m.sync_held&&m.bus_token&&m.reset_token);finish(&m);retry(&m);
    puts("  teardown: active subscriptions, retained bus/GPIO2/sync custody and retry; independent native GPIO1 untouched PASS");
}
int main(void) {
    driver=t5_driver_get(2);assert(driver&&!t5_driver_get(1));touch=driver->capability;
    admission();start_faults();lifecycle();
    puts("Badge CST production composition: scoped typed admission and provider-owned reset lifetime PASS");
}

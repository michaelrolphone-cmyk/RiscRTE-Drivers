#include "../Drivers/legacy_power_compat/bridge.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
static int context;
static unsigned calls;
#if LEGACY_POWER_ROLE!=2
static bool simple(void *c){assert(c==&context);calls++;return true;}
#endif
#if LEGACY_POWER_ROLE==1
static int32_t light(void *c,risc_light_sleep_result_v1 *out){assert(c==&context);assert(out);calls++;return RISC_LIGHT_SLEEP_RETAINED;}
#elif LEGACY_POWER_ROLE==2
static bool keys(void *c,uint32_t *out){assert(c==&context);calls++;*out=2;return true;}
#endif
int main(void) {
#if LEGACY_POWER_ROLE==3
    legacy_display_power_v1 old={.base={.api_version=1,.struct_size=sizeof(old),.context=&context},.prepare_sleep=simple,.resume=simple};
    size_t base_size=sizeof(old.base),minimum=offsetof(legacy_display_power_v1,prepare_deep_sleep);
#else
    legacy_battery_power_v1 old={.base={.api_version=1,.struct_size=sizeof(old),.context=&context}};
    size_t base_size=sizeof(old.base);
#if LEGACY_POWER_ROLE==1
    old.prepare_sleep=simple;old.resume=simple;old.light_sleep=light;
    size_t minimum=offsetof(legacy_battery_power_v1,deep_sleep);
#else
    old.key_events=keys;size_t minimum=offsetof(legacy_battery_power_v1,prepare_sleep);
#endif
#endif
    risc_provider_dependency_v1 dep={REQUIRED,1,&old};
    assert(!start(NULL,0)&&!start(&dep,0)&&!start(&dep,2));
    /* A short allocation proves the adapter doesn't read nonexistent suffixes. */
    void *base=malloc(base_size);assert(base);memcpy(base,&old,base_size);((uint32_t*)base)[1]=base_size;dep.api=base;
    assert(!start(&dep,1));free(base);dep.api=&old;
    old.base.struct_size=minimum-1;assert(!start(&dep,1));old.base.struct_size=minimum;
    assert(start(&dep,1)&&active&&!calls);assert(!start(&dep,1));
#if LEGACY_POWER_ROLE==1
    risc_light_sleep_result_v1 result={0};assert(api.prepare_sleep(api.context));assert(api.light_sleep(api.context,&result)==RISC_LIGHT_SLEEP_RETAINED);
    assert(api.resume(api.context));assert(!api.deep_sleep&&!api.light_sleep_for&&!api.sleep_wake_pending&&!api.deep_sleep_for&&!api.light_sleep_set&&!api.deep_sleep_set);
#elif LEGACY_POWER_ROLE==2
    uint32_t events;assert(api.events(api.context,&events)&&events==2);
#else
    assert(api.prepare_sleep(api.context)&&api.resume(api.context));assert(!api.prepare_deep_sleep&&!api.resume_status);
#endif
    assert(quiesce());stop();assert(!active&&api.api_version==1&&api.struct_size==sizeof(api));
    old.base.struct_size=sizeof(old);assert(start(&dep,1));stop();
    old.base.api_version=2;assert(!start(&dep,1));old.base.api_version=1;
    dep.capability_id="device.private-power";assert(!start(&dep,1));dep.capability_id=REQUIRED;dep.api_version=2;assert(!start(&dep,1));
    assert(t5_driver_get(2)==&driver&&!t5_driver_get(1));
    puts(PROVIDED ": legacy boundary, exact base-only refusal, optional suffix checks and lifecycle PASS");return 0;
}

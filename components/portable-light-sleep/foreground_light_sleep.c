/* App-linked, board-independent returning suspension. Never an ELF provider.
 * The selected display.power service retains the completed image and restores
 * it at its native geometry before restoring brightness. Do not use acquire()
 * as a snapshot API: display.output does not promise completed pixels there.
 */
#include "PortableForegroundIdle.h"
#include "RiscRuntimeSleepV1.h"
#include "RiscDisplayPowerV1.h"
#include "RiscPowerSleepV1.h"
#include "RiscPowerKeyNeutralizeV1.h"

static bool foreground_idle_retained;

static int foreground_idle_retain(const risc_runtime_api_v1 *rt) {
    foreground_idle_retained=true;
    /* This terminal Runtime fence is the sole permitted operation after an
     * uncertain/retained result. No diagnostic, yield, free or grant release. */
    if(!rt->retain_invocation())for(;;){}
    return -2;
}

static bool foreground_idle_empty(const risc_runtime_capability_v1 *grant) {
    return grant->struct_size==sizeof(*grant) && !grant->api &&
           !grant->slot && !grant->generation;
}

static int foreground_idle_release(const risc_runtime_api_v1 *rt,
                                  risc_runtime_capability_v1 *grants,
                                  unsigned count,int result) {
    while(count) {
        risc_runtime_capability_v1 *grant=&grants[--count];
        if(!rt->release(grant) || !foreground_idle_empty(grant))
            return foreground_idle_retain(rt);
    }
    return result;
}

static bool foreground_idle_ordinary_light(int32_t rc) {
    switch(rc) {
    case RISC_LIGHT_SLEEP_OK:
    case RISC_LIGHT_SLEEP_INVALID:
    case RISC_LIGHT_SLEEP_BUSY:
    case RISC_LIGHT_SLEEP_ACTIVE_WAKE:
    case RISC_LIGHT_SLEEP_PLATFORM:
    case RISC_LIGHT_SLEEP_UNSUPPORTED:return true;
    /* CONTEXT may mean lost owner authority; it is not a clean refusal. */
    default:return false;
    }
}

#ifdef PORTABLE_FOREGROUND_ALARM_SLEEP
static int foreground_idle_sleep(const risc_runtime_api_v1 *rt,
#else
int portable_app_foreground_idle_sleep(const risc_runtime_api_v1 *rt,
#endif
                                      const risc_display_output_api_v1 *display,
                                      const risc_battery_gauge_api_v1 *gauge
#ifdef PORTABLE_FOREGROUND_ALARM_SLEEP
                                      ,const portable_foreground_alarm_hooks *alarm
#endif
                                      ) {
    (void)gauge; /* A gauge is not a power service; it is never cast or polled. */
    if(foreground_idle_retained)return -2;
    /* This opt-in path requires the real native-file sleep transaction. Older
     * tables safely refuse; there is no legacy no-transaction fallback. */
    if(!rt || rt->api_version!=RISC_RUNTIME_API_V1 ||
       rt->struct_size<RISC_RUNTIME_SLEEP_V1_SIZE || !rt->acquire ||
       !rt->release || !rt->retain_invocation || !rt->sleep_prepare ||
       !rt->sleep_resume || !display)return 0;

    static const char *const capabilities[]={RISC_DISPLAY_POWER_CAPABILITY,
        RISC_POWER_SLEEP_CAPABILITY,RISC_POWER_KEY_CAPABILITY};
    risc_runtime_capability_v1 grants[3]={{0}};
    for(unsigned i=0;i<3;++i) {
        grants[i].struct_size=sizeof(grants[i]);
        /* A boolean acquisition failure cannot prove that an external
         * provider's activation rollback released every dependency. */
        if(!rt->acquire(capabilities[i],1,0,&grants[i]) ||
           grants[i].struct_size!=sizeof(grants[i]) || !grants[i].api ||
           !grants[i].slot || !grants[i].generation)
            return foreground_idle_retain(rt);
    }
    const risc_display_power_api_v1 *panel=grants[0].api;
    const risc_power_sleep_api_v1 *sleep=grants[1].api;
    const risc_power_key_neutralize_v1 *key=grants[2].api;
    if(panel->api_version!=RISC_DISPLAY_POWER_API_V1 ||
       panel->struct_size<RISC_DISPLAY_POWER_RESUME_STATUS_SIZE ||
       !panel->prepare_sleep || !panel->resume_status ||
       sleep->api_version!=RISC_POWER_SLEEP_API_V1 ||
       sleep->struct_size<RISC_POWER_LIGHT_SLEEP_SIZE ||
       !sleep->prepare_sleep || !sleep->resume || !sleep->light_sleep ||
       key->base.api_version!=RISC_POWER_KEY_API_V1 ||
       key->base.struct_size<RISC_POWER_KEY_NEUTRALIZE_SIZE ||
       !key->base.events || !key->neutralize)
        return foreground_idle_release(rt,grants,3,0);

    /* The caller keeps its navigation/key dependency grant alive throughout.
     * This gate must outlive our temporary grant, including release/late IRQ. */
    if(!key->neutralize(key->base.context))return foreground_idle_retain(rt);
    uint64_t native_token=0;
    int32_t native_result=risc_runtime_sleep_prepare_v1(rt,&native_token);
    if(native_result!=RISC_RUNTIME_SLEEP_OK) {
        if(native_token || (native_result!=RISC_RUNTIME_SLEEP_BUSY &&
                            native_result!=RISC_RUNTIME_SLEEP_UNSUPPORTED))
            return foreground_idle_retain(rt);
        return foreground_idle_release(rt,grants,3,0);
    }
    if(!native_token)return foreground_idle_retain(rt);

    int32_t result=RISC_LIGHT_SLEEP_INVALID;
    bool sleep_attempted=false;
    bool panel_ready=panel->prepare_sleep(panel->context);
    if(panel_ready) {
        sleep_attempted=true;
        if(sleep->prepare_sleep(sleep->context)) {
            risc_light_sleep_result_v1 wake={.struct_size=sizeof(wake)};
#ifdef PORTABLE_FOREGROUND_ALARM_SLEEP
            uint32_t duration_ms=0;int ready=1;
            if(alarm) {
                ready=alarm->prepare(alarm->context,&duration_ms);
                if((ready!=0 && ready!=1) || (ready==1 && duration_ms>86400000u))
                    return foreground_idle_retain(rt);
            }
            if(ready) {
                if(duration_ms) {
                    if(sleep->struct_size>=RISC_POWER_TIMED_SLEEP_SIZE && sleep->light_sleep_for)
                        result=sleep->light_sleep_for(sleep->context,duration_ms,&wake);
                }else result=sleep->light_sleep(sleep->context,&wake);
            }
#else
            result=sleep->light_sleep(sleep->context,&wake);
#endif
            if(!foreground_idle_ordinary_light(result))return foreground_idle_retain(rt);
            if(result==RISC_LIGHT_SLEEP_OK &&
               (wake.struct_size<sizeof(wake) ||
                (wake.wake_cause!=RISC_LIGHT_SLEEP_WAKE_GPIO &&
                 wake.wake_cause!=RISC_LIGHT_SLEEP_WAKE_OTHER
#ifdef PORTABLE_FOREGROUND_ALARM_SLEEP
                 && !(alarm && wake.wake_cause==RISC_LIGHT_SLEEP_WAKE_TIMER)
#endif
                 )))
                return foreground_idle_retain(rt);
        }
    }
    /* Pair every attempted preparation, including a false prepare result.
     * Stop at the first uncertain cleanup; never try a later restoration. */
    if(sleep_attempted && !sleep->resume(sleep->context))
        return foreground_idle_retain(rt);
    if(panel->resume_status(panel->context)!=RISC_LIGHT_SLEEP_OK)
        return foreground_idle_retain(rt);
    native_result=risc_runtime_sleep_resume_v1(rt,native_token);
    if(native_result!=RISC_RUNTIME_SLEEP_OK &&
       native_result!=RISC_RUNTIME_SLEEP_MEDIA_UNAVAILABLE)
        return foreground_idle_retain(rt);
    if(!key->neutralize(key->base.context))return foreground_idle_retain(rt);
#ifdef PORTABLE_FOREGROUND_ALARM_SLEEP
    int restored=foreground_idle_release(rt,grants,3,result==RISC_LIGHT_SLEEP_OK?1:0);
    if(restored==1 && alarm) {
        int resumed=alarm->resume(alarm->context);
        if(resumed!=0 && resumed!=1)return foreground_idle_retain(rt);
    }
    return restored;
#else
    return foreground_idle_release(rt,grants,3,result==RISC_LIGHT_SLEEP_OK?1:0);
#endif
}

#ifdef PORTABLE_FOREGROUND_ALARM_SLEEP
/* Opt-in deployment-local extension; the original API remains untimed. */
int portable_app_foreground_idle_sleep(const risc_runtime_api_v1 *rt,
        const risc_display_output_api_v1 *display,const risc_battery_gauge_api_v1 *gauge) {
    return foreground_idle_sleep(rt,display,gauge,NULL);
}
int portable_app_foreground_idle_sleep_alarm(const risc_runtime_api_v1 *rt,
        const risc_display_output_api_v1 *display,const risc_battery_gauge_api_v1 *gauge,
        const portable_foreground_alarm_hooks *alarm) {
    if(foreground_idle_retained)return -2;
    if(!alarm || !alarm->prepare || !alarm->resume)return 0;
    return foreground_idle_sleep(rt,display,gauge,alarm);
}
#endif

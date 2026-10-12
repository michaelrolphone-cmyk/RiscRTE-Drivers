/* Transitional adapter, no hardware access, chip identity or pin binding.
 * Its ordinary Runtime dependency keeps the original implementation mapped.
 * No callbacks/tasks or borrowed data outlive the dependent capability grant. */
#include "LegacyPowerAbiV1.h"
#include <string.h>
#define MEMBER_AVAILABLE(object,type,member) ((object)->base.struct_size>=offsetof(type,member)+sizeof((object)->member))
#if LEGACY_POWER_ROLE==1
#define DRIVER_ID "compat-power-sleep"
#define PROVIDED RISC_POWER_SLEEP_CAPABILITY
#define REQUIRED RISC_BATTERY_GAUGE_CAPABILITY
static risc_power_sleep_api_v1 api={.api_version=1,.struct_size=sizeof(api)};
#elif LEGACY_POWER_ROLE==2
#define DRIVER_ID "compat-power-key"
#define PROVIDED RISC_POWER_KEY_CAPABILITY
#define REQUIRED RISC_BATTERY_GAUGE_CAPABILITY
static risc_power_key_api_v1 api={.api_version=1,.struct_size=sizeof(api)};
#elif LEGACY_POWER_ROLE==3
#define DRIVER_ID "compat-display-power"
#define PROVIDED RISC_DISPLAY_POWER_CAPABILITY
#define REQUIRED RISC_DISPLAY_OUTPUT_CAPABILITY
static risc_display_power_api_v1 api={.api_version=1,.struct_size=sizeof(api)};
#else
#error Select one independent capability, not a hardware model.
#endif
static bool active;
static bool start(const risc_provider_dependency_v1 *deps,size_t n) {
    if(active||!deps||n!=1||!deps[0].capability_id||strcmp(deps[0].capability_id,REQUIRED)||deps[0].api_version!=1||!deps[0].api)return false;
#if LEGACY_POWER_ROLE==1
    const legacy_battery_power_v1 *old=deps[0].api;
    if(old->base.api_version!=1||!MEMBER_AVAILABLE(old,legacy_battery_power_v1,light_sleep)||
       !old->prepare_sleep||!old->resume||!old->light_sleep)return false;
    api.context=old->base.context;api.prepare_sleep=old->prepare_sleep;api.resume=old->resume;api.light_sleep=old->light_sleep;
#define COPY_OPTIONAL(member) if(MEMBER_AVAILABLE(old,legacy_battery_power_v1,member))api.member=old->member
    COPY_OPTIONAL(deep_sleep);COPY_OPTIONAL(light_sleep_for);COPY_OPTIONAL(sleep_wake_pending);
    COPY_OPTIONAL(deep_sleep_for);COPY_OPTIONAL(light_sleep_set);COPY_OPTIONAL(deep_sleep_set);
#undef COPY_OPTIONAL
#elif LEGACY_POWER_ROLE==2
    const legacy_battery_power_v1 *old=deps[0].api;
    if(old->base.api_version!=1||!MEMBER_AVAILABLE(old,legacy_battery_power_v1,key_events)||!old->key_events)return false;
    api.context=old->base.context;api.events=old->key_events;
#else
    const legacy_display_power_v1 *old=deps[0].api;
    if(old->base.api_version!=1||!MEMBER_AVAILABLE(old,legacy_display_power_v1,resume)||!old->prepare_sleep||!old->resume)return false;
    api.context=old->base.context;api.prepare_sleep=old->prepare_sleep;api.resume=old->resume;
    if(MEMBER_AVAILABLE(old,legacy_display_power_v1,prepare_deep_sleep))api.prepare_deep_sleep=old->prepare_deep_sleep;
    if(MEMBER_AVAILABLE(old,legacy_display_power_v1,resume_status))api.resume_status=old->resume_status;
#endif
    active=true;return true;
}
/* No owned physical session exists here. The graph subsequently quiesces the
 * dependency; its refusal still prevents physical-driver unloading. */
static bool quiesce(void){return true;}
static void stop(void){memset(&api,0,sizeof(api));api.api_version=1;api.struct_size=sizeof(api);active=false;}
static const risc_driver_v2 driver={2,sizeof(driver),DRIVER_ID,PROVIDED,1,&api,start,stop,quiesce};
__attribute__((visibility("default"))) const risc_driver_v2 *t5_driver_get(uint32_t version){return version==2?&driver:NULL;}

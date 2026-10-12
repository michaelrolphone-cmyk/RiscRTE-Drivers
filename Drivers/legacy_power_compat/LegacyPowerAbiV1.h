#pragma once
/* Private compatibility layouts from T-Watch source 851b8998. These are NOT
 * public capability contracts. New providers implement the independent generic
 * APIs directly; old deployed suffixes are interpreted only in this adapter. */
#include "RiscBatteryGaugeV1.h"
#include "RiscDisplayOutputV1.h"
#include "RiscPowerSleepV1.h"
#include "RiscPowerKeyV1.h"
#include "RiscDisplayPowerV1.h"
typedef struct {
    risc_battery_gauge_api_v1 base;
    bool (*key_events)(void *,uint32_t *);
    bool (*prepare_sleep)(void *);
    bool (*resume)(void *);
    int32_t (*light_sleep)(void *,risc_light_sleep_result_v1 *);
    int32_t (*deep_sleep)(void *);
    int32_t (*light_sleep_for)(void *,uint32_t,risc_light_sleep_result_v1 *);
    bool (*sleep_wake_pending)(void *,bool *);
    int32_t (*deep_sleep_for)(void *,uint32_t);
    int32_t (*light_sleep_set)(void *,uint32_t,risc_light_sleep_result_v1 *);
    int32_t (*deep_sleep_set)(void *,uint32_t);
} legacy_battery_power_v1;
typedef struct {
    risc_display_output_api_v1 base;
    bool (*prepare_sleep)(void *);
    bool (*resume)(void *);
    int32_t (*prepare_deep_sleep)(void *);
    int32_t (*resume_status)(void *);
} legacy_display_power_v1;

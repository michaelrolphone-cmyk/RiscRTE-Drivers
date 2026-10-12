#pragma once
/* power.sleep@1 is a system power service, not a battery-gauge extension.
 * Preparation/resume is paired even on refusal. RETAINED means normal I/O
 * and unloading must stop. A successful deep entry never returns. */
#include "RiscLightSleepV1.h"
#include "RiscDeepSleepV1.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define RISC_POWER_SLEEP_CAPABILITY "power.sleep"
#define RISC_POWER_SLEEP_API_V1 1u
typedef struct {
    uint32_t api_version,struct_size;
    void *context;
    bool (*prepare_sleep)(void *);
    bool (*resume)(void *);
    int32_t (*light_sleep)(void *,risc_light_sleep_result_v1 *);
    int32_t (*deep_sleep)(void *);
    int32_t (*light_sleep_for)(void *,uint32_t,risc_light_sleep_result_v1 *);
    /* Non-acknowledging observation of the service's wake inputs. */
    bool (*sleep_wake_pending)(void *,bool *);
    int32_t (*deep_sleep_for)(void *,uint32_t);
    /* Include any separately enrolled generic wake sources. */
    int32_t (*light_sleep_set)(void *,uint32_t,risc_light_sleep_result_v1 *);
    int32_t (*deep_sleep_set)(void *,uint32_t);
} risc_power_sleep_api_v1;
#define RISC_POWER_LIGHT_SLEEP_SIZE offsetof(risc_power_sleep_api_v1,deep_sleep)
#define RISC_POWER_DEEP_SLEEP_SIZE offsetof(risc_power_sleep_api_v1,light_sleep_for)
#define RISC_POWER_TIMED_SLEEP_SIZE offsetof(risc_power_sleep_api_v1,deep_sleep_for)
#define RISC_POWER_TIMED_DEEP_SLEEP_SIZE offsetof(risc_power_sleep_api_v1,light_sleep_set)
#define RISC_POWER_WAKE_SET_SIZE sizeof(risc_power_sleep_api_v1)

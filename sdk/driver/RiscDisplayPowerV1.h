#pragma once
/* display.power@1 is distinct from display.output@1. Applications need no
 * panel-specific suffix on a framebuffer interface. The board profile selects
 * the power service for its output (LCD, OLED, e-paper, remote, or other). */
#include "RiscDeepSleepV1.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define RISC_DISPLAY_POWER_CAPABILITY "display.power"
#define RISC_DISPLAY_POWER_API_V1 1u
typedef struct {
    uint32_t api_version,struct_size;
    void *context;
    /* Refuse pending/borrowed frames. Even failed preparation is paired with
     * resume. The service owns physical blanking, supplies and pin retention. */
    bool (*prepare_sleep)(void *);
    bool (*resume)(void *);
    int32_t (*prepare_deep_sleep)(void *);
    int32_t (*resume_status)(void *);
} risc_display_power_api_v1;
#define RISC_DISPLAY_POWER_LIGHT_SLEEP_SIZE offsetof(risc_display_power_api_v1,prepare_deep_sleep)
#define RISC_DISPLAY_POWER_DEEP_SLEEP_SIZE offsetof(risc_display_power_api_v1,resume_status)
#define RISC_DISPLAY_POWER_RESUME_STATUS_SIZE sizeof(risc_display_power_api_v1)

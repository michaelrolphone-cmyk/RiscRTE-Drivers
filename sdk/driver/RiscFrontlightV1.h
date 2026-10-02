#pragma once
/* Dual-channel frontlight. Levels are ratios of maximum, not raw PWM counts. */
#include "RiscProviderV2.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RISC_FRONTLIGHT_API_V1 1u
#define RISC_FRONTLIGHT_CAPABILITY "display.frontlight"
typedef struct {
    uint32_t api_version;
    uint32_t struct_size;
    void *context;
    bool (*set)(void *context, uint16_t cool, uint16_t warm, uint16_t maximum);
    bool (*off)(void *context);
} risc_frontlight_api_v1;
#ifdef __cplusplus
}
#endif

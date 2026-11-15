#pragma once
#include <stdbool.h>
#include <stdint.h>
#define RISC_POWER_SLEEP_GUARD_CAPABILITY "power.sleep-guard"
#define RISC_POWER_SLEEP_GUARD_API_V1 1u
/* Cached prerequisite only: ready() never performs I/O, waits or cleanup.
 * False is not a claim of low power. The selected graph must exclude any
 * other writer of the guarded device for this provider's whole lifetime. */
typedef struct {
    uint32_t api_version, struct_size;
    void *context;
    bool (*ready)(void *context);
} risc_power_sleep_guard_v1;

#pragma once
/* power.rail@1: an independently selected supply/enable path. The interface
 * describes functions, not a board, PMIC model, channel number or transport.
 * Configuration readback is not a measurement of voltage at the load. */
#include <stdbool.h>
#include <stdint.h>
#define RISC_POWER_RAIL_CAPABILITY "power.rail"
#define RISC_POWER_RAIL_API_V1 1u
typedef struct {
    uint32_t struct_size;
    uint32_t configured_millivolts;
    bool enabled;
    bool configuration_verified;
    uint8_t reserved[2];
} risc_power_rail_info_v1;
typedef struct {
    uint32_t api_version, struct_size;
    void *context;
    bool (*get_info)(void *,risc_power_rail_info_v1 *);
    /* The board-selected implementation owns voltage/ramp policy. A consumer
     * holds its load in reset while enabling. False is not proof of no effect. */
    bool (*set_enabled)(void *,bool);
} risc_power_rail_api_v1;

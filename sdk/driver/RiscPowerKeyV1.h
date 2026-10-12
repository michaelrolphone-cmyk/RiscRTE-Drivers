#pragma once
/* input.power-key@1: logical button events. A PMIC key, GPIO switch, touch key,
 * or remote input can implement this contract. No raw pin/IRQ/PMIC values. */
#include <stdbool.h>
#include <stdint.h>
#define RISC_POWER_KEY_CAPABILITY "input.power-key"
#define RISC_POWER_KEY_API_V1 1u
enum { RISC_POWER_KEY_LONG_PRESS=1u, RISC_POWER_KEY_SHORT_PRESS=2u };
typedef struct {
    uint32_t api_version,struct_size;
    void *context;
    /* On success consumes latched events; zero means none. A failed read must
     * not be interpreted as an empty queue. */
    bool (*events)(void *,uint32_t *);
} risc_power_key_api_v1;

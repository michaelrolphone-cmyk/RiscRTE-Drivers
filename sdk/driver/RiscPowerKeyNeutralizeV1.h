#pragma once
/* Optional prefix-compatible input.power-key@1 extension. This app-owned
 * boundary consumes the wake gesture without synthesizing an application key.
 * Successful neutralize has no hardware I/O: it arms a logical gate. Events
 * remain suppressed through release and any late latch until a fresh physical
 * press has been observed. A caller must negotiate the complete suffix. */
#include "RiscPowerKeyV1.h"
#include <stddef.h>
typedef struct {
    risc_power_key_api_v1 base;
    bool (*neutralize)(void *);
} risc_power_key_neutralize_v1;
#define RISC_POWER_KEY_NEUTRALIZE_SIZE sizeof(risc_power_key_neutralize_v1)

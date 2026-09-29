#pragma once
#include "FreeRTOS.h"
#include <time.h>
static inline TickType_t xTaskGetTickCount(void) {
    struct timespec now = {0};
    (void)timespec_get(&now, TIME_UTC);
    return (TickType_t)((uint64_t)now.tv_sec * 1000u +
                        (uint64_t)now.tv_nsec / 1000000u);
}

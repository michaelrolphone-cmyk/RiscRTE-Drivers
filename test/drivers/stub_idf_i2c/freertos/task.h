#pragma once
#include "FreeRTOS.h"
#include <time.h>
static inline TickType_t xTaskGetTickCount(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (TickType_t)((uint64_t)now.tv_sec * 1000u + now.tv_nsec / 1000000u);
}

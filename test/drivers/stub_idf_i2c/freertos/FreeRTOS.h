#pragma once
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
/* Host test fixture ONLY. */
#include <stdint.h>
typedef uint32_t TickType_t;
#define pdMS_TO_TICKS(milliseconds) ((TickType_t)(milliseconds))

#ifndef pdTRUE
#define pdTRUE 1
#endif
#ifndef pdFALSE
#define pdFALSE 0
#endif
#ifndef portMAX_DELAY
#define portMAX_DELAY UINT32_MAX
#endif

#define portTICK_PERIOD_MS 1u

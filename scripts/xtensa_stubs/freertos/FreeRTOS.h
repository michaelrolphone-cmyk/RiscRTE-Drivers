#pragma once
/* Build-only declarations for the FreeRTOS semaphore macros used by
 * i2c-esp32s3-v2. They preserve the ESP-IDF queue-symbol ABI without
 * embedding another FreeRTOS implementation in this repository. */
#include <stddef.h>
#include <stdint.h>
typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef uint32_t TickType_t;
typedef void *QueueHandle_t;
#define pdTRUE 1
#define pdFALSE 0
#define portMAX_DELAY ((TickType_t)UINT32_MAX)
#define pdMS_TO_TICKS(milliseconds) ((TickType_t)(milliseconds))
#define queueQUEUE_TYPE_MUTEX ((uint8_t)1u)
#define queueSEND_TO_BACK ((BaseType_t)0)
#define semGIVE_BLOCK_TIME ((TickType_t)0u)

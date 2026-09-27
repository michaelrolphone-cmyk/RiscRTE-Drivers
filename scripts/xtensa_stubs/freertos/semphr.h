#pragma once
/* Build-only semaphore facade matching the ESP-IDF macro expansion used by
 * the canonical driver. Runtime resolution is intentionally left to RiscRTE. */
#include "FreeRTOS.h"
typedef QueueHandle_t SemaphoreHandle_t;
QueueHandle_t xQueueCreateMutex(uint8_t queue_type);
BaseType_t xQueueSemaphoreTake(QueueHandle_t queue, TickType_t ticks_to_wait);
BaseType_t xQueueGenericSend(QueueHandle_t queue, const void *item,
                             TickType_t ticks_to_wait, BaseType_t copy_position);
void vQueueDelete(QueueHandle_t queue);
#define xSemaphoreCreateMutex() xQueueCreateMutex(queueQUEUE_TYPE_MUTEX)
#define xSemaphoreTake(semaphore, ticks) \
    xQueueSemaphoreTake((QueueHandle_t)(semaphore), (ticks))
#define xSemaphoreGive(semaphore) \
    xQueueGenericSend((QueueHandle_t)(semaphore), NULL, semGIVE_BLOCK_TIME, queueSEND_TO_BACK)
#define vSemaphoreDelete(semaphore) vQueueDelete((QueueHandle_t)(semaphore))

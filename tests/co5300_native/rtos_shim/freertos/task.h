#pragma once
#include "FreeRTOS.h"
typedef void* TaskHandle_t;
extern "C" {
BaseType_t xPortGetCoreID(void);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
void vTaskDelay(TickType_t);
}

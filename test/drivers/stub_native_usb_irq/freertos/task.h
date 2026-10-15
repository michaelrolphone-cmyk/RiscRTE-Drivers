#pragma once
#include "FreeRTOS.h"
typedef struct tskTaskControlBlock *TaskHandle_t;
#ifdef __cplusplus
extern "C" {
#endif
TaskHandle_t xTaskGetCurrentTaskHandle(void);
BaseType_t xTaskGetCoreID(TaskHandle_t task);
#ifdef __cplusplus
}
#endif

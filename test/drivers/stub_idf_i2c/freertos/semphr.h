#pragma once
/* Host test fixture ONLY. Models the FreeRTOS mutex API used by the I2C ELF. */
#include "FreeRTOS.h"
#include <pthread.h>
#include <stdlib.h>
#include <time.h>

typedef struct test_semaphore {
    pthread_mutex_t mutex;
} *SemaphoreHandle_t;

static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    SemaphoreHandle_t semaphore = (SemaphoreHandle_t)malloc(sizeof(*semaphore));
    if (!semaphore) return NULL;
    if (pthread_mutex_init(&semaphore->mutex, NULL) != 0) {
        free(semaphore);
        return NULL;
    }
    return semaphore;
}

static inline int xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t ticks) {
    if (!semaphore) return pdFALSE;
    if (ticks == 0)
        return pthread_mutex_trylock(&semaphore->mutex) == 0 ? pdTRUE : pdFALSE;
    if (ticks == portMAX_DELAY)
        return pthread_mutex_lock(&semaphore->mutex) == 0 ? pdTRUE : pdFALSE;
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += ticks / 1000u;
    deadline.tv_nsec += (long)(ticks % 1000u) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        ++deadline.tv_sec; deadline.tv_nsec -= 1000000000L;
    }
    return pthread_mutex_timedlock(&semaphore->mutex, &deadline) == 0 ? pdTRUE : pdFALSE;
}

static inline int xSemaphoreGive(SemaphoreHandle_t semaphore) {
    return semaphore && pthread_mutex_unlock(&semaphore->mutex) == 0 ? pdTRUE : pdFALSE;
}

static inline void vSemaphoreDelete(SemaphoreHandle_t semaphore) {
    if (!semaphore) return;
    (void)pthread_mutex_destroy(&semaphore->mutex);
    free(semaphore);
}

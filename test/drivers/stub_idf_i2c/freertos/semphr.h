#pragma once
/* Host test fixture ONLY. Models the FreeRTOS mutex API used by the I2C ELF. */
#include "FreeRTOS.h"
#include <pthread.h>
#include <stdlib.h>
#include <threads.h>
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

static inline uint64_t test_semaphore_now_ms(void) {
    struct timespec now = {0};
    (void)timespec_get(&now, TIME_UTC);
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
}

static inline int xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t ticks) {
    if (!semaphore) return pdFALSE;
    if (ticks == 0)
        return pthread_mutex_trylock(&semaphore->mutex) == 0 ? pdTRUE : pdFALSE;
    if (ticks == portMAX_DELAY)
        return pthread_mutex_lock(&semaphore->mutex) == 0 ? pdTRUE : pdFALSE;
    const uint64_t deadline = test_semaphore_now_ms() + (uint64_t)ticks;
    for (;;) {
        if (pthread_mutex_trylock(&semaphore->mutex) == 0) return pdTRUE;
        if (test_semaphore_now_ms() >= deadline) return pdFALSE;
        thrd_yield();
    }
}

static inline int xSemaphoreGive(SemaphoreHandle_t semaphore) {
    return semaphore && pthread_mutex_unlock(&semaphore->mutex) == 0 ? pdTRUE : pdFALSE;
}

static inline void vSemaphoreDelete(SemaphoreHandle_t semaphore) {
    if (!semaphore) return;
    (void)pthread_mutex_destroy(&semaphore->mutex);
    free(semaphore);
}

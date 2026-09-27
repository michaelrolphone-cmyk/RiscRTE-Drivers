#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define T5_DRIVER_ABI_VERSION 1u
#define T5_KERNEL_IO_API_VERSION 1u

// Privileged, runtime-bound endpoint. No GPIO numbers, board objects, or global
// kernel exports are given to apps. Calls are synchronous on the owning task.
typedef struct {
    uint32_t api_version;
    uint32_t struct_size;
    void *context;
    uint32_t (*millis)(void *context);
    void (*sleep_ms)(void *context, uint32_t ms);
    bool (*power_acquire)(void *context);
    void (*power_release)(void *context);
    bool (*serial_open)(void *context, uint32_t baud);
    void (*serial_close)(void *context);
    size_t (*serial_read)(void *context, uint8_t *buffer, size_t capacity);
} t5_kernel_io_v1;

typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    const char *driver_id;
    const char *capability_id;
    uint32_t capability_api;
    const void *capability;
    bool (*start)(const t5_kernel_io_v1 *host);
    void (*stop)(void);
} t5_driver_v1;

typedef const t5_driver_v1 *(*t5_driver_get_fn)(uint32_t requested_abi);
// The ONLY public symbol of a driver ELF. Constructors/global initialization
// are not part of this ABI; initialize explicitly in start and unwind in stop.
const t5_driver_v1 *t5_driver_get(uint32_t requested_abi);
#ifdef __cplusplus
}
#endif

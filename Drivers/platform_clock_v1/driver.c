/* Generic platform.clock@1 provider. This ELF implements timing itself using
 * the native loader's existing libc clock_gettime/usleep imports; the RiscRTE
 * core contains no peripheral-specific timing proxy. Invocation, quiescence
 * and unloading MUST be serialized by the generic provider executor. */
#include "RiscPlatformClockV1.h"
#include <stdint.h>
#include <time.h>
#include <unistd.h>

static bool running;

/* UINT64_MAX is a fail-closed time-source error sentinel. Consumers must
 * treat it as an immediate timeout, including when sampled at loop entry. */
static uint64_t monotonic_ms(void *context) {
    (void)context;
    if (!running) return UINT64_MAX;
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0 || now.tv_sec < 0 ||
        now.tv_nsec < 0 || now.tv_nsec >= 1000000000L ||
        (uint64_t)now.tv_sec > (UINT64_MAX - 999u) / 1000u)
        return UINT64_MAX;
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
}

static void sleep_ms(void *context, uint32_t milliseconds) {
    (void)context;
    /* usleep is a real OS scheduling primitive, already exported by the
     * standard ELF libc table. Limit each invocation to a valid subsecond
     * interval; no busy-wait and no hardware timer implementation in core. */
    while (running && milliseconds) {
        uint32_t chunk = milliseconds > 999u ? 999u : milliseconds;
        (void)usleep(chunk * 1000u);
        milliseconds -= chunk;
    }
}

static bool start(const risc_provider_dependency_v1 *deps, size_t count) {
    (void)deps;
    if (running || count) return false;
    /* Reject startup when this target's monotonic clock is unavailable. */
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0 || now.tv_sec < 0 ||
        now.tv_nsec < 0 || now.tv_nsec >= 1000000000L) return false;
    running = true;
    return true;
}
static bool quiesce(void) {
    /* Caller must first stop new entries and drain current synchronous calls.
     * Neither stop nor dlclose is a substitute for that executor barrier. */
    return true;
}
static void stop(void) { running = false; }
static const risc_platform_clock_api_v1 api = {
    RISC_PLATFORM_CLOCK_API_V1, sizeof(risc_platform_clock_api_v1), NULL,
    monotonic_ms, sleep_ms
};
static const risc_driver_v2 driver = {
    RISC_PROVIDER_DRIVER_ABI_V2, sizeof(risc_driver_v2),
    "platform-clock-v1", "platform.clock", RISC_PLATFORM_CLOCK_API_V1,
    &api, start, stop, quiesce
};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &driver : NULL;
}

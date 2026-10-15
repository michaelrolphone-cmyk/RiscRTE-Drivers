#include <time.h>
#include <unistd.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "RiscPlatformClockV1.h"

static int32_t seconds = 123;
static int32_t nanoseconds = 456000000;
static int fail;
static unsigned sleeps;
static uint32_t last_sleep;

/* Model the ordinary Runtime import: exactly two 32-bit words, never a native
 * SDK6 struct timespec. Function names are scoped by the host test command. */
int clock_gettime(clockid_t clock_id, struct timespec *out) {
    assert(clock_id == 4);
    if (fail) return -1;
    const int32_t frozen[2] = {seconds, nanoseconds};
    memcpy(out, frozen, sizeof(frozen));
    return 0;
}
int usleep(useconds_t usec) {
    ++sleeps;
    last_sleep = usec;
    return 0;
}

int main(void) {
    struct { uint32_t before; struct timespec value; uint32_t after; } guard = {
        UINT32_C(0x5a5a5a5a), {0, 0}, UINT32_C(0xa5a5a5a5)
    };
    assert(sizeof(time_t) == 4 && sizeof(guard.value) == 8);
    assert(offsetof(struct timespec, tv_nsec) == 4);
    assert(clock_gettime(CLOCK_MONOTONIC, &guard.value) == 0);
    assert(guard.value.tv_sec == seconds && guard.value.tv_nsec == nanoseconds);
    assert(guard.before == UINT32_C(0x5a5a5a5a) && guard.after == UINT32_C(0xa5a5a5a5));
    const risc_driver_v2 *driver = t5_driver_get(2);
    assert(driver && !t5_driver_get(1));
    const risc_platform_clock_api_v1 *api = driver->capability;
    assert(api->monotonic_ms(NULL) == UINT64_MAX);
    assert(driver->start(NULL, 0));
    assert(api->monotonic_ms(NULL) == UINT64_C(123456));
    seconds = INT32_MAX; nanoseconds = 999999999;
    assert(api->monotonic_ms(NULL) == (uint64_t)INT32_MAX * 1000 + 999);
    nanoseconds = 1000000000;
    assert(api->monotonic_ms(NULL) == UINT64_MAX);
    nanoseconds = 0; seconds = -1;
    assert(api->monotonic_ms(NULL) == UINT64_MAX);
    seconds = 1; fail = 1;
    assert(api->monotonic_ms(NULL) == UINT64_MAX);
    api->sleep_ms(NULL, 1000);
    assert(sleeps == 2 && last_sleep == 1000);
    assert(driver->quiesce());
    driver->stop();
    assert(!driver->start(NULL, 0));
    fail = 0;
    assert(driver->start(NULL, 0));
    driver->stop();
    assert(api->monotonic_ms(NULL) == UINT64_MAX);
    return 0;
}

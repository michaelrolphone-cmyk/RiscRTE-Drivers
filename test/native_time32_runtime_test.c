/* Compile with -I sdk/compat/elf-time32 to exercise the actual ordinary view. */
#include <time.h>
#include <stdint.h>
#include <assert.h>
int copy_native_time_to_ordinary(int64_t, int64_t, void *);
int main(void) {
    struct { uint32_t before; struct timespec value; uint32_t after; } out = {
        UINT32_C(0x12345678), {0, 0}, UINT32_C(0x87654321)
    };
    assert(copy_native_time_to_ordinary(123, 456000000, &out.value) == 0);
    assert(out.value.tv_sec == 123 && out.value.tv_nsec == 456000000);
    assert(out.before == UINT32_C(0x12345678) && out.after == UINT32_C(0x87654321));
    assert(copy_native_time_to_ordinary(INT32_MAX, 999999999, &out.value) == 0);
    assert(out.value.tv_sec == INT32_MAX && out.value.tv_nsec == 999999999);
    assert(copy_native_time_to_ordinary((int64_t)INT32_MAX + 1, 0, &out.value) == -1);
    assert(out.value.tv_sec == INT32_MAX && out.value.tv_nsec == 999999999);
    assert(out.before == UINT32_C(0x12345678) && out.after == UINT32_C(0x87654321));
    assert(copy_native_time_to_ordinary(1, -1, &out.value) == -1);
    assert(copy_native_time_to_ordinary(1, 1000000000, &out.value) == -1);
    return 0;
}

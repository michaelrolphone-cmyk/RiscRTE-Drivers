/* Compile this translation unit with Runtime's real private ABI adapter,
 * without the consumer overlay. The consumer is compiled separately. */
#include "private/elf_time32.h"
int copy_native_time_to_ordinary(int64_t seconds, int64_t nanoseconds, void *out) {
    const struct timespec native = {.tv_sec = seconds, .tv_nsec = nanoseconds};
    return risc_elf_time32_copy(&native, out);
}

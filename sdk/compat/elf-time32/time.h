#ifndef RISC_ELF_TIME32_CONSUMER_H
#define RISC_ELF_TIME32_CONSUMER_H
/* Ordinary loadable-ELF clock_gettime ABI, frozen at the deployed time32 layout.
 * This narrow consumer overlay is selected by the generic clock builder only.
 * It must never be used when compiling native ESP-IDF firmware/providers.
 */
#include <stddef.h>
#include <stdint.h>
#if defined(__time_t_defined) || defined(_TIME_T_DECLARED) || defined(_SYS__TIMESPEC_H_) || defined(_STRUCT_TIMESPEC)
#error "The ordinary ELF time32 overlay must precede native libc time declarations"
#endif
typedef int32_t time_t;
#define __time_t_defined
#define _TIME_T_DECLARED
typedef int32_t clockid_t;
#define __clockid_t_defined
#define _CLOCKID_T_DECLARED
struct timespec { int32_t tv_sec; int32_t tv_nsec; };
#define _SYS__TIMESPEC_H_
#define _STRUCT_TIMESPEC
#define CLOCK_MONOTONIC 4
#ifdef __cplusplus
static_assert(sizeof(time_t) == 4 && sizeof(struct timespec) == 8 && offsetof(struct timespec, tv_nsec) == 4, "ordinary ELF time32 ABI");
extern "C" {
#else
_Static_assert(sizeof(time_t) == 4 && sizeof(struct timespec) == 8 && offsetof(struct timespec, tv_nsec) == 4, "ordinary ELF time32 ABI");
#endif
int clock_gettime(clockid_t clock_id, struct timespec *value);
#ifdef __cplusplus
}
#endif
#endif

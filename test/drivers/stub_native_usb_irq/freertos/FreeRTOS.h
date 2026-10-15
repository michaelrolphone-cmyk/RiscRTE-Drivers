#pragma once
#include <stdint.h>
typedef int BaseType_t;
#define tskNO_AFFINITY INT32_MAX
#ifdef __cplusplus
extern "C" {
#endif
BaseType_t xPortGetCoreID(void);
BaseType_t xPortInIsrContext(void);
#ifdef __cplusplus
}
#endif

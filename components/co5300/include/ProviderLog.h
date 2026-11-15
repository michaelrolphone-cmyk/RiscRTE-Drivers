#pragma once
#include "RiscRuntimeV1.h"
/* Bounded best-effort logging only. Never acquire capabilities or change
 * lifecycle from here. No I/O/formatting is performed inside graph locks. */
static inline void provider_log(const char *message) {
    const risc_runtime_api_v1 *rt=risc_runtime_get_api(1);
    if(rt && rt->api_version==1 && rt->struct_size>=RISC_RUNTIME_CAPABILITIES_V1_SIZE && rt->diagnostic)
        (void)rt->diagnostic(message);
}

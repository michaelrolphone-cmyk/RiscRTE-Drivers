/* Host-only diagnostic sink for the standalone owning-driver suites. */
#include "RiscRuntimeV1.h"
#include <stdio.h>
static bool diagnostic(const char *line){return puts(line)>=0;}
static const risc_runtime_api_v1 runtime={.api_version=1,.struct_size=sizeof(runtime),.diagnostic=diagnostic};
const risc_runtime_api_v1 *risc_runtime_get_api(uint32_t version){return version==1?&runtime:NULL;}

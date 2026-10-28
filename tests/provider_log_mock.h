#pragma once
#include "RiscRuntimeV1.h"
#include <string.h>
static char diagnostics_[4096];
static bool log_message_(const char *message) {
    size_t n=strlen(diagnostics_),m=strlen(message);
    if(n+m+2<sizeof(diagnostics_)){memcpy(diagnostics_+n,message,m);diagnostics_[n+m]='\n';diagnostics_[n+m+1]=0;}
    return true;
}
static const risc_runtime_api_v1 diagnostic_runtime_={.api_version=1,.struct_size=sizeof(diagnostic_runtime_),.diagnostic=log_message_};
const risc_runtime_api_v1 *risc_runtime_get_api(uint32_t version) {return version==1?&diagnostic_runtime_:NULL;}

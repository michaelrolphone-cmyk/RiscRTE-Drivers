#pragma once
#include "RiscProviderV2.h"
#define RISC_BQ25896_PROFILE_API_V1 1u
#define RISC_BQ25896_PROFILE_CAPABILITY "board.power.bq25896.profile"
typedef struct {
    uint32_t api_version;
    uint32_t struct_size;
    uint32_t max_host_milliamps;
    uint32_t boost_millivolts;
    uint32_t boost_limit_milliamps;
    uint32_t boost_settle_ms;
    uint32_t input_settle_ms;
    uint32_t transient_window_ms;
    uint32_t transient_stable_ms;
} risc_bq25896_profile_api_v1;

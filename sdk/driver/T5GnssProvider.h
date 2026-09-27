#pragma once
#include <T5GpsApi.h>
#define T5_GNSS_CAPABILITY "position.gnss"
#define T5_GNSS_API_VERSION 1u

typedef struct {
    uint32_t api_version;
    uint32_t struct_size;
    bool (*read)(t5_gps_state_t *state);
} t5_gnss_api_v1;

#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define T5_GPS_API_VERSION 1u

typedef enum {
    T5_GPS_STATUS_UNSUPPORTED = 0,
    T5_GPS_STATUS_OFF = 1,
    T5_GPS_STATUS_SEARCHING = 2,
    T5_GPS_STATUS_FIX = 3,
} t5_gps_status_t;

typedef struct {
    uint8_t status;
    uint8_t receiver_detected;
    uint8_t fix_valid;
    uint8_t satellites;
    double latitude;
    double longitude;
    float altitude_m;
    float hdop;
    float speed_kph;
    float course_deg;
    uint32_t age_ms;
    uint32_t baud;
    uint32_t chars_processed;
} t5_gps_state_t;

typedef struct {
    uint32_t api_version;
    uint32_t struct_size;
    bool (*supported)(void);
    bool (*start)(void);
    void (*stop)(void);
    bool (*read)(t5_gps_state_t *state);
} t5_gps_api_v1;

const t5_gps_api_v1 *t5_gps_get_api(uint32_t api_version);

#ifdef __cplusplus
}
#endif

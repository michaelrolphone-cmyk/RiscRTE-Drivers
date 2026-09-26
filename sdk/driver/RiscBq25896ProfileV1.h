#pragma once
/* Installed electrical policy for a BQ25896 wired directly to USB VBUS.
 * Pure immutable data: this provider never owns I2C or writes chip registers.
 * Select exactly one applicable profile and the board's i2c.bus provider.
 * Neither chip presence nor a matching CPU implies this wiring/profile.
 * The chip driver copies and validates the profile before claiming hardware.
 * Boards needing additional rail/OTG-pin switching require a composed power
 * provider; this profile does not assert that such wiring is supported. */
#include "RiscProviderV2.h"
#define RISC_BQ25896_PROFILE_API_V1 1u
#define RISC_BQ25896_PROFILE_CAPABILITY "board.power.bq25896.profile"
typedef struct {
    uint32_t api_version;
    uint32_t struct_size;
    uint32_t max_host_milliamps;    /* Consumer admission, not inrush limit. */
    uint32_t boost_millivolts;      /* Exact chip step: 4550 + n * 64. */
    uint32_t boost_limit_milliamps; /* 500/750/1200/1400/1650/1875/2150. */
    uint32_t boost_settle_ms;       /* Initial source stabilization delay. */
    uint32_t input_settle_ms;       /* >=220 ms chip qualification + margin. */
    /* Zero/zero forbids transient recovery. Otherwise permit at most ONE
     * cleared latched startup fault, followed by a clean stable interval.
     * Live/repeated faults ALWAYS fail, irrespective of profile. */
    uint32_t transient_window_ms;
    uint32_t transient_stable_ms;
} risc_bq25896_profile_api_v1;

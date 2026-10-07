#pragma once
/* radio.iq API 1. The driver owns bring-up and the SRAM burst.
 * Callers only pass a buffer. Layout is append-only. */
#include "RiscProviderV2.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RISC_RADIO_IQ_API_V1 1u
#define RISC_RADIO_IQ_PAIRS 256u

#define RISC_RADIO_IQ_OK 0
#define RISC_RADIO_IQ_NOT_RUNNING 1
#define RISC_RADIO_IQ_PLL_FAILED 2
#define RISC_RADIO_IQ_PBUS_FAILED 3
#define RISC_RADIO_IQ_DUMP_TIMEOUT 4
#define RISC_RADIO_IQ_BAD_ARGUMENT 5
#define RISC_RADIO_IQ_BUSY 6
#define RISC_RADIO_IQ_CLEANUP_RETAINED 7

typedef struct {
    uint32_t api_version;
    uint32_t struct_size;
    void *context;
    /* One receive burst. count is 1..RISC_RADIO_IQ_PAIRS words.
     * Each word is the published dump pair: signed 10-bit I in bits 0-9,
     * signed 10-bit Q in bits 10-19. Returns RISC_RADIO_IQ_*. */
    int (*capture_burst)(void *context, uint32_t *pairs, uint32_t count);
    /* Append-only cleanup extension. Idempotent; false retains the resource
     * lease and provider. Retry without normal I/O until this returns true. */
    bool (*suspend)(void *context);
} risc_radio_iq_api_v1;
#ifdef __cplusplus
}
#endif

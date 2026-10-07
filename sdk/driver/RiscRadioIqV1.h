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

/* Optional copied diagnostic extension. No sample data or borrowed pointers.
 * Old consumers retain the unchanged risc_radio_iq_api_v1 prefix. */
enum { RISC_RADIO_IQ_STAGE_IDLE, RISC_RADIO_IQ_STAGE_CLAIM,
       RISC_RADIO_IQ_STAGE_PLL, RISC_RADIO_IQ_STAGE_RECEIVER,
       RISC_RADIO_IQ_STAGE_DUMP, RISC_RADIO_IQ_STAGE_COPY,
       RISC_RADIO_IQ_STAGE_COMPLETE, RISC_RADIO_IQ_STAGE_CLEANUP };
typedef struct {
    uint32_t struct_size, stage;
    int32_t result;
    uint32_t requested_pairs, clock_mask, dump_before, dump_after, elapsed_cycles;
    uint32_t dump_ready, cleanup_ok;
} risc_radio_iq_diagnostics_v1;
typedef struct {
    risc_radio_iq_api_v1 base;
    bool (*diagnostics)(void *context, risc_radio_iq_diagnostics_v1 *out);
} risc_radio_iq_diagnostics_api_v1;
#ifdef __cplusplus
}
#endif

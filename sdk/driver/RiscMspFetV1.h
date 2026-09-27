#pragma once
/* Texas Instruments MSP-FET/eZ-FET transport. Target-family programming and
 * debugging policy belongs in providers above this vendor capability. */
#include "RiscUsbControllerV1.h"
#ifdef __cplusplus
extern "C" {
#endif

#define RISC_MSP_FET_API_V1 1u
#define RISC_MSP_FET_MAX_PROBES 4u
#define RISC_MSP_FET_MAX_REQUEST 250u
#define RISC_MSP_FET_MAX_RESPONSE 4096u

typedef enum {
    RISC_MSP_FET_VARIANT_UNKNOWN = 0,
    RISC_MSP_FET_VARIANT_EZFET_LITE = 1,
    RISC_MSP_FET_VARIANT_MSP_FET = 2,
} risc_msp_fet_variant_v1;

typedef enum {
    RISC_MSP_FET_INTERFACE_NONE = 0,
    RISC_MSP_FET_INTERFACE_JTAG = 1,
    RISC_MSP_FET_INTERFACE_SBW = 2,
} risc_msp_fet_interface_v1;

typedef struct {
    uint64_t device;
    uint16_t vid;
    uint16_t pid;
    uint8_t control_interface;
    uint8_t data_interface;
    uint8_t rx_endpoint;
    uint8_t tx_endpoint;
    uint8_t variant;
    uint8_t supports_jtag;
    uint8_t supports_sbw;
    uint8_t has_backchannel_uart;
} risc_msp_fet_probe_v1;

typedef struct {
    uint32_t api_version;
    uint32_t struct_size;
    void *context;

    /* Incremental discovery. max_events is a strict work bound. */
    bool (*poll)(void *context, size_t max_events);

    /* Standard size-query semantics. */
    bool (*snapshot)(void *context, risc_msp_fet_probe_v1 *out,
                     size_t *inout_count);

    /* Claims the probe's debug CDC function, synchronizes the TI HAL transport,
     * and starts JTAG or Spy-Bi-Wire. */
    uint64_t (*open)(void *context, uint64_t device, uint8_t interface_mode);

    /* Stops the current target interface and starts another one. */
    bool (*set_interface)(void *context, uint64_t session,
                          uint8_t interface_mode);

    /* Executes one MSP-FET HAL function ID. Returns response payload bytes,
     * zero for a successful empty reply, or a negative error. */
    int32_t (*execute)(void *context, uint64_t session, uint8_t function_id,
                       const uint8_t *request, size_t request_length,
                       uint8_t *response, size_t response_capacity,
                       uint32_t timeout_ms);

    /* Stops JTAG/SBW, resets the communications channel and releases claims.
     * Failure retains the session so the ELF cannot be unloaded unsafely. */
    bool (*close)(void *context, uint64_t session);
} risc_msp_fet_api_v1;

#ifdef __cplusplus
}
#endif

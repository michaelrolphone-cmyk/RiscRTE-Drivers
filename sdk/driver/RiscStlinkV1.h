#pragma once
/* Vendor-specific ST-LINK probe transport. Target-specific STM32/STM8
 * programming/debug policy belongs in providers above this capability. */
#include "RiscUsbControllerV1.h"
#ifdef __cplusplus
extern "C" {
#endif

#define RISC_STLINK_API_V1 1u
#define RISC_STLINK_MAX_PROBES 4u
#define RISC_STLINK_COMMAND_BYTES 16u
#define RISC_STLINK_MAX_TRANSFER 6144u

typedef enum {
    RISC_STLINK_TRANSPORT_NONE = 0,
    RISC_STLINK_TRANSPORT_SWD = 1,
    RISC_STLINK_TRANSPORT_SWIM = 2,
} risc_stlink_transport_v1;

typedef enum {
    RISC_STLINK_VARIANT_UNKNOWN = 0,
    RISC_STLINK_VARIANT_V2 = 2,
    RISC_STLINK_VARIANT_V2_1 = 3,
    RISC_STLINK_VARIANT_V3 = 4,
} risc_stlink_variant_v1;

typedef struct {
    uint64_t device;
    uint16_t vid;
    uint16_t pid;
    uint8_t interface_number;
    uint8_t alternate;
    uint8_t rx_endpoint;
    uint8_t tx_endpoint;
    uint16_t max_packet;
    uint8_t variant;
    uint8_t supports_swd;
    uint8_t supports_swim;
} risc_stlink_probe_v1;

typedef struct {
    uint32_t api_version;
    uint32_t struct_size;
    void *context;

    /* Incremental discovery. max_events is a strict work bound. */
    bool (*poll)(void *context, size_t max_events);

    /* Standard size-query semantics: insufficient capacity returns false and
     * stores the required count. */
    bool (*snapshot)(void *context, risc_stlink_probe_v1 *out,
                     size_t *inout_count);

    /* Claims one probe and enters the requested ST-LINK transport. */
    uint64_t (*open)(void *context, uint64_t device, uint8_t transport);

    /* Re-enter SWD or SWIM on an existing exclusive probe session. */
    bool (*set_transport)(void *context, uint64_t session, uint8_t transport);

    /* Bounded raw ST-LINK command exchange for higher-level target providers.
     * command is padded to the 16-byte V2/V2.1/V3 command packet. A command
     * may have a data-out phase OR a data-in phase, never both. */
    int32_t (*command)(void *context, uint64_t session,
                       const uint8_t *command, size_t command_length,
                       const uint8_t *tx, size_t tx_length,
                       uint8_t *rx, size_t rx_capacity,
                       uint32_t timeout_ms);

    /* Leaves the selected ST-LINK transport before releasing the USB claim.
     * Failure retains the claim/session so code cannot be unloaded while the
     * physical probe state is uncertain. */
    bool (*close)(void *context, uint64_t session);
} risc_stlink_api_v1;

#ifdef __cplusplus
}
#endif

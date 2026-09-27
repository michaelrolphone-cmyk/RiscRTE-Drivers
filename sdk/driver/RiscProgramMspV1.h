#pragma once
/* MSP target-programming provider above debug.vendor.msp.
 * The initial API intentionally supports addressed FRAM writes only. */
#include "RiscProviderV2.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define RISC_PROGRAM_MSP_API_V1 1u
#define RISC_PROGRAM_MSP_MAX_CHUNK 128u
#define RISC_PROGRAM_MSP_ERROR_MAX 160u

typedef enum {
    RISC_PROGRAM_MSP_INTERFACE_AUTO = 0,
    RISC_PROGRAM_MSP_INTERFACE_JTAG = 1,
    RISC_PROGRAM_MSP_INTERFACE_SBW = 2,
} risc_program_msp_interface_v1;

typedef struct {
    uint64_t probe_device;
    uint8_t probe_variant;
    uint8_t interface_mode;
    uint8_t jtag_id;
    uint8_t protocol_major;
    uint8_t protocol_minor;
    uint8_t reserved[2];
} risc_program_msp_target_v1;

typedef struct {
    uint32_t api_version;
    uint32_t struct_size;
    void *context;

    /* device=0 selects the only discovered MSP probe; ambiguity fails. */
    uint64_t (*open)(void *context, uint64_t device, uint8_t interface_mode,
                     risc_program_msp_target_v1 *target);

    /* Addressed MSP430FR/FRAM programming. Odd edge bytes are preserved by
     * read-modify-write. Each call is independently bounded. */
    int32_t (*write)(void *context, uint64_t session, uint32_t address,
                     const uint8_t *data, size_t length);
    int32_t (*verify)(void *context, uint64_t session, uint32_t address,
                      const uint8_t *data, size_t length);

    bool (*close)(void *context, uint64_t session);
    bool (*last_error)(void *context, char *out, size_t capacity);
} risc_program_msp_api_v1;

#ifdef __cplusplus
}
#endif

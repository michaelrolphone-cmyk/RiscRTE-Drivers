#pragma once
/* Driver-only protocol ABI. RiscRTE core MUST treat usb.host, serial.port,
 * and their provider interface tables as opaque versioned capabilities. */
#include "RiscProviderV2.h"
#ifdef __cplusplus
extern "C" {
#endif

#define RISC_USB_HOST_API_V1 1u
#define RISC_USB_CDC_API_V1 1u
#define RISC_USB_CONFIG_LIMIT 4096u
#define RISC_USB_CDC_MAX_SESSIONS 4u

/* Device identities and interface claims are host-ELF-owned generation-safe
 * tokens, not IDF objects. Negative transfers fail; zero means no data. */
typedef struct {
    uint32_t api_version;
    uint32_t struct_size;
    void *context;
    bool (*configuration)(void *context, uint64_t device, uint8_t *bytes,
                          size_t *inout_length, uint16_t *vid, uint16_t *pid);
    bool (*claim)(void *context, uint64_t device, uint8_t interface_number,
                  uint8_t alternate, uint64_t *claim);
    void (*release)(void *context, uint64_t claim);
    int32_t (*control)(void *context, uint64_t device, uint8_t request_type,
                       uint8_t request, uint16_t value, uint16_t index,
                       uint8_t *payload, uint16_t length, uint32_t timeout_ms);
    int32_t (*bulk_read)(void *context, uint64_t claim, uint8_t endpoint,
                         uint8_t *dst, size_t capacity, uint32_t timeout_ms);
    int32_t (*bulk_write)(void *context, uint64_t claim, uint8_t endpoint,
                          const uint8_t *src, size_t length, uint32_t timeout_ms);
} risc_usb_host_api_v1;

/* Actual CDC configuration, class control requests, bulk I/O and interface
 * release are performed by the CDC ELF through the separate usb.host ELF.
 * Runtime grants authorize access but do not operate the hardware. */
typedef struct {
    uint32_t api_version;
    uint32_t struct_size;
    uint64_t (*open)(uint64_t device);
    bool (*configure)(uint64_t session, uint32_t baud, uint8_t data_bits,
                      uint8_t parity, uint8_t stop_bits);
    bool (*control_lines)(uint64_t session, bool dtr, bool rts);
    int32_t (*read)(uint64_t session, uint8_t *dst, size_t capacity,
                    uint32_t timeout_ms);
    int32_t (*write)(uint64_t session, const uint8_t *src, size_t length,
                     uint32_t timeout_ms);
    bool (*close)(uint64_t session);
} risc_usb_cdc_api_v1;
#ifdef __cplusplus
}
#endif

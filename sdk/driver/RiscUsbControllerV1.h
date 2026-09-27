#pragma once
/* USB-specific provider-to-provider interfaces. Never include from RiscRTE
 * compiled core. The controller API MUST be implemented by a hardware-owning
 * ELF, not by a firmware bridge or mock in a shipping package. */
#include "RiscUsbProviderV1.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RISC_USB_CONTROLLER_API_V1 1u
#define RISC_USB_HOST_MAX_DEVICES 8u
#define RISC_USB_HOST_MAX_CLAIMS 16u

/* Physical tokens are owned and generation-checked by the controller ELF.
 * ATTACH/DETACH are delivered in order; a reused address MUST have a new
 * physical token. Overflow/lost events MUST be treated as an error. */
typedef struct {
    uint32_t kind; /* 1=attach, 2=detach */
    uint64_t physical_device;
} risc_usb_controller_event_v1;

typedef struct {
    uint32_t api_version;
    uint32_t struct_size;
    void *context;
    /* Return 1 for event, 0 for empty, negative for failure/lost events. */
    int32_t (*next_event)(void *context, risc_usb_controller_event_v1 *event);
    bool (*configuration)(void *context, uint64_t physical_device,
                          uint8_t *bytes, size_t *inout_length,
                          uint16_t *vid, uint16_t *pid);
    bool (*claim)(void *context, uint64_t physical_device,
                  uint8_t interface_number, uint8_t alternate,
                  uint64_t *physical_claim);
    /* False means resources might remain live: dependent provider MUST
     * quarantine rather than return a claim slot to the allocation pool. */
    bool (*release)(void *context, uint64_t physical_claim);
    int32_t (*control)(void *context, uint64_t physical_device,
                       uint8_t request_type, uint8_t request,
                       uint16_t value, uint16_t index, uint8_t *payload,
                       uint16_t length, uint32_t timeout_ms);
    int32_t (*bulk_read)(void *context, uint64_t physical_claim,
                         uint8_t endpoint, uint8_t *dst, size_t capacity,
                         uint32_t timeout_ms);
    int32_t (*bulk_write)(void *context, uint64_t physical_claim,
                          uint8_t endpoint, const uint8_t *src, size_t length,
                          uint32_t timeout_ms);
    /* True only when controller has drained transfers, callbacks and DMA.
     * Host must also have no outstanding device/interface claims. */
    bool (*quiesce)(void *context);
} risc_usb_controller_api_v1;

/* Optional append-only extension of the original USB host v1 interface. Old
 * class ELFs use only .host and need no changes. Discovery belongs to the
 * USB provider, never a firmware USB-specific enumerator. Call poll() on
 * the serialized provider executor; devices() copies currently present,
 * generation-qualified tokens into a bounded caller-owned buffer. */
typedef struct {
    risc_usb_host_api_v1 host;
    bool (*poll)(void *context, size_t max_events, size_t *processed);
    bool (*devices)(void *context, uint64_t *out,
                    size_t *inout_count);
} risc_usb_host_discovery_v1;
#ifdef __cplusplus
}
#endif

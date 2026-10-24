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

/* Preserve the deployed discovery/interrupt/diagnostic prefix. U1 checked
 * release/control slots follow it, so published HID binaries keep offsets. */
typedef struct {
    risc_usb_host_api_v1 host;
    bool (*poll)(void *context, size_t max_events, size_t *processed);
    bool (*devices)(void *context, uint64_t *out, size_t *inout_count);
} risc_usb_host_legacy_discovery_v1;

typedef struct {
    risc_usb_host_api_v1 host;
    bool (*poll)(void *context, size_t max_events, size_t *processed);
    bool (*devices)(void *context, uint64_t *out,
                    size_t *inout_count);
    int32_t (*interrupt_read)(void *context, uint64_t claim, uint8_t endpoint,
                              uint8_t *dst, size_t capacity, uint32_t timeout_ms);
    bool (*diagnostic)(void *context, char *out, size_t capacity);
    bool (*release_checked)(void *context, uint64_t claim);
    /* The caller presents its own claim rather than a publicly discoverable
     * device token. Interface recipient: index must equal claimed interface.
     * Device recipient: claim must be the device's sole live claim, preventing
     * one class from issuing device-wide vendor commands against another.
     * Endpoint/other recipients and stale/closing/detached claims fail closed. */
    int32_t (*control_claim)(void *context, uint64_t claim,
                             uint8_t request_type, uint8_t request,
                             uint16_t value, uint16_t index, uint8_t *payload,
                             uint16_t length, uint32_t timeout_ms);
} risc_usb_host_discovery_v1;

/* Provider-owned, bounded and coherent discovery result. Presence and
 * identification are distinct: if descriptor retrieval transiently fails,
 * identified=0 but the same live token remains visible so an established
 * session is NOT falsely closed. No descriptor bytes enter firmware. */
typedef struct {
    uint64_t token;
    uint16_t vid, pid;
    uint8_t identified;
    uint8_t reserved[3];
} risc_usb_device_identity_v1;

/* The discovery ABI prefix above stays intact for independently installed
 * classes. snapshot() consumes at most 16 controller events, then returns one
 * ordered host-owned generation-token snapshot. Capacity failure publishes no
 * partial records and returns the required count. Failed event polling returns
 * false, never an apparently empty device set. */
typedef struct {
    risc_usb_host_discovery_v1 discovery;
    bool (*snapshot)(void *context, risc_usb_device_identity_v1 *out,
                     size_t *inout_count);
} risc_usb_host_snapshot_v1;
#ifdef __cplusplus
}
#endif

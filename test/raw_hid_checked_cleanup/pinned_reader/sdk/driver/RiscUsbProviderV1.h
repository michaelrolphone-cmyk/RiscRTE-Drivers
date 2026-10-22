#pragma once
/* Driver-only protocol ABI. RiscRTE core MUST treat usb.host, serial.port,
 * and their provider interface tables as opaque versioned capabilities. */
#include "RiscSerialPortV1.h"
#ifdef __cplusplus
extern "C" {
#endif

#define RISC_USB_HOST_API_V1 1u
#define RISC_USB_CDC_API_V1 RISC_SERIAL_PORT_API_V1
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

/* The generic serial ABI is identical to the previous class API prefix.
 * Keep historical names for installed providers and their independent builds;
 * compiled core uses only RiscSerialPortV1.h for semantic serial operations. */
typedef risc_serial_port_api_v1 risc_usb_cdc_api_v1;
typedef risc_serial_port_discovery_v1 risc_usb_serial_class_discovery_v1;

/* The class's append-only inventory extension is provider-originated. It
 * lists opaque generation-qualified physical identities through the class
 * ELF itself rather than exporting host enumeration into compiled firmware. */
typedef risc_serial_port_inventory_v1 risc_usb_serial_class_inventory_v1;
#ifdef __cplusplus
}
#endif

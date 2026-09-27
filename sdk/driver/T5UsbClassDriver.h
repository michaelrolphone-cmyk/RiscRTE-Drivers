#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

// A USB class provider is a driver ELF, not an application. The runtime owns
// VBUS, enumeration, device handles, interface claims and transfer submission.
// These POD values never contain pointers to ESP-IDF USB objects.
#define T5_USB_CDC_CLASS_API_VERSION 1u
#define T5_USB_CDC_CLASS_CAPABILITY "usb.class.cdc_acm"

typedef struct {
    uint8_t control_interface;
    uint8_t data_interface;
    uint8_t data_alternate;
    uint8_t ep_in;
    uint8_t ep_out;
    uint16_t ep_in_mps;
    uint16_t ep_out_mps;
} t5_usb_cdc_binding_v1;

typedef struct {
    uint32_t api_version;
    uint32_t struct_size;
    // Input is a bounded copy of the complete configuration descriptor, not
    // an ESP-IDF pointer. A failed probe must not claim a device or mutate out.
    bool (*probe)(const uint8_t *configuration, size_t length,
                  uint16_t vid, uint16_t pid, t5_usb_cdc_binding_v1 *out);
    // Encode the CDC class request payload for SET_LINE_CODING (0x21/0x20).
    bool (*line_coding)(uint32_t baud, uint8_t data_bits, uint8_t parity,
                        uint8_t stop_bits, uint8_t payload[7]);
    // Value for SET_CONTROL_LINE_STATE (0x21/0x22), interface supplied by bind.
    uint16_t (*control_lines)(bool dtr, bool rts);
} t5_usb_cdc_class_api_v1;

#ifdef __cplusplus
}
#endif

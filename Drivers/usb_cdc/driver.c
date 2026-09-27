#include "T5DriverApi.h"
#include "T5UsbClassDriver.h"

// No ESP-IDF, host handle, heap allocation, global hardware symbol or task
// ownership is imported into this ELF. All descriptor data is copied in by
// the runtime and all actual USB operations remain privileged runtime calls.
static bool probe(const uint8_t *config, size_t size, uint16_t vid, uint16_t pid,
                  t5_usb_cdc_binding_v1 *out) {
    (void)vid;
    (void)pid;
    if (!config || !out || size < 9 || size > 4096 ||
        config[0] < 9 || config[1] != 2) return false;
    const size_t total = (size_t)config[2] | ((size_t)config[3] << 8u);
    if (total < 9 || total > size) return false;
    t5_usb_cdc_binding_v1 found = {0xffu, 0xffu, 0u, 0u, 0u, 64u, 64u};
    uint8_t interface = 0xffu, alt = 0u, klass = 0xffu;
    bool in = false, out_ep = false;
    for (size_t offset = 0; offset < total;) {
        if (total - offset < 2) return false;
        const uint8_t length = config[offset];
        const uint8_t type = config[offset + 1u];
        if (length < 2 || length > total - offset) return false;
        if (type == 4u && length >= 9u) {
            interface = config[offset + 2u];
            alt = config[offset + 3u];
            klass = config[offset + 5u];
            if (klass == 0x02u && found.control_interface == 0xffu)
                found.control_interface = interface;
            if (klass == 0x0au && found.data_interface == 0xffu) {
                found.data_interface = interface;
                found.data_alternate = alt;
            }
        } else if (type == 5u && length >= 7u && klass == 0x0au &&
                   interface == found.data_interface && alt == found.data_alternate) {
            const uint8_t endpoint = config[offset + 2u];
            const uint8_t attributes = config[offset + 3u] & 3u;
            const uint16_t mps = (uint16_t)config[offset + 4u] |
                                 ((uint16_t)config[offset + 5u] << 8u);
            if (attributes == 2u && mps > 0u && mps <= 512u) {
                if (endpoint & 0x80u) {
                    if (in) return false;
                    found.ep_in = endpoint;
                    found.ep_in_mps = mps;
                    in = true;
                } else {
                    if (out_ep) return false;
                    found.ep_out = endpoint;
                    found.ep_out_mps = mps;
                    out_ep = true;
                }
            }
        }
        offset += length;
    }
    if (found.control_interface == 0xffu || found.data_interface == 0xffu ||
        !found.ep_in || !found.ep_out || !in || !out_ep) return false;
    *out = found;
    return true;
}

static bool line_coding(uint32_t baud, uint8_t bits, uint8_t parity,
                        uint8_t stop, uint8_t payload[7]) {
    if (!payload || baud < 300u || baud > 3000000u || bits < 5u || bits > 8u ||
        parity > 4u || (stop != 1u && stop != 2u)) return false;
    payload[0] = (uint8_t)baud;
    payload[1] = (uint8_t)(baud >> 8u);
    payload[2] = (uint8_t)(baud >> 16u);
    payload[3] = (uint8_t)(baud >> 24u);
    payload[4] = stop == 2u ? 2u : 0u;
    payload[5] = parity;
    payload[6] = bits;
    return true;
}

static uint16_t control_lines(bool dtr, bool rts) {
    return (uint16_t)((dtr ? 1u : 0u) | (rts ? 2u : 0u));
}

static bool start(const t5_kernel_io_v1 *kernel) {
    // A pure descriptor/protocol provider uses no privileged I/O primitives.
    // The runtime owns all USB operations and enforces the device lease.
    (void)kernel;
    return true;
}
static void stop(void) {}

static const t5_usb_cdc_class_api_v1 capability = {
    T5_USB_CDC_CLASS_API_VERSION, sizeof(t5_usb_cdc_class_api_v1),
    probe, line_coding, control_lines
};
static const t5_driver_v1 driver = {
    T5_DRIVER_ABI_VERSION, sizeof(t5_driver_v1), "usb-cdc-acm",
    T5_USB_CDC_CLASS_CAPABILITY, T5_USB_CDC_CLASS_API_VERSION,
    &capability, start, stop
};

__attribute__((visibility("default")))
const t5_driver_v1 *t5_driver_get(uint32_t abi) {
    return abi == T5_DRIVER_ABI_VERSION ? &driver : 0;
}

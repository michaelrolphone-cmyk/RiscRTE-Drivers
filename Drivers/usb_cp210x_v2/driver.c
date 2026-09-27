#include "RiscUsbProviderV1.h"

/* The separately installed usb.host ELF owns physical USB operations. This
 * class ELF owns CP210x matching, vendor requests and serial sessions. */
typedef struct {
    uint64_t token, device, claim;
    uint8_t interface_number, alternate, in_ep, out_ep;
} cp_session;
static const risc_usb_host_api_v1 *host;
static cp_session sessions[RISC_USB_CDC_MAX_SESSIONS];
static uint8_t descriptors[RISC_USB_CONFIG_LIMIT];
static uint64_t generation;

static bool equal(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *b && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static cp_session *lookup(uint64_t token) {
    if (!host || !token) return 0;
    for (size_t i = 0; i < RISC_USB_CDC_MAX_SESSIONS; ++i)
        if (sessions[i].token == token) return &sessions[i];
    return 0;
}
static bool start(const risc_provider_dependency_v1 *deps, size_t count) {
    if (host || !deps || count != 1 || !equal(deps[0].capability_id, "usb.host") ||
        deps[0].api_version != RISC_USB_HOST_API_V1 || !deps[0].api) return false;
    const risc_usb_host_api_v1 *api = (const risc_usb_host_api_v1 *)deps[0].api;
    if (api->api_version != RISC_USB_HOST_API_V1 ||
        api->struct_size < sizeof(*api) || !api->configuration || !api->claim ||
        !api->release || !api->control || !api->bulk_read || !api->bulk_write) return false;
    host = api;
    return true;
}
static bool quiesce(void) {
    for (size_t i = 0; i < RISC_USB_CDC_MAX_SESSIONS; ++i)
        if (sessions[i].token) return false;
    return true;
}
static void stop(void) {
    /* Generic lifecycle MUST call quiesce first. Never unmap with sessions. */
    if (quiesce()) host = 0;
}

static bool parse(size_t length, cp_session *result) {
    if (!result || length < 9 || length > sizeof(descriptors) ||
        descriptors[0] < 9 || descriptors[1] != 2) return false;
    const size_t total = (size_t)descriptors[2] | ((size_t)descriptors[3] << 8);
    if (total < 9 || total > length) return false;
    uint8_t iface = 0xff, alt = 0, cls = 0;
    uint8_t in = 0, out = 0;
    size_t candidates = 0;
    cp_session found = {0};
    for (size_t pos = 0; pos < total;) {
        if (total - pos < 2) return false;
        const uint8_t n = descriptors[pos], kind = descriptors[pos + 1];
        if (n < 2 || n > total - pos) return false;
        if (kind == 4) {
            /* Commit the PREVIOUS interface after all its endpoints, not
             * before processing the final endpoint of the configuration. */
            if (cls == 0xff && iface != 0xff && in && out) {
                ++candidates;
                found.interface_number = iface;
                found.alternate = alt;
                found.in_ep = in;
                found.out_ep = out;
            }
            if (n < 9) return false;
            iface = descriptors[pos + 2];
            alt = descriptors[pos + 3];
            cls = descriptors[pos + 5];
            in = out = 0;
        } else if (kind == 5) {
            if (n < 7) return false;
            if (cls == 0xff && iface != 0xff &&
                (descriptors[pos + 3] & 3u) == 2u) {
                uint16_t mps = (uint16_t)descriptors[pos + 4] |
                               ((uint16_t)descriptors[pos + 5] << 8);
                if (mps && mps <= 512) {
                    if (descriptors[pos + 2] & 0x80u) {
                        if (in) return false;
                        in = descriptors[pos + 2];
                    } else {
                        if (out) return false;
                        out = descriptors[pos + 2];
                    }
                }
            }
        }
        pos += n;
    }
    if (cls == 0xff && iface != 0xff && in && out) {
        ++candidates;
        found.interface_number = iface;
        found.alternate = alt;
        found.in_ep = in;
        found.out_ep = out;
    }
    if (candidates != 1) return false;
    *result = found;
    return true;
}

static int32_t command(uint64_t device, uint8_t iface, uint8_t req,
                       uint16_t value, uint8_t *payload, uint16_t length) {
    return host->control(host->context, device, 0x41u, req, value, iface,
                         payload, length, 1000);
}
static uint64_t open_device(uint64_t device) {
    if (!host || !device) return 0;
    cp_session *slot = 0;
    for (size_t i = 0; i < RISC_USB_CDC_MAX_SESSIONS; ++i)
        if (!sessions[i].token) { slot = &sessions[i]; break; }
    if (!slot) return 0;
    size_t length = sizeof(descriptors);
    uint16_t vid = 0, pid = 0;
    if (!host->configuration(host->context, device, descriptors, &length, &vid, &pid) ||
        vid != 0x10c4u) return 0;
    (void)pid; /* Future device profiles may constrain the vendor PID set. */
    cp_session candidate = {0};
    if (!parse(length, &candidate)) return 0;
    candidate.device = device;
    if (!host->claim(host->context, device, candidate.interface_number,
                     candidate.alternate, &candidate.claim) || !candidate.claim) return 0;
    if (command(device, candidate.interface_number, 0x00u, 1u, 0, 0) != 0) {
        host->release(host->context, candidate.claim);
        return 0;
    }
    ++generation;
    if (!generation) ++generation;
    candidate.token = generation;
    *slot = candidate;
    return candidate.token;
}
static bool configure(uint64_t token, uint32_t baud, uint8_t bits,
                      uint8_t parity, uint8_t stop_bits) {
    cp_session *s = lookup(token);
    if (!s || baud < 300 || baud > 3000000 || bits < 5 || bits > 8 ||
        parity > 4 || (stop_bits != 1 && stop_bits != 2)) return false;
    uint8_t speed[4] = {(uint8_t)baud, (uint8_t)(baud >> 8),
                        (uint8_t)(baud >> 16), (uint8_t)(baud >> 24)};
    if (command(s->device, s->interface_number, 0x1eu, 0, speed, 4) != 4) return false;
    uint16_t line = (uint16_t)bits << 8;
    if (parity) line |= (uint16_t)parity << 4;
    if (stop_bits == 2) line |= 2u;
    return command(s->device, s->interface_number, 0x03u, line, 0, 0) == 0;
}
static bool control_lines(uint64_t token, bool dtr, bool rts) {
    cp_session *s = lookup(token);
    if (!s) return false;
    uint16_t value = (uint16_t)(0x0300u | (dtr ? 1u : 0u) |
                                (rts ? 2u : 0u));
    return command(s->device, s->interface_number, 0x07u, value, 0, 0) == 0;
}
static int32_t read_data(uint64_t token, uint8_t *dst, size_t capacity,
                         uint32_t timeout_ms) {
    cp_session *s = lookup(token);
    if (!s || !dst || !capacity || capacity > RISC_USB_CONFIG_LIMIT) return -1;
    int32_t n = host->bulk_read(host->context, s->claim, s->in_ep,
                                dst, capacity, timeout_ms);
    return n >= 0 && (size_t)n <= capacity ? n : -1;
}
static int32_t write_data(uint64_t token, const uint8_t *src, size_t length,
                          uint32_t timeout_ms) {
    cp_session *s = lookup(token);
    if (!s || !src || !length || length > RISC_USB_CONFIG_LIMIT) return -1;
    int32_t n = host->bulk_write(host->context, s->claim, s->out_ep,
                                 src, length, timeout_ms);
    return n >= 0 && (size_t)n <= length ? n : -1;
}
static bool close_device(uint64_t token) {
    cp_session *s = lookup(token);
    if (!s) return false;
    /* Failed physical disable is NOT successful cleanup or safe ELF unload. */
    if (command(s->device, s->interface_number, 0x00u, 0, 0, 0) != 0) return false;
    host->release(host->context, s->claim);
    s->token = s->device = s->claim = 0;
    return true;
}
static const risc_usb_cdc_api_v1 capability = {
    RISC_USB_CDC_API_V1, sizeof(risc_usb_cdc_api_v1),
    open_device, configure, control_lines, read_data, write_data, close_device
};
static const risc_driver_v2 driver = {
    RISC_PROVIDER_DRIVER_ABI_V2, sizeof(risc_driver_v2),
    "usb-cp210x-v2", "serial.port", RISC_USB_CDC_API_V1,
    &capability, start, stop, quiesce
};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &driver : 0;
}

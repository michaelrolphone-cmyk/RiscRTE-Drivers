#include "RiscUsbProviderV1.h"
#include <stdint.h>
#include <stddef.h>

/* CH34x-specific matching, vendor requests and serial state live in this
 * independently installable ELF. Physical USB, enumeration, interface claims
 * and transfers remain exclusively in the usb.host/controller ELFs. */
typedef struct {
    uint64_t token, device, claim;
    uint8_t iface, alt, in_ep, out_ep, version;
} ch_session;
static const risc_usb_host_api_v1 *host;
static ch_session sessions[RISC_USB_CDC_MAX_SESSIONS];
static uint8_t descriptor[RISC_USB_CONFIG_LIMIT];
static uint64_t sequence;

static bool same(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *b && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static bool supported(uint16_t vid, uint16_t pid) {
    return (vid == 0x1a86u && (pid == 0x5523u || pid == 0x7522u || pid == 0x7523u)) ||
           (vid == 0x4348u && pid == 0x5523u) ||
           (vid == 0x2184u && pid == 0x0057u) ||
           (vid == 0x9986u && pid == 0x7523u);
}
static ch_session *lookup(uint64_t token) {
    if (!host || !token) return 0;
    for (size_t i = 0; i < RISC_USB_CDC_MAX_SESSIONS; ++i)
        if (sessions[i].token == token) return &sessions[i];
    return 0;
}
static bool start(const risc_provider_dependency_v1 *deps, size_t count) {
    if (host || !deps || count != 1 || !same(deps[0].capability_id, "usb.host") ||
        deps[0].api_version != RISC_USB_HOST_API_V1 || !deps[0].api) return false;
    const risc_usb_host_api_v1 *api = (const risc_usb_host_api_v1 *)deps[0].api;
    if (api->api_version != RISC_USB_HOST_API_V1 || api->struct_size < sizeof(*api) ||
        !api->configuration || !api->claim || !api->release || !api->control ||
        !api->bulk_read || !api->bulk_write) return false;
    host = api;
    return true;
}
static bool quiesce(void) {
    for (size_t i = 0; i < RISC_USB_CDC_MAX_SESSIONS; ++i)
        if (sessions[i].token) return false;
    return true;
}
static void stop(void) {
    if (quiesce()) host = 0;
}
static bool parse(size_t length, ch_session *result) {
    if (!result || length < 9 || length > sizeof(descriptor) ||
        descriptor[0] < 9 || descriptor[1] != 2 ||
        ((size_t)descriptor[2] | ((size_t)descriptor[3] << 8)) != length)
        return false;
    uint8_t iface = 0xff, alt = 0, cls = 0;
    uint8_t in_ep = 0, out_ep = 0;
    size_t candidates = 0;
    ch_session found = {0};
    for (size_t pos = 0; pos < length;) {
        if (length - pos < 2) return false;
        const uint8_t n = descriptor[pos], kind = descriptor[pos + 1];
        if (n < 2 || n > length - pos) return false;
        if (kind == 4) {
            if (cls == 0xff && iface != 0xff && in_ep && out_ep) {
                ++candidates;
                found.iface = iface; found.alt = alt;
                found.in_ep = in_ep; found.out_ep = out_ep;
            }
            if (n < 9) return false;
            iface = descriptor[pos + 2]; alt = descriptor[pos + 3];
            cls = descriptor[pos + 5]; in_ep = out_ep = 0;
        } else if (kind == 5) {
            if (n < 7) return false;
            if (cls == 0xff && iface != 0xff && (descriptor[pos + 3] & 3u) == 2u) {
                const uint16_t mps = (uint16_t)descriptor[pos + 4] |
                                     ((uint16_t)descriptor[pos + 5] << 8);
                const uint8_t ep = descriptor[pos + 2];
                if (!mps || mps > 512 || !(ep & 0x0fu) || (ep & 0x70u))
                    return false;
                if (ep & 0x80u) {
                    if (in_ep) return false;
                    in_ep = ep;
                } else {
                    if (out_ep) return false;
                    out_ep = ep;
                }
            }
        }
        pos += n;
    }
    if (cls == 0xff && iface != 0xff && in_ep && out_ep) {
        ++candidates;
        found.iface = iface; found.alt = alt;
        found.in_ep = in_ep; found.out_ep = out_ep;
    }
    if (candidates != 1) return false; /* No ambiguous multiport binding. */
    *result = found;
    return true;
}
static int32_t command(uint64_t device, uint8_t request_type, uint8_t request,
                       uint16_t value, uint16_t index, uint8_t *payload,
                       uint16_t length) {
    return host->control(host->context, device, request_type, request, value,
                         index, payload, length, 1000);
}
static bool divisor(uint32_t speed, uint8_t version, uint16_t *out) {
    if (!out || speed < 300u || speed > 3000000u) return false;
    const uint32_t clock_rate = 48000000u;
    int factor = 1, prescale = 3;
    for (; prescale >= 0; --prescale) {
        const uint32_t clock_div = 1u << (12 - 3 * prescale - 1);
        const uint32_t min_rate = (clock_rate + clock_div * 512u - 1u) /
                                  (clock_div * 512u);
        if (speed > min_rate) break;
    }
    if (prescale < 0) return false;
    uint32_t clock_div = 1u << (12 - 3 * prescale - factor);
    uint32_t div = clock_rate / (clock_div * speed);
    if (div < 9u || div > 255u) { div /= 2u; clock_div *= 2u; factor = 0; }
    if (div < 2u || div > 256u) return false;
    if (div < 256u) {
        /* 16 * 48 MHz = 768,000,000 fits uint32_t. Avoid unnecessary libgcc
         * 64-bit divide helpers that produce unsupported ELF relocations. */
        const uint32_t actual = (16u * clock_rate) / (clock_div * div);
        const uint32_t next = (16u * clock_rate) / (clock_div * (div + 1u));
        const uint32_t requested = 16u * speed;
        if (actual >= requested && requested >= next &&
            actual - requested >= requested - next) ++div;
    }
    if (factor == 1 && (div & 1u) == 0u) { div /= 2u; factor = 0; }
    uint16_t value = (uint16_t)(((0x100u - div) & 0xffu) << 8u);
    value |= (uint16_t)((factor & 1) << 2u);
    value |= (uint16_t)(prescale & 3);
    if (version > 0x27u) value |= 0x0080u;
    *out = value;
    return true;
}
static bool valid_format(uint32_t baud, uint8_t bits, uint8_t parity, uint8_t stops) {
    return baud >= 300u && baud <= 3000000u && bits >= 5 && bits <= 8 &&
           parity <= 4 && (stops == 1 || stops == 2);
}
static uint64_t open_device(uint64_t device) {
    if (!host || !device || sequence == UINT64_MAX) return 0;
    ch_session *slot = 0;
    for (size_t i = 0; i < RISC_USB_CDC_MAX_SESSIONS; ++i)
        if (!sessions[i].token) { slot = &sessions[i]; break; }
    if (!slot) return 0;
    size_t length = sizeof(descriptor);
    uint16_t vid = 0, pid = 0;
    if (!host->configuration(host->context, device, descriptor, &length, &vid, &pid) ||
        !supported(vid, pid)) return 0;
    ch_session candidate = {0};
    if (!parse(length, &candidate)) return 0;
    candidate.device = device;
    if (!host->claim(host->context, device, candidate.iface, candidate.alt,
                     &candidate.claim) || !candidate.claim) return 0;
    uint8_t version[2] = {0};
    /* Identical vendor initialization sequence to known-good v1.2.16. */
    if (command(device, 0xc0u, 0x5fu, 0, 0, version, 2) != 2 ||
        command(device, 0x40u, 0xa1u, 0, 0, 0, 0) != 0) {
        host->release(host->context, candidate.claim);
        return 0;
    }
    candidate.version = version[0];
    candidate.token = ++sequence;
    *slot = candidate;
    return candidate.token;
}
static bool configure(uint64_t token, uint32_t baud, uint8_t bits,
                      uint8_t parity, uint8_t stops) {
    ch_session *s = lookup(token);
    if (!s || !valid_format(baud, bits, parity, stops)) return false;
    /* Older CH34x revisions accept only the original default framing. */
    if (s->version < 0x30u && (bits != 8 || parity != 0 || stops != 1))
        return false;
    uint16_t value = 0;
    if (!divisor(baud, s->version, &value) ||
        command(s->device, 0x40u, 0x9au, 0x1312u, value, 0, 0) != 0)
        return false;
    if (s->version >= 0x30u) {
        uint8_t lcr = (uint8_t)(0xc0u | (bits - 5u));
        switch (parity) {
            case 1: lcr |= 0x08u; break; /* odd */
            case 2: lcr |= 0x18u; break; /* even */
            case 3: lcr |= 0x28u; break; /* mark */
            case 4: lcr |= 0x38u; break; /* space */
            default: break;
        }
        if (stops == 2) lcr |= 0x04u;
        if (command(s->device, 0x40u, 0x9au, 0x2518u, lcr, 0, 0) != 0)
            return false;
    }
    return true;
}
static bool control_lines(uint64_t token, bool dtr, bool rts) {
    ch_session *s = lookup(token);
    if (!s) return false;
    uint8_t control = 0;
    if (rts) control |= 0x40u;
    if (dtr) control |= 0x20u;
    return command(s->device, 0x40u, 0xa4u,
                   (uint16_t)~(uint16_t)control, 0, 0, 0) == 0;
}
static int32_t read_data(uint64_t token, uint8_t *dst, size_t capacity,
                         uint32_t timeout) {
    ch_session *s = lookup(token);
    if (!s || !dst || !capacity || capacity > RISC_USB_CONFIG_LIMIT) return -1;
    const int32_t n = host->bulk_read(host->context, s->claim, s->in_ep,
                                     dst, capacity, timeout);
    return n >= 0 && (size_t)n <= capacity ? n : -1;
}
static int32_t write_data(uint64_t token, const uint8_t *src, size_t length,
                          uint32_t timeout) {
    ch_session *s = lookup(token);
    if (!s || !src || !length || length > RISC_USB_CONFIG_LIMIT) return -1;
    const int32_t n = host->bulk_write(host->context, s->claim, s->out_ep,
                                      src, length, timeout);
    return n >= 0 && (size_t)n <= length ? n : -1;
}
static bool close_device(uint64_t token) {
    ch_session *s = lookup(token);
    if (!s) return false;
    /* The shared usb.host API releases the physical claim and quarantines
     * it internally if teardown cannot be proved safe. */
    host->release(host->context, s->claim);
    *s = (ch_session){0};
    return true;
}
static const risc_usb_cdc_api_v1 capability = {
    RISC_USB_CDC_API_V1, sizeof(risc_usb_cdc_api_v1),
    open_device, configure, control_lines, read_data, write_data, close_device
};
static const risc_driver_v2 driver = {
    RISC_PROVIDER_DRIVER_ABI_V2, sizeof(risc_driver_v2),
    "usb-ch34x-v2", "serial.port", RISC_USB_CDC_API_V1,
    &capability, start, stop, quiesce
};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &driver : 0;
}

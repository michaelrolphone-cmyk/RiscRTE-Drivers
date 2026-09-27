#include "RiscUsbProviderV1.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * FTDI USB UART class provider.
 *
 * usb.host owns enumeration, interface claims and transfers. This ELF owns
 * FTDI identification, vendor requests, line state, baud divisors and the
 * FTDI-specific two-byte RX status prefix.
 *
 * The serial.port v1 API identifies only a USB device, not one interface of a
 * multi-port adapter, so this provider deliberately binds only devices with
 * exactly one usable FTDI UART interface. FT2232/FT4232 multi-port support
 * belongs behind a future per-port publication/binding contract rather than
 * choosing an arbitrary channel here.
 */

#define FTDI_VID 0x0403u
#define FTDI_PID_232_FAMILY 0x6001u
#define FTDI_PID_232H 0x6014u
#define FTDI_PID_FTX 0x6015u
#define FTDI_PID_232RL 0xfbfau

#define FTDI_REQ_RESET 0u
#define FTDI_REQ_MODEM_CTRL 1u
#define FTDI_REQ_SET_FLOW_CTRL 2u
#define FTDI_REQ_SET_BAUD 3u
#define FTDI_REQ_SET_DATA 4u

#define FTDI_RESET_SIO 0u
#define FTDI_PURGE_RX 1u
#define FTDI_PURGE_TX 2u

#define FTDI_DTR_MASK 0x0100u
#define FTDI_RTS_MASK 0x0200u
#define FTDI_DTR_HIGH 0x0001u
#define FTDI_RTS_HIGH 0x0002u

#define USB_REQ_GET_DESCRIPTOR 6u
#define USB_DESC_DEVICE 1u
#define FTDI_CONTROL_TIMEOUT_MS 1000u
#define FTDI_RX_STATUS_BYTES 2u
#define FTDI_RX_PACKET_MAX 512u

typedef enum {
    FTDI_CHIP_UNSUPPORTED = 0,
    FTDI_CHIP_232BM = 1,
    FTDI_CHIP_HIGHSPEED = 2,
    FTDI_CHIP_FTX = 3,
} ftdi_chip;

typedef struct {
    uint64_t token, device, claim;
    uint8_t iface, alt, in_ep, out_ep;
    uint8_t channel;
    uint16_t max_packet;
    uint16_t bcd_device;
    uint8_t chip;
    uint16_t pending_offset, pending_length;
    uint8_t pending[FTDI_RX_PACKET_MAX - FTDI_RX_STATUS_BYTES];
} ftdi_session;

static const risc_usb_host_api_v1 *host;
static ftdi_session sessions[RISC_USB_CDC_MAX_SESSIONS];
static uint8_t descriptors[RISC_USB_CONFIG_LIMIT];
static uint8_t rx_packet[FTDI_RX_PACKET_MAX];
static uint64_t generation;

static bool equal(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *b && *a == *b) { ++a; ++b; }
    return *a == *b;
}

static bool supported_pid(uint16_t vid, uint16_t pid) {
    if (vid != FTDI_VID) return false;
    return pid == FTDI_PID_232_FAMILY || pid == FTDI_PID_232H ||
           pid == FTDI_PID_FTX || pid == FTDI_PID_232RL;
}

static ftdi_session *lookup(uint64_t token) {
    if (!host || !token) return 0;
    for (size_t i = 0; i < RISC_USB_CDC_MAX_SESSIONS; ++i)
        if (sessions[i].token == token) return &sessions[i];
    return 0;
}

static bool start(const risc_provider_dependency_v1 *deps, size_t count) {
    if (host || !deps || count != 1 || !equal(deps[0].capability_id, "usb.host") ||
        deps[0].api_version != RISC_USB_HOST_API_V1 || !deps[0].api)
        return false;
    const risc_usb_host_api_v1 *api = (const risc_usb_host_api_v1 *)deps[0].api;
    if (api->api_version != RISC_USB_HOST_API_V1 ||
        api->struct_size < sizeof(*api) || !api->configuration || !api->claim ||
        !api->release || !api->control || !api->bulk_read || !api->bulk_write)
        return false;
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

static bool commit_candidate(ftdi_session *found, uint8_t iface, uint8_t alt,
                             uint8_t cls, uint8_t in_ep, uint8_t out_ep,
                             uint16_t in_mps, uint16_t out_mps,
                             size_t *candidates) {
    if (cls != 0xffu || iface == 0xffu || !in_ep || !out_ep) return true;
    if (!in_mps || !out_mps || in_mps > FTDI_RX_PACKET_MAX || out_mps > 512u)
        return false;
    ++*candidates;
    found->iface = iface;
    found->alt = alt;
    found->in_ep = in_ep;
    found->out_ep = out_ep;
    found->max_packet = in_mps;
    return true;
}

static bool parse_configuration(size_t length, ftdi_session *result) {
    if (!result || length < 9u || length > sizeof(descriptors) ||
        descriptors[0] < 9u || descriptors[1] != 2u ||
        ((size_t)descriptors[2] | ((size_t)descriptors[3] << 8)) != length)
        return false;

    uint8_t iface = 0xffu, alt = 0, cls = 0;
    uint8_t in_ep = 0, out_ep = 0;
    uint16_t in_mps = 0, out_mps = 0;
    size_t candidates = 0;
    ftdi_session found = {0};

    for (size_t pos = 0; pos < length;) {
        if (length - pos < 2u) return false;
        const uint8_t n = descriptors[pos];
        const uint8_t kind = descriptors[pos + 1u];
        if (n < 2u || n > length - pos) return false;

        if (kind == 4u) {
            if (!commit_candidate(&found, iface, alt, cls, in_ep, out_ep,
                                  in_mps, out_mps, &candidates))
                return false;
            if (n < 9u) return false;
            iface = descriptors[pos + 2u];
            alt = descriptors[pos + 3u];
            cls = descriptors[pos + 5u];
            in_ep = out_ep = 0;
            in_mps = out_mps = 0;
        } else if (kind == 5u && cls == 0xffu && iface != 0xffu) {
            if (n < 7u) return false;
            const uint8_t ep = descriptors[pos + 2u];
            const uint8_t transfer = descriptors[pos + 3u] & 3u;
            const uint16_t mps = (uint16_t)descriptors[pos + 4u] |
                                 ((uint16_t)descriptors[pos + 5u] << 8);
            if (transfer == 2u) {
                if (!mps || mps > 512u || !(ep & 0x0fu) || (ep & 0x70u))
                    return false;
                if (ep & 0x80u) {
                    if (in_ep) return false;
                    in_ep = ep;
                    in_mps = mps;
                } else {
                    if (out_ep) return false;
                    out_ep = ep;
                    out_mps = mps;
                }
            }
        }
        pos += n;
    }

    if (!commit_candidate(&found, iface, alt, cls, in_ep, out_ep,
                          in_mps, out_mps, &candidates))
        return false;
    if (candidates != 1u) return false;
    *result = found;
    return true;
}

static bool read_device_descriptor(uint64_t device, uint16_t *bcd_device) {
    if (!host || !bcd_device) return false;
    uint8_t bytes[18] = {0};
    const int32_t n = host->control(
        host->context, device, 0x80u, USB_REQ_GET_DESCRIPTOR,
        (uint16_t)(USB_DESC_DEVICE << 8), 0, bytes, sizeof(bytes),
        FTDI_CONTROL_TIMEOUT_MS);
    if (n != (int32_t)sizeof(bytes) || bytes[0] < sizeof(bytes) ||
        bytes[1] != USB_DESC_DEVICE)
        return false;
    *bcd_device = (uint16_t)bytes[12] | ((uint16_t)bytes[13] << 8);
    return true;
}

static bool classify(ftdi_session *session, uint16_t pid, uint16_t bcd_device) {
    if (!session) return false;
    session->bcd_device = bcd_device;
    switch (bcd_device) {
        case 0x0400u: /* FT232B */
        case 0x0600u: /* FT232R */
            if (pid != FTDI_PID_232_FAMILY && pid != FTDI_PID_232RL) return false;
            session->chip = FTDI_CHIP_232BM;
            session->channel = 0;
            return true;
        case 0x0900u: /* FT232H */
            if (pid != FTDI_PID_232H || session->iface > 0xfeu) return false;
            session->chip = FTDI_CHIP_HIGHSPEED;
            session->channel = (uint8_t)(session->iface + 1u);
            return session->channel != 0;
        case 0x1000u: /* FT-X family */
            if (pid != FTDI_PID_FTX || session->iface > 0xfeu) return false;
            session->chip = FTDI_CHIP_FTX;
            session->channel = (uint8_t)(session->iface + 1u);
            return session->channel != 0;
        default:
            return false;
    }
}

static int32_t control_out(uint64_t device, uint8_t request,
                           uint16_t value, uint16_t index) {
    return host->control(host->context, device, 0x40u, request, value, index,
                         0, 0, FTDI_CONTROL_TIMEOUT_MS);
}

static uint32_t divisor_bm(uint32_t baud) {
    static const uint8_t divfrac[8] = {0, 3, 2, 4, 1, 5, 6, 7};
    const uint32_t divisor3 = (48000000u + baud) / (2u * baud);
    uint32_t divisor = divisor3 >> 3;
    divisor |= (uint32_t)divfrac[divisor3 & 7u] << 14;
    if (divisor == 1u) divisor = 0u;
    else if (divisor == 0x4001u) divisor = 1u;
    return divisor;
}

static uint32_t divisor_highspeed(uint32_t baud) {
    static const uint8_t divfrac[8] = {0, 3, 2, 4, 1, 5, 6, 7};
    /* Equivalent to round((8 * 120 MHz) / (10 * baud)) without overflow. */
    const uint32_t divisor3 = (96000000u + baud / 2u) / baud;
    uint32_t divisor = divisor3 >> 3;
    divisor |= (uint32_t)divfrac[divisor3 & 7u] << 14;
    if (divisor == 1u) divisor = 0u;
    else if (divisor == 0x4001u) divisor = 1u;
    return divisor | 0x00020000u;
}

static bool baud_request(const ftdi_session *session, uint32_t baud,
                         uint16_t *value, uint16_t *index) {
    if (!session || !value || !index || baud < 300u) return false;
    uint32_t encoded = 0;
    if (session->chip == FTDI_CHIP_HIGHSPEED && baud >= 1200u) {
        if (baud > 12000000u) return false;
        encoded = divisor_highspeed(baud);
    } else {
        if (baud > 3000000u) return false;
        encoded = divisor_bm(baud);
    }
    *value = (uint16_t)encoded;
    const uint16_t upper = (uint16_t)(encoded >> 16);
    *index = session->channel ?
        (uint16_t)((upper << 8) | session->channel) : upper;
    return true;
}

static uint64_t open_device(uint64_t device) {
    if (!host || !device || generation == UINT64_MAX) return 0;

    ftdi_session *slot = 0;
    for (size_t i = 0; i < RISC_USB_CDC_MAX_SESSIONS; ++i)
        if (!sessions[i].token) { slot = &sessions[i]; break; }
    if (!slot) return 0;

    size_t length = sizeof(descriptors);
    uint16_t vid = 0, pid = 0;
    if (!host->configuration(host->context, device, descriptors, &length,
                             &vid, &pid) || !supported_pid(vid, pid))
        return 0;

    ftdi_session candidate = {0};
    if (!parse_configuration(length, &candidate)) return 0;

    uint16_t bcd_device = 0;
    if (!read_device_descriptor(device, &bcd_device) ||
        !classify(&candidate, pid, bcd_device))
        return 0;

    candidate.device = device;
    if (!host->claim(host->context, device, candidate.iface, candidate.alt,
                     &candidate.claim) || !candidate.claim)
        return 0;

    /* Reset and purge are bounded vendor operations. Failure retains no claim. */
    if (control_out(device, FTDI_REQ_RESET, FTDI_RESET_SIO,
                    candidate.channel) != 0 ||
        control_out(device, FTDI_REQ_RESET, FTDI_PURGE_RX,
                    candidate.channel) != 0 ||
        control_out(device, FTDI_REQ_RESET, FTDI_PURGE_TX,
                    candidate.channel) != 0 ||
        control_out(device, FTDI_REQ_SET_FLOW_CTRL, 0,
                    candidate.channel) != 0) {
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
    ftdi_session *s = lookup(token);
    if (!s || bits < 5u || bits > 8u || parity > 4u ||
        (stop_bits != 1u && stop_bits != 2u))
        return false;

    uint16_t baud_value = 0, baud_index = 0;
    if (!baud_request(s, baud, &baud_value, &baud_index)) return false;

    uint16_t line = bits;
    line |= (uint16_t)parity << 8;
    if (stop_bits == 2u) line |= (uint16_t)(2u << 11);

    if (control_out(s->device, FTDI_REQ_SET_DATA, line, s->channel) != 0)
        return false;
    return control_out(s->device, FTDI_REQ_SET_BAUD,
                       baud_value, baud_index) == 0;
}

static bool control_lines(uint64_t token, bool dtr, bool rts) {
    ftdi_session *s = lookup(token);
    if (!s) return false;
    uint16_t value = FTDI_DTR_MASK | FTDI_RTS_MASK;
    if (dtr) value |= FTDI_DTR_HIGH;
    if (rts) value |= FTDI_RTS_HIGH;
    return control_out(s->device, FTDI_REQ_MODEM_CTRL,
                       value, s->channel) == 0;
}

static size_t drain_pending(ftdi_session *s, uint8_t *dst, size_t capacity) {
    if (!s || !dst || !capacity || s->pending_offset > s->pending_length ||
        s->pending_length > sizeof(s->pending))
        return 0;
    const size_t available = (size_t)(s->pending_length - s->pending_offset);
    const size_t copied = available < capacity ? available : capacity;
    if (copied) {
        memcpy(dst, s->pending + s->pending_offset, copied);
        s->pending_offset = (uint16_t)(s->pending_offset + copied);
    }
    if (s->pending_offset == s->pending_length)
        s->pending_offset = s->pending_length = 0;
    return copied;
}

static int32_t read_data(uint64_t token, uint8_t *dst, size_t capacity,
                         uint32_t timeout_ms) {
    ftdi_session *s = lookup(token);
    if (!s || !dst || !capacity || capacity > RISC_USB_CONFIG_LIMIT ||
        s->max_packet < FTDI_RX_STATUS_BYTES ||
        s->max_packet > sizeof(rx_packet) || !timeout_ms)
        return -1;

    size_t copied = drain_pending(s, dst, capacity);
    if (copied == capacity) return (int32_t)copied;

    /*
     * Read exactly one USB packet. FTDI prepends modem/line status bytes to
     * every packet, so a one-packet transfer preserves the status boundary.
     * Any payload that does not fit the caller is retained in the session.
     */
    const int32_t n = host->bulk_read(host->context, s->claim, s->in_ep,
                                      rx_packet, s->max_packet, timeout_ms);
    if (n < 0) return copied ? (int32_t)copied : -1;
    if (n == 0) return (int32_t)copied;
    if (n < (int32_t)FTDI_RX_STATUS_BYTES || n > (int32_t)s->max_packet)
        return copied ? (int32_t)copied : -1;

    const size_t payload = (size_t)n - FTDI_RX_STATUS_BYTES;
    const size_t room = capacity - copied;
    const size_t take = payload < room ? payload : room;
    if (take) memcpy(dst + copied, rx_packet + FTDI_RX_STATUS_BYTES, take);
    copied += take;

    const size_t remainder = payload - take;
    if (remainder) {
        if (remainder > sizeof(s->pending)) return -1;
        memcpy(s->pending, rx_packet + FTDI_RX_STATUS_BYTES + take, remainder);
        s->pending_offset = 0;
        s->pending_length = (uint16_t)remainder;
    }
    return (int32_t)copied;
}

static int32_t write_data(uint64_t token, const uint8_t *src, size_t length,
                          uint32_t timeout_ms) {
    ftdi_session *s = lookup(token);
    if (!s || !src || !length || length > RISC_USB_CONFIG_LIMIT || !timeout_ms)
        return -1;
    const int32_t n = host->bulk_write(host->context, s->claim, s->out_ep,
                                       src, length, timeout_ms);
    return n >= 0 && (size_t)n <= length ? n : -1;
}

static bool close_device(uint64_t token) {
    ftdi_session *s = lookup(token);
    if (!s) return false;

    /* Preserve the claim if line teardown cannot be confirmed. */
    if (control_out(s->device, FTDI_REQ_MODEM_CTRL,
                    FTDI_DTR_MASK | FTDI_RTS_MASK, s->channel) != 0)
        return false;

    host->release(host->context, s->claim);
    *s = (ftdi_session){0};
    return true;
}

static const risc_usb_cdc_api_v1 capability = {
    RISC_USB_CDC_API_V1, sizeof(risc_usb_cdc_api_v1),
    open_device, configure, control_lines, read_data, write_data, close_device
};

static const risc_driver_v2 driver = {
    RISC_PROVIDER_DRIVER_ABI_V2, sizeof(risc_driver_v2),
    "usb-ftdi", "serial.port", RISC_USB_CDC_API_V1,
    &capability, start, stop, quiesce
};

__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &driver : 0;
}

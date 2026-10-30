#include "RiscUsbControllerV1.h"
#include "RiscSerialPortV1.h"
#include <stdint.h>
#include <stddef.h>

/* CH34x-specific matching, vendor requests and serial state live in this
 * independently installable ELF. Physical USB, enumeration, interface claims
 * and transfers remain exclusively in the usb.host/controller ELFs. */
typedef struct {
    uint64_t token, device, claim;
    uint8_t iface, alt, in_ep, out_ep, version;
    bool orphaned; /* Failed open: no consumer received a session token. */
    uint32_t rx, tx;
    uint32_t rx_size, rx_offset, tx_size, tx_offset;
    bool failed;
} ch_session;
static const risc_usb_host_api_v1 *host;
static const risc_usb_host_discovery_v1 *discovery;
static ch_session sessions[RISC_USB_CDC_MAX_SESSIONS];
static uint8_t descriptor[RISC_USB_CONFIG_LIMIT];
static uint64_t sequence;
typedef ch_session serial_session;
#include "../common/SerialStreamPump.inc"

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
static bool release_session(ch_session *s) {
    if (!host || !discovery || !s || !s->claim ||
        !discovery->release_checked(host->context, s->claim)) return false;
    *s = (ch_session){0};
    return true;
}
static const char *start_error;
static bool last_error(char *destination, size_t capacity) {
    if (!start_error || !destination || !capacity) return false;
    size_t i = 0;
    while (start_error[i] && i + 1 < capacity) {
        destination[i] = start_error[i];
        ++i;
    }
    destination[i] = 0;
    return true;
}
static bool start(const risc_provider_dependency_v1 *deps, size_t count) {
    start_error = 0;
    if (host) {
        start_error = "start: provider already active";
        return false;
    }
    if (!deps) {
        start_error = "start: missing dependency table";
        return false;
    }
    if (count != 1) {
        start_error = "start: expected exactly one dependency";
        return false;
    }
    if (!same(deps[0].capability_id, "usb.host")) {
        start_error = "start: usb.host dependency missing";
        return false;
    }
    if (deps[0].api_version != RISC_USB_HOST_API_V1) {
        start_error = "start: usb.host API version mismatch";
        return false;
    }
    if (!deps[0].api) {
        start_error = "start: usb.host API unavailable";
        return false;
    }
    const risc_usb_host_api_v1 *api = (const risc_usb_host_api_v1 *)deps[0].api;
    if (api->api_version != RISC_USB_HOST_API_V1) {
        start_error = "start: usb.host descriptor version mismatch";
        return false;
    }
    if (api->struct_size < sizeof(risc_usb_host_discovery_v1)) {
        start_error = "start: usb.host discovery API too small";
        return false;
    }
    if (!api->configuration || !api->claim || !api->release || !api->control ||
        !api->bulk_read || !api->bulk_write) {
        start_error = "start: usb.host base callbacks incomplete";
        return false;
    }
    const risc_usb_host_discovery_v1 *extension =
        (const risc_usb_host_discovery_v1 *)api;
    if (!extension->poll || !extension->devices ||
        !extension->release_checked || !extension->control_claim) {
        start_error = "start: usb.host discovery callbacks incomplete";
        return false;
    }
    host = api;
    discovery = extension;
    return true;
}
static bool quiesce(void) {
    /* Only failed-open orphan claims may be retried autonomously. Never close
     * a token owned by an app. One pass is bounded by the fixed session table. */
    for (size_t i = 0; i < RISC_USB_CDC_MAX_SESSIONS; ++i) {
        if (sessions[i].token) return false;
        if (sessions[i].orphaned && !release_session(&sessions[i])) return false;
    }
    return true;
}
static void stop(void) {
    if (!quiesce()) return; /* Uncertain physical state keeps this ELF pinned. */
    discovery = 0;
    host = 0;
    streams = 0;
    next_session = 0;
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
    if (candidates != 1) return false;
    *result = found;
    return true;
}
static int32_t command(uint64_t claim, uint8_t request_type, uint8_t request,
                       uint16_t value, uint16_t index, uint8_t *payload,
                       uint16_t length) {
    if (!discovery || !claim) return -1;
    return discovery->control_claim(host->context, claim, request_type, request, value,
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
        /* 16 * 48 MHz fits uint32_t. Avoid Xtensa ELF-incompatible libgcc
         * 64-bit division helpers and unsupported relocations. */
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
/* Match is read-only: WCH PID policy and descriptor parsing live in this ELF.
 * No claim, vendor request or UART initialization may occur during probing. */
static int32_t probe_device(uint64_t device) {
    if (!host || !device) return -1;
    size_t length = sizeof(descriptor);
    uint16_t vid = 0, pid = 0;
    if (!host->configuration(host->context, device, descriptor, &length,
                             &vid, &pid)) return -1;
    if (!supported(vid, pid)) return 0;
    ch_session candidate = {0};
    return parse(length, &candidate) ? 1 : 0;
}

/* The CH34x ELF itself enumerates its matching devices, not compiled core.
 * The host owns token generations; uncertain identification never produces
 * an empty healthy inventory or a partial result that could revoke leases. */
static bool snapshot_devices(risc_serial_device_v1 *out, size_t *inout_count) {
    if (!host || !discovery || !inout_count) return false;
    size_t processed = 0;
    if (!discovery->poll(host->context, 16, &processed)) return false;
    uint64_t tokens[RISC_USB_HOST_MAX_DEVICES] = {0};
    size_t count = RISC_USB_HOST_MAX_DEVICES;
    if (!discovery->devices(host->context, tokens, &count) ||
        count > RISC_USB_HOST_MAX_DEVICES) return false;
    risc_serial_device_v1 found[RISC_USB_HOST_MAX_DEVICES] = {{0}};
    size_t matches = 0;
    for (size_t i = 0; i < count; ++i) {
        if (!tokens[i]) return false;
        const int32_t result = probe_device(tokens[i]);
        if (result < 0) return false;
        if (!result) continue;
        found[matches].provider_device = tokens[i];
        found[matches].generation = tokens[i];
        found[matches].transport = RISC_SERIAL_TRANSPORT_USB;
        ++matches;
    }
    if (*inout_count < matches || (matches && !out)) {
        *inout_count = matches;
        return false;
    }
    reconcile_endpoints(tokens, count);
    for (size_t i = 0; i < matches; ++i) out[i] = found[i];
    *inout_count = matches;
    return true;
}
static uint64_t open_device(uint64_t device) {
    if (!host || !discovery || !device || sequence == UINT64_MAX) return 0;
    ch_session *slot = 0;
    for (size_t i = 0; i < RISC_USB_CDC_MAX_SESSIONS; ++i)
        if (!sessions[i].token && !sessions[i].claim) { slot = &sessions[i]; break; }
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
    if (command(candidate.claim, 0xc0u, 0x5fu, 0, 0, version, 2) != 2 ||
        command(candidate.claim, 0x40u, 0xa1u, 0, 0, 0, 0) != 0) {
        /* Claim was acquired, but the open failed before returning a token.
         * Retain an orphan in this slot if physical release is uncertain. */
        if (!discovery->release_checked(host->context, candidate.claim)) {
            candidate.orphaned = true;
            *slot = candidate;
        }
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
    if (s->version < 0x30u && (bits != 8 || parity != 0 || stops != 1))
        return false;
    uint16_t value = 0;
    if (!divisor(baud, s->version, &value) ||
        command(s->claim, 0x40u, 0x9au, 0x1312u, value, 0, 0) != 0)
        return false;
    if (s->version >= 0x30u) {
        uint8_t lcr = (uint8_t)(0xc0u | (bits - 5u));
        switch (parity) {
            case 1: lcr |= 0x08u; break;
            case 2: lcr |= 0x18u; break;
            case 3: lcr |= 0x28u; break;
            case 4: lcr |= 0x38u; break;
            default: break;
        }
        if (stops == 2) lcr |= 0x04u;
        if (command(s->claim, 0x40u, 0x9au, 0x2518u, lcr, 0, 0)
            != 0) return false;
    }
    return true;
}
static bool control_lines(uint64_t token, bool dtr, bool rts) {
    ch_session *s = lookup(token);
    if (!s) return false;
    uint8_t control = 0;
    if (rts) control |= 0x40u;
    if (dtr) control |= 0x20u;
    return command(s->claim, 0x40u, 0xa4u,
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
    close_endpoints(s);
    /* A false result preserves the caller's exact session and claim, so the
     * next close retries the same physical token without a second owner. */
    return release_session(s);
}
static const risc_serial_port_streams_v1 capability = {
    {{{RISC_USB_CDC_API_V1, sizeof(risc_serial_port_streams_v1),
       open_device, configure, control_lines, legacy_read, legacy_write, close_device},
      probe_device}, snapshot_devices}, endpoints
};
static const risc_driver_poll_v2 driver = {
    {{RISC_PROVIDER_DRIVER_ABI_V2, sizeof(risc_driver_poll_v2),
      "usb-ch34x-v2", "serial.port", RISC_USB_CDC_API_V1,
      &capability.inventory.discovery.serial, start, stop, quiesce}, last_error, bind_streams},
    poll_streams
};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &driver.streams.driver : 0;
}

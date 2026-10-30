#include "RiscUsbControllerV1.h"
#include "RiscSerialPortV1.h"

/* The class ELF owns CDC parsing, control, sessions and I/O; usb.host owns
 * physical claims and transfers. No USB protocol work lives in firmware. */
typedef struct {
    uint64_t token;
    uint64_t device;
    uint64_t control_claim;
    uint64_t data_claim;
    uint8_t control_interface;
    uint8_t data_interface;
    uint8_t data_alternate;
    uint8_t ep_in;
    uint8_t ep_out;
    bool orphaned; /* Failed open: token was never returned to a consumer. */
    uint32_t rx, tx;
    uint32_t rx_size, rx_offset, tx_size, tx_offset;
    bool failed;
} cdc_session;
static const risc_usb_host_api_v1 *host;
static const risc_usb_host_discovery_v1 *host_discovery;
static cdc_session sessions[RISC_USB_CDC_MAX_SESSIONS];
static uint8_t config_bytes[RISC_USB_CONFIG_LIMIT];
static uint64_t generation;
typedef cdc_session serial_session;
#include "../common/SerialStreamPump.inc"

static bool same(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *b && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static cdc_session *lookup(uint64_t token) {
    if (!host || !token) return 0;
    for (size_t i = 0; i < RISC_USB_CDC_MAX_SESSIONS; ++i)
        if (sessions[i].token == token) return &sessions[i];
    return 0;
}
/* A successful data release may precede a failed control release. Retain
 * just the outstanding claim and exact session token for the next retry. */
static bool release_session(cdc_session *s) {
    if (!host_discovery || !s) return false;
    if (s->data_claim && s->data_claim != s->control_claim) {
        if (!host_discovery->release_checked(host->context, s->data_claim))
            return false;
        s->data_claim = 0;
    }
    if (s->control_claim) {
        if (!host_discovery->release_checked(host->context, s->control_claim))
            return false;
        s->control_claim = 0;
    }
    *s = (cdc_session){0};
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
    const risc_usb_host_discovery_v1 *discovery =
        (const risc_usb_host_discovery_v1 *)api;
    if (!discovery->poll || !discovery->devices ||
        !discovery->release_checked || !discovery->control_claim) {
        start_error = "start: usb.host discovery callbacks incomplete";
        return false;
    }
    host = api;
    host_discovery = discovery;
    return true;
}
static bool quiesce(void) {
    /* Never close live consumer sessions. Only retry claims from failed open
     * operations, which no consumer can close because no token was returned.
     * A single pass of at most four sessions is the complete retry budget. */
    for (size_t i = 0; i < RISC_USB_CDC_MAX_SESSIONS; ++i) {
        if (sessions[i].orphaned && !release_session(&sessions[i])) return false;
        if (sessions[i].token) return false;
    }
    return true;
}
static void stop(void) {
    if (!host) return;
    /* Direct stop cannot discard an uncertain physical release. */
    for (size_t i = 0; i < RISC_USB_CDC_MAX_SESSIONS; ++i)
        if (sessions[i].token) {
            close_endpoints(&sessions[i]);
            if (!release_session(&sessions[i])) return;
        }
    if (!quiesce()) return;
    host_discovery = 0;
    host = 0;
    streams = 0;
    next_session = 0;
}

/* Full descriptor scan: composite interfaces, CDC union, IAD and alternate
 * selection; reject ambiguity or malformed lengths without claiming. */
static bool parse(size_t length, cdc_session *out) {
    if (!out || length < 9 || length > RISC_USB_CONFIG_LIMIT ||
        config_bytes[0] != 9 || config_bytes[1] != 2) return false;
    size_t total = (size_t)config_bytes[2] | ((size_t)config_bytes[3] << 8);
    if (total < 9 || total != length) return false;
    uint8_t control = 0xff, only_data = 0xff;
    uint8_t control_count = 0, data_count = 0;
    for (size_t at = 0; at < total;) {
        if (total - at < 2) return false;
        uint8_t n = config_bytes[at], type = config_bytes[at + 1];
        if (n < 2 || n > total - at) return false;
        if (type == 4) {
            if (n < 9) return false;
            uint8_t iface = config_bytes[at + 2];
            uint8_t alt = config_bytes[at + 3];
            uint8_t cls = config_bytes[at + 5];
            uint8_t subcls = config_bytes[at + 6];
            if (alt == 0 && cls == 2 && subcls == 2) {
                if (++control_count != 1) return false;
                control = iface;
            } else if (alt == 0 && cls == 10) {
                if (++data_count == 0) return false;
                only_data = iface;
            }
        }
        at += n;
    }
    if (control_count != 1 || !data_count) return false;
    uint8_t data = 0xff;
    bool has_union = false, has_iad = false;
    uint8_t iad_first = 0, iad_end = 0;
    for (size_t at = 0; at < total;) {
        uint8_t n = config_bytes[at], type = config_bytes[at + 1];
        if (type == 0x24 && n >= 4 && config_bytes[at + 2] == 6 &&
            config_bytes[at + 3] == control) {
            if (has_union || n != 5) return false;
            has_union = true;
            data = config_bytes[at + 4];
        } else if (type == 11) {
            if (n < 8) return false;
            uint8_t first = config_bytes[at + 2];
            uint8_t count = config_bytes[at + 3];
            if (!count || (unsigned)first + count > 256u) return false;
            if (config_bytes[at + 4] == 2 && config_bytes[at + 5] == 2 &&
                control >= first && (unsigned)control < (unsigned)first + count) {
                if (has_iad) return false;
                has_iad = true;
                iad_first = first;
                iad_end = (uint8_t)((unsigned)first + count - 1u);
            }
        }
        at += n;
    }
    if (!has_union) {
        if (data_count != 1) return false;
        data = only_data;
    }
    if (data == control || data == 0xff ||
        (has_iad && (data < iad_first || data > iad_end))) return false;
    bool found_data = false;
    for (size_t at = 0; at < total;) {
        uint8_t n = config_bytes[at];
        if (config_bytes[at + 1] == 4 && config_bytes[at + 2] == data &&
            config_bytes[at + 3] == 0 && config_bytes[at + 5] == 10)
            found_data = true;
        at += n;
    }
    if (!found_data) return false;
    uint8_t iface = 0xff, alt = 0xff, cls = 0xff;
    uint8_t in = 0, out_ep = 0, chosen_alt = 0;
    uint8_t chosen_in = 0, chosen_out = 0;
    bool chosen = false, duplicate_endpoint = false;
    for (size_t at = 0; at < total;) {
        uint8_t n = config_bytes[at], type = config_bytes[at + 1];
        if (type == 4) {
            if (iface == data && cls == 10) {
                if (duplicate_endpoint) return false;
                if (in && out_ep) {
                    if (chosen) return false;
                    chosen = true;
                    chosen_alt = alt;
                    chosen_in = in;
                    chosen_out = out_ep;
                }
            }
            iface = config_bytes[at + 2];
            alt = config_bytes[at + 3];
            cls = config_bytes[at + 5];
            in = out_ep = 0;
            duplicate_endpoint = false;
        } else if (type == 5 && iface == data && cls == 10) {
            if (n < 7) return false;
            uint8_t ep = config_bytes[at + 2];
            uint16_t mps = (uint16_t)config_bytes[at + 4] |
                           ((uint16_t)config_bytes[at + 5] << 8);
            if ((config_bytes[at + 3] & 3u) == 2 && mps && mps <= 512) {
                if ((ep & 0x0fu) == 0 || (ep & 0x70u)) return false;
                if (ep & 0x80u) {
                    if (in) duplicate_endpoint = true;
                    else in = ep;
                } else {
                    if (out_ep) duplicate_endpoint = true;
                    else out_ep = ep;
                }
            }
        }
        at += n;
    }
    if (iface == data && cls == 10) {
        if (duplicate_endpoint) return false;
        if (in && out_ep) {
            if (chosen) return false;
            chosen = true;
            chosen_alt = alt;
            chosen_in = in;
            chosen_out = out_ep;
        }
    }
    if (!chosen) return false;
    out->control_interface = control;
    out->data_interface = data;
    out->data_alternate = chosen_alt;
    out->ep_in = chosen_in;
    out->ep_out = chosen_out;
    return true;
}
/* Provider-owned matching is read-only. A failed descriptor read is UNKNOWN,
 * not a nonmatch that authorizes trying a different class. */
static int32_t probe_device(uint64_t device) {
    if (!host || !device) return -1;
    size_t length = sizeof(config_bytes);
    uint16_t vid = 0, pid = 0;
    if (!host->configuration(host->context, device, config_bytes, &length,
                             &vid, &pid)) return -1;
    (void)vid; (void)pid;
    cdc_session candidate = {0};
    return parse(length, &candidate) ? 1 : 0;
}

/* CDC itself announces coherent, generation-bound devices. A descriptor
 * failure makes the entire inventory uncertain; no claimed session is
 * touched, no partial enumeration is exposed and no firmware USB scan runs. */
static bool snapshot_devices(risc_serial_device_v1 *out, size_t *inout_count) {
    if (!host || !host_discovery || !inout_count) return false;
    size_t processed = 0;
    if (!host_discovery->poll(host->context, 16, &processed)) return false;
    uint64_t tokens[RISC_USB_HOST_MAX_DEVICES] = {0};
    size_t count = RISC_USB_HOST_MAX_DEVICES;
    if (!host_discovery->devices(host->context, tokens, &count) ||
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
    if (!host || !device || generation == UINT64_MAX) return 0;
    cdc_session *slot = 0;
    for (size_t i = 0; i < RISC_USB_CDC_MAX_SESSIONS; ++i)
        if (!sessions[i].token) { slot = &sessions[i]; break; }
    if (!slot) return 0;
    size_t length = sizeof(config_bytes);
    uint16_t vid = 0, pid = 0;
    if (!host->configuration(host->context, device, config_bytes, &length,
                             &vid, &pid)) return 0;
    (void)vid; (void)pid;
    cdc_session candidate = {0};
    if (!parse(length, &candidate)) return 0;
    candidate.device = device;
    if (!host->claim(host->context, device, candidate.control_interface, 0,
                     &candidate.control_claim) || !candidate.control_claim)
        return 0;
    ++generation;
    candidate.token = generation;
    *slot = candidate;
    if (!host->claim(host->context, device, candidate.data_interface,
                     candidate.data_alternate, &slot->data_claim) ||
        !slot->data_claim) {
        /* The token is not returned, so quiesce must own later retries. */
        slot->orphaned = true;
        (void)release_session(slot);
        return 0;
    }
    return slot->token;
}
static bool configure(uint64_t token, uint32_t baud, uint8_t bits,
                      uint8_t parity, uint8_t stop_bits) {
    cdc_session *s = lookup(token);
    if (!s || s->orphaned || !s->control_claim || !s->data_claim ||
        baud < 300 || baud > 3000000 || bits < 5 || bits > 8 ||
        parity > 4 || (stop_bits != 1 && stop_bits != 2)) return false;
    uint8_t payload[7] = {(uint8_t)baud, (uint8_t)(baud >> 8),
        (uint8_t)(baud >> 16), (uint8_t)(baud >> 24),
        stop_bits == 2 ? 2u : 0u, parity, bits};
    return host_discovery->control_claim(host->context, s->control_claim, 0x21, 0x20, 0,
                                         s->control_interface, payload, 7, 1000) == 7;
}
static bool control_lines(uint64_t token, bool dtr, bool rts) {
    cdc_session *s = lookup(token);
    if (!s || s->orphaned || !s->control_claim || !s->data_claim) return false;
    uint16_t value = (uint16_t)((dtr ? 1u : 0u) | (rts ? 2u : 0u));
    return host_discovery->control_claim(host->context, s->control_claim, 0x21, 0x22, value,
                                         s->control_interface, 0, 0, 1000) == 0;
}
static int32_t read_data(uint64_t token, uint8_t *dst, size_t capacity,
                         uint32_t timeout_ms) {
    cdc_session *s = lookup(token);
    if (!s || s->orphaned || !s->data_claim || !dst || !capacity ||
        capacity > RISC_USB_CONFIG_LIMIT) return -1;
    return host->bulk_read(host->context, s->data_claim, s->ep_in,
                           dst, capacity, timeout_ms);
}
static int32_t write_data(uint64_t token, const uint8_t *src, size_t length,
                          uint32_t timeout_ms) {
    cdc_session *s = lookup(token);
    if (!s || s->orphaned || !s->data_claim || !src || !length ||
        length > RISC_USB_CONFIG_LIMIT) return -1;
    return host->bulk_write(host->context, s->data_claim, s->ep_out,
                            src, length, timeout_ms);
}
static bool close_device(uint64_t token) {
    cdc_session *s = lookup(token);
    if (!s) return false;
    close_endpoints(s);
    return release_session(s);
}
static const risc_serial_port_streams_v1 capability = {
    {{{RISC_USB_CDC_API_V1, sizeof(risc_serial_port_streams_v1),
       open_device, configure, control_lines, legacy_read, legacy_write, close_device},
      probe_device}, snapshot_devices}, endpoints
};
static const risc_driver_poll_v2 driver = {
    {{RISC_PROVIDER_DRIVER_ABI_V2, sizeof(risc_driver_poll_v2),
      "usb-cdc-acm", "serial.port", RISC_USB_CDC_API_V1,
      &capability.inventory.discovery.serial, start, stop, quiesce}, last_error, bind_streams},
    poll_streams
};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &driver.streams.driver : 0;
}

#include "RiscUsbControllerV1.h"
#include "RiscSerialPortV1.h"

/* Installed usb.host owns physical USB. This class ELF owns CP210x vendor
 * controls, descriptor matching and generation-qualified serial sessions. */
typedef struct {
    uint64_t token, device, claim;
    uint8_t interface_number, alternate, in_ep, out_ep;
    bool disabled;
    bool orphaned; /* Open failed; its session token was never returned. */
    uint32_t rx, tx;
    uint32_t rx_size, rx_offset, tx_size, tx_offset;
    bool failed;
} cp_session;
static const risc_usb_host_api_v1 *host;
static const risc_usb_host_discovery_v1 *discovery;
static cp_session sessions[RISC_USB_CDC_MAX_SESSIONS];
static uint8_t descriptors[RISC_USB_CONFIG_LIMIT];
static uint64_t generation;
typedef cp_session serial_session;
#include "../common/SerialStreamPump.inc"

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
    if (!equal(deps[0].capability_id, "usb.host")) {
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
    const risc_usb_host_discovery_v1 *extended =
        (const risc_usb_host_discovery_v1 *)api;
    /* A detached CP210x cannot acknowledge its UART-disable vendor request.
     * Require provider-owned discovery to distinguish detach from failed I/O. */
    if (!extended->poll || !extended->devices || !extended->release_checked ||
        !extended->control_claim) {
        start_error = "start: usb.host discovery callbacks incomplete";
        return false;
    }
    host = api;
    discovery = extended;
    return true;
}
static int32_t command(uint64_t claim, uint8_t iface, uint8_t req,
                       uint16_t value, uint8_t *payload, uint16_t length) {
    if (!discovery || !claim) return -1;
    return discovery->control_claim(host->context, claim, 0x41u, req, value, iface,
                                    payload, length, 1000);
}

/* The host ELF is the sole authority for physical presence. A failed poll or
 * inventory is UNKNOWN, not detach: keep the claim and dependent VBUS lease.
 * Poll before snapshot so a queued unplug cannot strand UART disable forever.
 * The eight-device scan is bounded; never infer detach from a vendor NACK. */
static bool attached(uint64_t device, bool *present) {
    if (!host || !discovery || !device || !present) return false;
    size_t processed = 0;
    if (!discovery->poll(host->context, 16, &processed)) return false;
    uint64_t tokens[RISC_USB_HOST_MAX_DEVICES] = {0};
    size_t count = RISC_USB_HOST_MAX_DEVICES;
    if (!discovery->devices(host->context, tokens, &count) ||
        count > RISC_USB_HOST_MAX_DEVICES) return false;
    *present = false;
    for (size_t i = 0; i < count; ++i)
        if (tokens[i] == device) { *present = true; break; }
    return true;
}

/* UART disable and claim release are independent acknowledged stages. If the
 * host confirms detach, USB vendor controls can no longer succeed: skip that
 * impossible operation and release the SAME claim through the checked API.
 * While attached, failed UART disable remains uncertain and pins the claim. */
static bool close_device(uint64_t token) {
    cp_session *s = lookup(token);
    if (!s || !discovery) return false;
    close_endpoints(s);
    if (!s->disabled) {
        bool present = false;
        if (!attached(s->device, &present)) return false;
        if (present && command(s->claim, s->interface_number, 0x00u, 0, 0, 0) != 0)
            return false;
        s->disabled = true;
    }
    if (!discovery->release_checked(host->context, s->claim)) return false;
    *s = (cp_session){0};
    return true;
}
static bool quiesce(void) {
    /* Consumer-owned sessions remain open. Failed-open claims have no caller
     * token, so retry them here with a fixed four-slot work bound. */
    for (size_t i = 0; i < RISC_USB_CDC_MAX_SESSIONS; ++i) {
        if (sessions[i].orphaned && !close_device(sessions[i].token)) return false;
        if (sessions[i].token) return false;
    }
    return true;
}
static void stop(void) {
    /* A direct stop cannot bypass physical release or discard claim tokens. */
    if (!host) return;
    for (size_t i = 0; i < RISC_USB_CDC_MAX_SESSIONS; ++i)
        if (sessions[i].token && !close_device(sessions[i].token)) return;
    if (!quiesce()) return;
    discovery = 0;
    host = 0;
    streams = 0;
    next_session = 0;
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
/* Read-only class probe: do not enable UART or claim an interface merely to
 * decide whether a package matches. A host/descriptor failure is UNKNOWN. */
static int32_t probe_device(uint64_t device) {
    if (!host || !device) return -1;
    size_t length = sizeof(descriptors);
    uint16_t vid = 0, pid = 0;
    if (!host->configuration(host->context, device, descriptors, &length,
                             &vid, &pid)) return -1;
    (void)pid;
    if (vid != 0x10c4u) return 0;
    cp_session candidate = {0};
    return parse(length, &candidate) ? 1 : 0;
}

/* Semantic device inventory originates inside the installed CP210x ELF.
 * No USB descriptors or vendor policies cross into generic core; a single
 * failed identity poll makes the entire snapshot UNKNOWN, not detached.
 * In particular, never hand a partial device set to the registry. */
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
        const int32_t decision = probe_device(tokens[i]);
        if (decision < 0) return false;
        if (decision == 0) continue;
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
    cp_session *slot = 0;
    for (size_t i = 0; i < RISC_USB_CDC_MAX_SESSIONS; ++i)
        if (!sessions[i].token) { slot = &sessions[i]; break; }
    if (!slot) return 0;
    size_t length = sizeof(descriptors);
    uint16_t vid = 0, pid = 0;
    if (!host->configuration(host->context, device, descriptors, &length, &vid, &pid) ||
        vid != 0x10c4u) return 0;
    (void)pid;
    cp_session candidate = {0};
    if (!parse(length, &candidate)) return 0;
    candidate.device = device;
    if (!host->claim(host->context, device, candidate.interface_number,
                     candidate.alternate, &candidate.claim) || !candidate.claim) return 0;
    ++generation;
    candidate.token = generation;
    *slot = candidate;
    if (command(candidate.claim, candidate.interface_number, 0x00u, 1u, 0, 0) != 0) {
        slot->orphaned = true;
        (void)close_device(candidate.token);
        return 0;
    }
    return candidate.token;
}
static bool configure(uint64_t token, uint32_t baud, uint8_t bits,
                      uint8_t parity, uint8_t stop_bits) {
    cp_session *s = lookup(token);
    if (!s || s->disabled || s->orphaned || baud < 300 || baud > 3000000 ||
        bits < 5 || bits > 8 || parity > 4 ||
        (stop_bits != 1 && stop_bits != 2)) return false;
    uint8_t speed[4] = {(uint8_t)baud, (uint8_t)(baud >> 8),
                        (uint8_t)(baud >> 16), (uint8_t)(baud >> 24)};
    if (command(s->claim, s->interface_number, 0x1eu, 0, speed, 4) != 4) return false;
    uint16_t line = (uint16_t)bits << 8;
    if (parity) line |= (uint16_t)parity << 4;
    if (stop_bits == 2) line |= 2u;
    return command(s->claim, s->interface_number, 0x03u, line, 0, 0) == 0;
}
static bool control_lines(uint64_t token, bool dtr, bool rts) {
    cp_session *s = lookup(token);
    if (!s || s->disabled || s->orphaned) return false;
    uint16_t value = (uint16_t)(0x0300u | (dtr ? 1u : 0u) |
                                (rts ? 2u : 0u));
    return command(s->claim, s->interface_number, 0x07u, value, 0, 0) == 0;
}
static int32_t read_data(uint64_t token, uint8_t *dst, size_t capacity,
                         uint32_t timeout_ms) {
    cp_session *s = lookup(token);
    if (!s || s->disabled || s->orphaned || !dst || !capacity ||
        capacity > RISC_USB_CONFIG_LIMIT) return -1;
    int32_t n = host->bulk_read(host->context, s->claim, s->in_ep,
                                dst, capacity, timeout_ms);
    return n >= 0 && (size_t)n <= capacity ? n : -1;
}
static int32_t write_data(uint64_t token, const uint8_t *src, size_t length,
                          uint32_t timeout_ms) {
    cp_session *s = lookup(token);
    if (!s || s->disabled || s->orphaned || !src || !length ||
        length > RISC_USB_CONFIG_LIMIT) return -1;
    int32_t n = host->bulk_write(host->context, s->claim, s->out_ep,
                                 src, length, timeout_ms);
    return n >= 0 && (size_t)n <= length ? n : -1;
}
static const risc_serial_port_streams_v1 capability = {
    {{{RISC_USB_CDC_API_V1, sizeof(risc_serial_port_streams_v1),
       open_device, configure, control_lines, legacy_read, legacy_write, close_device},
      probe_device}, snapshot_devices}, endpoints
};
static const risc_driver_poll_v2 driver = {
    {{RISC_PROVIDER_DRIVER_ABI_V2, sizeof(risc_driver_poll_v2),
      "usb-cp210x-v2", "serial.port", RISC_USB_CDC_API_V1,
      &capability.inventory.discovery.serial, start, stop, quiesce}, last_error, bind_streams},
    poll_streams
};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &driver.streams.driver : 0;
}

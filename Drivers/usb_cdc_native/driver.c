/* Native CDC candidate. One serialized provider executor owns all state.
 * Uses existing serial/session/clock ABIs; never polls the discovery daemon.
 * Native calls retain exact claims and queues on uncertain physical closure.
 * The historical usb_cdc_v2 source and replay artifacts remain unchanged. */
#include "RiscUsbControllerV1.h"
#include "RiscSerialPortV1.h"
#include "RiscSerialStreamSessionV1.h"
#include "RiscStreamSessionProviderV1.h"
#include "RiscPlatformClockV1.h"
#include <string.h>

typedef struct {
    uint64_t token, device, control_claim, data_claim;
    uint8_t control_interface, data_interface, data_alternate, ep_in, ep_out;
    bool native, orphaned, stopped, paused, rx_finished, tx_finished;
    uint32_t rx, tx, rx_size, rx_offset, tx_size, tx_offset;
} cdc_session;
static const risc_usb_host_api_v1 *host;
static const risc_usb_host_discovery_v1 *discovery;
static const risc_platform_clock_api_v1 *clock_api;
static const risc_stream_provider_v1 *streams;
static cdc_session sessions[RISC_USB_CDC_MAX_SESSIONS];
static struct { uint8_t rx[512], tx[512]; } staging[RISC_USB_CDC_MAX_SESSIONS];
static uint8_t config_bytes[RISC_USB_CONFIG_LIMIT];
static uint64_t generation;
static size_t next_session;
typedef struct { uint64_t start; uint32_t ms; } budget;
static budget begin(uint32_t ms) {
    budget b = {UINT64_MAX, ms};
    if (clock_api && ms && ms <= 1000) b.start = clock_api->monotonic_ms(clock_api->context);
    return b;
}
static uint32_t remaining(const budget *b) {
    if (!clock_api || b->start == UINT64_MAX) return 0;
    const uint64_t now = clock_api->monotonic_ms(clock_api->context);
    if (now == UINT64_MAX || now < b->start || now - b->start >= b->ms) return 0;
    return b->ms - (uint32_t)(now - b->start);
}
static bool within(const budget *b) {
    if (!clock_api || b->start == UINT64_MAX) return false;
    const uint64_t now = clock_api->monotonic_ms(clock_api->context);
    return now != UINT64_MAX && now >= b->start && now - b->start <= b->ms;
}
static cdc_session *lookup(uint64_t token) {
    if (!host || !token) return 0;
    for (size_t i = 0; i < RISC_USB_CDC_MAX_SESSIONS; ++i)
        if (sessions[i].token == token) return &sessions[i];
    return 0;
}
#include "Descriptors.inc"
static bool config_valid(const risc_serial_stream_config_v1 *c) {
    return c->baud_rate >= 300 && c->baud_rate <= 3000000 &&
        c->data_bits >= 5 && c->data_bits <= 8 && c->parity <= 4 &&
        (c->stop_bits == 1 || c->stop_bits == 2) && !c->flow_control;
}
static int32_t probe_bounded(uint64_t device, const budget *b, cdc_session *parsed) {
    if (!remaining(b)) return RISC_STREAM_TIMEOUT;
    size_t length = sizeof(config_bytes); uint16_t vid = 0, pid = 0;
    if (!host->configuration(host->context, device, config_bytes, &length, &vid, &pid))
        return RISC_STREAM_IO;
    if (!remaining(b)) return RISC_STREAM_TIMEOUT;
    /* Distinguish a definite non-CDC descriptor from malformed/ambiguous CDC.
     * A corrupt descriptor must not masquerade as a healthy empty inventory. */
    if (length < 9 || length > sizeof(config_bytes) || config_bytes[0] != 9 ||
        config_bytes[1] != 2 || ((size_t)config_bytes[2] | ((size_t)config_bytes[3] << 8)) != length)
        return RISC_STREAM_IO;
    bool cdc = false;
    for (size_t at = 0; at < length;) {
        if (length - at < 2) return RISC_STREAM_IO;
        uint8_t n = config_bytes[at], kind = config_bytes[at + 1];
        if (n < 2 || n > length - at || (kind == 4 && n < 9) ||
            (kind == 5 && n < 7) || (kind == 11 && n < 8)) return RISC_STREAM_IO;
        if (kind == 4 && config_bytes[at + 3] == 0 && config_bytes[at + 5] == 2 && config_bytes[at + 6] == 2) cdc = true;
        at += n;
    }
    if (!cdc) return 0;
    return parse(length, parsed) ? 1 : RISC_STREAM_IO;
}
/* Only consume the host's coherent, already-published generation snapshot.
 * Discovery/enumeration/role/power work is explicitly an outer integration step.
 * A malformed, partial or failed list never means absence. */
static int32_t inventory(risc_serial_device_v1 *out, size_t *count, const budget *b) {
    if (!host || !discovery || !count || !remaining(b)) return RISC_STREAM_TIMEOUT;
    uint64_t tokens[RISC_USB_HOST_MAX_DEVICES] = {0};
    size_t n = RISC_USB_HOST_MAX_DEVICES;
    if (!discovery->devices(host->context, tokens, &n) || n > RISC_USB_HOST_MAX_DEVICES)
        return RISC_STREAM_IO;
    risc_serial_device_v1 found[RISC_USB_HOST_MAX_DEVICES] = {{0}};
    size_t matches = 0;
    for (size_t i = 0; i < n; ++i) {
        if (!tokens[i]) return RISC_STREAM_IO;
        for (size_t j = 0; j < i; ++j) if (tokens[j] == tokens[i]) return RISC_STREAM_IO;
        cdc_session parsed = {0};
        int32_t rc = probe_bounded(tokens[i], b, &parsed);
        if (rc < 0) return rc;
        if (!rc) continue;
        found[matches++] = (risc_serial_device_v1){tokens[i], tokens[i], RISC_SERIAL_TRANSPORT_USB, {0}};
    }
    if (!within(b)) return RISC_STREAM_TIMEOUT;
    if (*count < matches || (matches && !out)) { *count = matches; return RISC_STREAM_LIMIT; }
    for (size_t i = 0; i < matches; ++i) out[i] = found[i];
    *count = matches;
    return RISC_STREAM_OK;
}
static int32_t check_device(uint64_t device, uint64_t gen, const budget *b) {
    risc_serial_device_v1 list[RISC_USB_HOST_MAX_DEVICES] = {{0}};
    size_t n = RISC_USB_HOST_MAX_DEVICES;
    int32_t rc = inventory(list, &n, b);
    if (rc != RISC_STREAM_OK) return rc;
    for (size_t i = 0; i < n; ++i)
        if (list[i].provider_device == device && list[i].generation == gen) return RISC_STREAM_OK;
    return RISC_STREAM_DISCONNECTED;
}
static bool snapshot(risc_serial_device_v1 *out, size_t *count) {
    budget b = begin(1000);
    return inventory(out, count, &b) == RISC_STREAM_OK;
}
static int32_t probe(uint64_t device) {
    if (!host || !device) return -1;
    budget b = begin(1000); cdc_session parsed = {0};
    return probe_bounded(device, &b, &parsed);
}
/* Mark stopped before any fallible operation. Finish revokes queue data use,
 * never destroys a queue or staging buffer; close waits for both claim receipts.
 * An expired budget makes no further host/queue callback, including cleanup. */
static bool finish_queues(cdc_session *s, int32_t reason, const budget *b) {
    s->stopped = true;
    if (s->rx && !s->rx_finished) {
        if (!remaining(b) || streams->finish(streams->context, s->rx, reason) != RISC_STREAM_OK) return false;
        s->rx_finished = true;
    }
    if (s->tx && !s->tx_finished) {
        if (!remaining(b) || streams->finish(streams->context, s->tx, reason) != RISC_STREAM_OK) return false;
        s->tx_finished = true;
    }
    return true;
}
static int32_t close_bounded(cdc_session *s, const budget *b) {
    if (!finish_queues(s, RISC_STREAM_CANCELLED, b)) return RISC_STREAM_RETAINED;
    if (s->data_claim && s->data_claim != s->control_claim) {
        if (!remaining(b) || !discovery->release_checked(host->context, s->data_claim)) return RISC_STREAM_RETAINED;
        s->data_claim = 0;
    }
    if (s->control_claim) {
        if (!remaining(b) || !discovery->release_checked(host->context, s->control_claim)) return RISC_STREAM_RETAINED;
        s->data_claim = s->control_claim = 0;
    }
    /* Physical ownership is gone, but failed queue closure still pins the slot. */
    if (s->rx) {
        if (!remaining(b) || streams->close(streams->context, s->rx) != RISC_STREAM_OK) return RISC_STREAM_RETAINED;
        s->rx = 0;
    }
    if (s->tx) {
        if (!remaining(b) || streams->close(streams->context, s->tx) != RISC_STREAM_OK) return RISC_STREAM_RETAINED;
        s->tx = 0;
    }
    if (!within(b)) return RISC_STREAM_RETAINED;
    *s = (cdc_session){0};
    return RISC_STREAM_OK;
}
static int32_t claims_open(uint64_t device, bool native, const budget *b, cdc_session **out) {
    *out = 0;
    if (generation == UINT64_MAX) return RISC_STREAM_LIMIT;
    cdc_session *s = 0;
    for (size_t i = 0; i < RISC_USB_CDC_MAX_SESSIONS; ++i) {
        if (sessions[i].token && sessions[i].device == device) return RISC_STREAM_BUSY;
        if (!sessions[i].token && !s) s = &sessions[i];
    }
    if (!s) return RISC_STREAM_LIMIT;
    cdc_session candidate = {0};
    int32_t rc = probe_bounded(device, b, &candidate);
    if (rc != 1) return rc < 0 ? rc : RISC_STREAM_UNSUPPORTED;
    if (!remaining(b)) return RISC_STREAM_TIMEOUT;
    candidate.device = device; candidate.native = native;
    candidate.token = ++generation;
    *s = candidate; *out = s;
    bool ok = host->claim(host->context, device, s->control_interface, 0, &s->control_claim);
    if (!ok || !s->control_claim) return RISC_STREAM_IO;
    if (!remaining(b)) return RISC_STREAM_TIMEOUT;
    ok = host->claim(host->context, device, s->data_interface, s->data_alternate, &s->data_claim);
    if (!ok || !s->data_claim) return RISC_STREAM_IO;
    return within(b) ? RISC_STREAM_OK : RISC_STREAM_TIMEOUT;
}
static int32_t configure_bounded(cdc_session *s, const risc_serial_stream_config_v1 *c, const budget *b) {
    uint32_t ms = remaining(b);
    if (!ms) return RISC_STREAM_TIMEOUT;
    uint32_t baud = c->baud_rate;
    uint8_t data[7] = {(uint8_t)baud, (uint8_t)(baud >> 8), (uint8_t)(baud >> 16),
        (uint8_t)(baud >> 24), c->stop_bits == 2 ? 2u : 0u, c->parity, c->data_bits};
    int32_t rc = discovery->control_claim(host->context, s->control_claim, 0x21, 0x20, 0,
        s->control_interface, data, 7, ms);
    if (!within(b)) return RISC_STREAM_TIMEOUT;
    return rc == 7 ? RISC_STREAM_OK : RISC_STREAM_IO;
}
static int32_t lines_bounded(cdc_session *s, bool dtr, bool rts, const budget *b) {
    uint32_t ms = remaining(b);
    if (!ms) return RISC_STREAM_TIMEOUT;
    int32_t rc = discovery->control_claim(host->context, s->control_claim, 0x21, 0x22,
        (uint16_t)((dtr ? 1u : 0u) | (rts ? 2u : 0u)), s->control_interface, 0, 0, ms);
    if (!within(b)) return RISC_STREAM_TIMEOUT;
    return rc == 0 ? RISC_STREAM_OK : RISC_STREAM_IO;
}
static int32_t publish(cdc_session *s, const budget *b) {
    if (!streams) return RISC_STREAM_UNSUPPORTED;
    risc_stream_endpoint_v1 e = {sizeof(e), 1, RISC_STREAM_READ, 2048, 0, 0, 0};
    if (!remaining(b)) return RISC_STREAM_TIMEOUT;
    if (streams->publish(streams->context, &e, &s->rx) != RISC_STREAM_OK || !s->rx) return RISC_STREAM_IO;
    e.rights = RISC_STREAM_WRITE;
    if (!remaining(b)) return RISC_STREAM_TIMEOUT;
    if (streams->publish(streams->context, &e, &s->tx) != RISC_STREAM_OK || !s->tx || s->tx == s->rx) return RISC_STREAM_IO;
    return within(b) ? RISC_STREAM_OK : RISC_STREAM_TIMEOUT;
}
static void output(const cdc_session *s, risc_provider_stream_session_v1 *out) {
    out->session = s->token; out->rx_endpoint = s->rx; out->tx_endpoint = s->tx;
}
static int32_t open_session(const void *bytes, uint32_t size, uint32_t ms, risc_provider_stream_session_v1 *out) {
    budget b = begin(ms);
    if (out && out->struct_size >= sizeof(*out)) {
        out->reserved = out->rx_endpoint = out->tx_endpoint = 0; out->session = 0;
    }
    if (!host || !streams || !bytes || size != sizeof(risc_serial_stream_open_v1) ||
        !ms || ms > 1000 || !out || out->struct_size < sizeof(*out)) return RISC_STREAM_INVALID;
    risc_serial_stream_open_v1 r; memcpy(&r, bytes, sizeof(r));
    if (r.api_version != 1 || r.struct_size != sizeof(r) || !r.provider_device ||
        !r.device_generation || !config_valid(&r.config)) return RISC_STREAM_INVALID;
    int32_t rc = check_device(r.provider_device, r.device_generation, &b);
    if (rc != RISC_STREAM_OK) return rc;
    cdc_session *s = 0;
    rc = claims_open(r.provider_device, true, &b, &s);
    if (rc == RISC_STREAM_OK) rc = configure_bounded(s, &r.config, &b);
    if (rc == RISC_STREAM_OK) rc = publish(s, &b);
    if (rc == RISC_STREAM_OK) { output(s, out); return rc; }
    if (!s) return rc;
    if (close_bounded(s, &b) == RISC_STREAM_OK) return rc;
    output(s, out); return RISC_STREAM_RETAINED;
}
static int32_t call_session(uint64_t token, const void *bytes, uint32_t size, uint32_t ms,
        void *reply, uint32_t capacity, uint32_t *actual) {
    (void)reply; (void)capacity;
    budget b = begin(ms);
    if (actual) *actual = 0;
    cdc_session *s = lookup(token);
    if (!s || !s->native || !actual || !bytes || size != sizeof(risc_serial_stream_call_v1) ||
        !ms || ms > 1000) return RISC_STREAM_INVALID;
    if (s->stopped) return RISC_STREAM_CLOSED;
    risc_serial_stream_call_v1 r; memcpy(&r, bytes, sizeof(r));
    if (r.api_version != 1 || r.struct_size != sizeof(r) || r.reserved) return RISC_STREAM_INVALID;
    if (r.operation == RISC_SERIAL_STREAM_CHECK_DEVICE) {
        for (size_t i = 0; i < sizeof(r.value.zero); ++i) if (r.value.zero[i]) return RISC_STREAM_INVALID;
        int32_t rc = check_device(s->device, s->device, &b);
        /* Unknown presence pauses the physical pump without erasing accepted
         * queue bytes. A later complete successful check resumes the same token. */
        s->paused = rc != RISC_STREAM_OK;
        if (rc == RISC_STREAM_DISCONNECTED) (void)finish_queues(s, rc, &b);
        return rc;
    }
    if (r.operation == RISC_SERIAL_STREAM_CONFIGURE) {
        if (!config_valid(&r.value.config)) return RISC_STREAM_INVALID;
        return configure_bounded(s, &r.value.config, &b);
    }
    if (r.operation == RISC_SERIAL_STREAM_CONTROL_LINES) {
        if (r.value.lines.dtr > 1 || r.value.lines.rts > 1) return RISC_STREAM_INVALID;
        for (size_t i = 0; i < sizeof(r.value.lines.reserved); ++i) if (r.value.lines.reserved[i]) return RISC_STREAM_INVALID;
        return lines_bounded(s, r.value.lines.dtr != 0, r.value.lines.rts != 0, &b);
    }
    return RISC_STREAM_UNSUPPORTED;
}
static int32_t close_session(uint64_t token, uint32_t ms) {
    budget b = begin(ms); cdc_session *s = lookup(token);
    if (!s || !s->native || !ms || ms > 1000) return RISC_STREAM_INVALID;
    return close_bounded(s, &b);
}
/* The raw prefix remains callable for old consumers, but cannot adopt or bypass
 * a native session. Legacy failed-open orphan custody belongs to this provider. */
static uint64_t raw_open(uint64_t device) {
    if (!host || !device) return 0;
    budget b = begin(1000); cdc_session *s = 0;
    if (check_device(device, device, &b) != RISC_STREAM_OK) return 0;
    int32_t rc = claims_open(device, false, &b, &s);
    if (rc == RISC_STREAM_OK) return s->token;
    if (s) { s->orphaned = true; (void)close_bounded(s, &b); }
    return 0;
}
static bool raw_configure(uint64_t token, uint32_t baud, uint8_t bits, uint8_t parity, uint8_t stops) {
    cdc_session *s = lookup(token); budget b = begin(1000);
    risc_serial_stream_config_v1 c = {baud, bits, parity, stops, 0};
    return s && !s->native && !s->orphaned && !s->stopped && config_valid(&c) &&
        configure_bounded(s, &c, &b) == RISC_STREAM_OK;
}
static bool raw_lines(uint64_t token, bool dtr, bool rts) {
    cdc_session *s = lookup(token); budget b = begin(1000);
    return s && !s->native && !s->orphaned && !s->stopped && lines_bounded(s, dtr, rts, &b) == RISC_STREAM_OK;
}
static int32_t raw_read(uint64_t token, uint8_t *data, size_t n, uint32_t ms) {
    cdc_session *s = lookup(token);
    if (!s || s->native || s->orphaned || s->stopped || s->rx || !data || !n || n > RISC_USB_CONFIG_LIMIT || !ms) return -1;
    return host->bulk_read(host->context, s->data_claim, s->ep_in, data, n, ms);
}
static int32_t raw_write(uint64_t token, const uint8_t *data, size_t n, uint32_t ms) {
    cdc_session *s = lookup(token);
    if (!s || s->native || s->orphaned || s->stopped || s->tx || !data || !n || n > RISC_USB_CONFIG_LIMIT || !ms) return -1;
    return host->bulk_write(host->context, s->data_claim, s->ep_out, data, n, ms);
}
static bool raw_close(uint64_t token) {
    cdc_session *s = lookup(token); budget b = begin(1000);
    return s && !s->native && close_bounded(s, &b) == RISC_STREAM_OK;
}
static bool raw_endpoints(uint64_t token, uint32_t *rx, uint32_t *tx) {
    if (rx) *rx = 0;
    if (tx) *tx = 0;
    cdc_session *s = lookup(token); budget b = begin(1000);
    if (!s || s->native || s->orphaned || s->stopped || !rx || !tx) return false;
    if (!s->rx && !s->tx && publish(s, &b) != RISC_STREAM_OK) {
        (void)finish_queues(s, RISC_STREAM_IO, &b); return false;
    }
    if (!s->rx || !s->tx) return false;
    *rx = s->rx; *tx = s->tx; return true;
}
static bool bind_streams(const risc_stream_provider_v1 *api) {
    if (host || streams || !api || api->api_version != 1 || api->struct_size < sizeof(*api) ||
        !api->context || !api->publish || !api->produce || !api->consume || !api->finish || !api->close) return false;
    streams = api; return true;
}
static bool start(const risc_provider_dependency_v1 *deps, size_t count) {
    if (host || clock_api || !deps || count != 2) return false;
    const risc_usb_host_discovery_v1 *h = 0;
    const risc_platform_clock_api_v1 *c = 0;
    for (size_t i = 0; i < count; ++i) {
        if (!deps[i].capability_id || deps[i].api_version != 1 || !deps[i].api) return false;
        if (!strcmp(deps[i].capability_id, "usb.host") && !h) h = deps[i].api;
        else if (!strcmp(deps[i].capability_id, "platform.clock") && !c) c = deps[i].api;
        else return false;
    }
    if (!h || !c || h->host.api_version != 1 || h->host.struct_size < sizeof(*h) ||
        !h->host.configuration || !h->host.claim || !h->host.bulk_read || !h->host.bulk_write ||
        !h->devices || !h->release_checked || !h->control_claim ||
        c->api_version != 1 || c->struct_size < sizeof(*c) || !c->monotonic_ms) return false;
    if (c->monotonic_ms(c->context) == UINT64_MAX) return false;
    discovery = h; host = &h->host; clock_api = c; return true;
}
static bool quiesce(void) {
    for (size_t i = 0; i < RISC_USB_CDC_MAX_SESSIONS; ++i) {
        cdc_session *s = &sessions[i];
        if (s->orphaned) { budget b = begin(1000); if (close_bounded(s, &b) != RISC_STREAM_OK) return false; }
        if (s->token) return false;
    }
    return true;
}
static void stop(void) {
    /* Runtime-owned native custody is never consumed by lifecycle cleanup.
     * A retained session fences the invocation and graph instead of retrying. */
    for (size_t i = 0; i < RISC_USB_CDC_MAX_SESSIONS; ++i)
        if (sessions[i].token && sessions[i].native) return;
    for (size_t i = 0; i < RISC_USB_CDC_MAX_SESSIONS; ++i) if (sessions[i].token) {
        budget b = begin(1000);
        if (close_bounded(&sessions[i], &b) != RISC_STREAM_OK) return;
    }
    if (!quiesce()) return;
    host = 0; discovery = 0; clock_api = 0; streams = 0; next_session = 0;
}
static bool flush_rx(cdc_session *s, const budget *b) {
    if (s->rx_size == s->rx_offset) return true;
    if (!remaining(b)) return false;
    uint32_t n = 0, left = s->rx_size - s->rx_offset;
    int32_t rc = streams->produce(streams->context, s->rx, staging[s - sessions].rx + s->rx_offset, left, &n);
    if ((rc != RISC_STREAM_OK && rc != RISC_STREAM_AGAIN) || n > left) {
        (void)finish_queues(s, RISC_STREAM_IO, b); return false;
    }
    s->rx_offset += n;
    if (s->rx_offset == s->rx_size) s->rx_offset = s->rx_size = 0;
    return true;
}
static void poll_streams(uint32_t ms) {
    if (!host || !streams || !ms || ms > 1000) return;
    budget b = begin(ms);
    for (size_t visited = 0; visited < RISC_USB_CDC_MAX_SESSIONS; ++visited) {
        cdc_session *s = &sessions[next_session];
        next_session = (next_session + 1) % RISC_USB_CDC_MAX_SESSIONS;
        if (!s->token || !s->rx || !s->tx || s->stopped || s->paused) continue;
        if (!flush_rx(s, &b)) return;
        if (!s->rx_size) {
            uint32_t left = remaining(&b);
            if (!left) return;
            /* At most one RX and TX per turn. Each transfer may use at most
             * one millisecond, further capped by the remaining TOTAL budget. */
            int32_t n = host->bulk_read(host->context, s->data_claim, s->ep_in,
                staging[s - sessions].rx, sizeof(staging[0].rx), left < 1 ? left : 1);
            if (n < 0 || n > (int32_t)sizeof(staging[0].rx)) { (void)finish_queues(s, RISC_STREAM_IO, &b); return; }
            s->rx_size = (uint32_t)n;
            if (!flush_rx(s, &b)) return;
        }
        if (s->tx_size == s->tx_offset) {
            if (!remaining(&b)) return;
            uint32_t n = 0;
            int32_t rc = streams->consume(streams->context, s->tx, staging[s - sessions].tx, sizeof(staging[0].tx), &n);
            if ((rc != RISC_STREAM_OK && rc != RISC_STREAM_AGAIN && rc != RISC_STREAM_EOF) || n > sizeof(staging[0].tx)) {
                (void)finish_queues(s, RISC_STREAM_IO, &b); return;
            }
            s->tx_size = n; s->tx_offset = 0;
        }
        if (s->tx_size > s->tx_offset) {
            uint32_t left = remaining(&b);
            if (!left) return;
            uint32_t nleft = s->tx_size - s->tx_offset;
            int32_t n = host->bulk_write(host->context, s->data_claim, s->ep_out,
                staging[s - sessions].tx + s->tx_offset, nleft, left < 1 ? left : 1);
            if (n < 0 || (uint32_t)n > nleft) { (void)finish_queues(s, RISC_STREAM_IO, &b); return; }
            s->tx_offset += (uint32_t)n;
            if (s->tx_offset == s->tx_size) s->tx_offset = s->tx_size = 0;
        }
        return;
    }
}
static const risc_serial_port_streams_v1 capability = {
    {{{1, sizeof(capability), raw_open, raw_configure, raw_lines, raw_read, raw_write, raw_close}, probe}, snapshot}, raw_endpoints
};
static const risc_stream_session_provider_v1 adapter = {1, sizeof(adapter), open_session, call_session, close_session};
static const risc_driver_stream_sessions_v2 driver = {
    {{{2, sizeof(driver), "usb-cdc-acm-v2", "serial.port", 1, &capability, start, stop, quiesce}, 0, bind_streams}, poll_streams},
    RISC_DRIVER_STREAM_SESSIONS_TAG_V1, RISC_DRIVER_STREAM_SESSIONS_VERSION_V1, &adapter
};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) { return abi == 2 ? &driver.poll.streams.driver : 0; }

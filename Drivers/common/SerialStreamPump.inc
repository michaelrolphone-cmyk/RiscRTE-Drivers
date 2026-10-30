/* Private ELF-side implementation shared by USB serial classes. Include after
 * defining serial_session, sessions and host. The class supplies lookup and
 * bounded read_data/write_data; no transport callback enters firmware queues.
 * All calls are serialized by the provider owner task. Queue EOF signifies
 * acceptance, not physical wire flush. No allocation or unbounded retries. */
static const risc_stream_provider_v1 *streams;
static size_t next_session;
/* Keep bulk staging outside descriptor/open candidates copied on the stack.
 * Slot reuse resets all lengths; stale staging bytes are never exposed. */
static struct {
    uint8_t rx_pending[512], tx_pending[512];
} staging[RISC_USB_CDC_MAX_SESSIONS];
static serial_session *lookup(uint64_t);
static void fail_endpoints(serial_session *, int32_t);
static int32_t read_data(uint64_t, uint8_t *, size_t, uint32_t);
static int32_t write_data(uint64_t, const uint8_t *, size_t, uint32_t);
static bool bind_streams(const risc_stream_provider_v1 *api) {
    if (host || streams || !api || api->api_version != RISC_STREAM_PROVIDER_API_V1 ||
        api->struct_size < sizeof(*api) || !api->context || !api->publish ||
        !api->produce || !api->consume || !api->finish || !api->close) return false;
    streams = api;
    return true;
}
static void close_endpoints(serial_session *s) {
    s->failed = true; // Stop data work before any physical close can fail.
    if (streams && s->rx) (void)streams->close(streams->context, s->rx);
    if (streams && s->tx) (void)streams->close(streams->context, s->tx);
    s->rx = s->tx = 0;
    s->rx_size = s->rx_offset = s->tx_size = s->tx_offset = 0;
}

static bool endpoints(uint64_t token, uint32_t *rx, uint32_t *tx) {
    if (rx) *rx = 0;
    if (tx) *tx = 0;
    serial_session *s = lookup(token);
    if (!streams || !s || s->failed || !rx || !tx) return false;
    if (!s->rx && !s->tx) {
        const risc_stream_endpoint_v1 source = {sizeof(source), 1, 1, 2048, 0, 0, 0};
        const risc_stream_endpoint_v1 sink = {sizeof(sink), 1, 2, 2048, 0, 0, 0};
        if (streams->publish(streams->context, &source, &s->rx) != 0 ||
            streams->publish(streams->context, &sink, &s->tx) != 0) {
            close_endpoints(s);
            return false;
        }
    }
    if (!s->rx || !s->tx) return false;
    *rx = s->rx; *tx = s->tx;
    return true;
}
static void fail_endpoints(serial_session *s, int32_t error) {
    s->failed = true;
    (void)streams->finish(streams->context, s->rx, error);
    (void)streams->finish(streams->context, s->tx, error);
}
static bool flush_rx(serial_session *s) {
    if (s->rx_size == s->rx_offset) return true;
    uint32_t accepted = 0;
    const uint32_t remaining = s->rx_size - s->rx_offset;
    const int32_t result = streams->produce(streams->context, s->rx,
        staging[s - sessions].rx_pending + s->rx_offset, remaining, &accepted);
    if (result < 0 || result > 1 || accepted > remaining) {
        fail_endpoints(s, -5);
        return false;
    }
    s->rx_offset += accepted;
    if (s->rx_offset == s->rx_size) s->rx_offset = s->rx_size = 0;
    return true;
}
static void poll_streams(uint32_t budget_ms) {
    if (!streams || !host || budget_ms < 2) return;
    // One session per turn, <= one 512-byte read and write, each <=1 ms.
    // The generic dispatcher enforces item/time checkpoints and yields.
    for (size_t visited = 0; visited < RISC_USB_CDC_MAX_SESSIONS; ++visited) {
        serial_session *s = &sessions[next_session];
        next_session = (next_session + 1) % RISC_USB_CDC_MAX_SESSIONS;
        if (!s->token || !s->rx || !s->tx || s->failed) continue;
        if (!flush_rx(s)) return;
        if (!s->rx_size) {
            const int32_t n = read_data(s->token, staging[s - sessions].rx_pending, sizeof(staging[s - sessions].rx_pending), 1);
            if (n < 0 || (size_t)n > sizeof(staging[s - sessions].rx_pending)) { fail_endpoints(s, -5); return; }
            s->rx_size = (uint32_t)n;
            if (!flush_rx(s)) return;
        }
        if (s->tx_size == s->tx_offset) {
            uint32_t n = 0;
            const int32_t result = streams->consume(streams->context, s->tx,
                staging[s - sessions].tx_pending, sizeof(staging[s - sessions].tx_pending), &n);
            if (result < 0 || result > 2 || n > sizeof(staging[s - sessions].tx_pending)) {
                fail_endpoints(s, -5); return;
            }
            s->tx_size = n; s->tx_offset = 0;
        }
        if (s->tx_size > s->tx_offset) {
            const uint32_t remaining = s->tx_size - s->tx_offset;
            const int32_t n = write_data(s->token, staging[s - sessions].tx_pending + s->tx_offset, remaining, 1);
            if (n < 0 || (uint32_t)n > remaining) { fail_endpoints(s, -5); return; }
            s->tx_offset += (uint32_t)n;
            if (s->tx_offset == s->tx_size) s->tx_offset = s->tx_size = 0;
        }
        return;
    }
}
// Raw capability calls cannot bypass an activated endpoint pair's ordering.
static int32_t legacy_read(uint64_t token, uint8_t *data, size_t n, uint32_t ms) {
    serial_session *s = lookup(token);
    return s && !s->rx && !s->failed ? read_data(token, data, n, ms) : -1;
}
static int32_t legacy_write(uint64_t token, const uint8_t *data, size_t n, uint32_t ms) {
    serial_session *s = lookup(token);
    return s && !s->tx && !s->failed ? write_data(token, data, n, ms) : -1;
}

static void reconcile_endpoints(const uint64_t *tokens, size_t count) {
    // A successful inventory is authoritative for disconnect. Revoke copied
    // endpoints immediately; physical claim cleanup remains checked close.
    for (size_t i = 0; i < RISC_USB_CDC_MAX_SESSIONS; ++i) {
        serial_session *s = &sessions[i];
        if (!s->token || !s->rx || !s->tx) continue;
        bool present = false;
        for (size_t j = 0; j < count; ++j) present |= tokens[j] == s->device;
        if (!present) {
            fail_endpoints(s, -6);
            close_endpoints(s);
        }
    }
}

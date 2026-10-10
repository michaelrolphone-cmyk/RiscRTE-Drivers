/* Copied WebDAV session discovery over authenticated LE Secure Connections.
 * Apache NimBLE owns ATT/GATT/SMP; the pinned cooperative port owns HCI custody. */
#include "RiscBluetoothSessionSetupV1.h"
#include "RiscBluetoothHostV1.h"
#include "RiscPlatformClockV1.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_stop.h"
#include "host/ble_sm.h"
#include "host/ble_store.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "ble_port.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include <stdatomic.h>
#include <string.h>
static const portable_bluetooth_host_v1 *host;
static const risc_platform_clock_api_v1 *clock_api;
static uint64_t lease, token, serial, setup_deadline, webdav_deadline, pair_deadline;
static bool started, initialized, stopping, stopped, poisoned, advertising_pending, secure_pending;
static bool pair_wait, confirmed, encrypted, authenticated, terminate_pending;
static uint16_t connection = BLE_HS_CONN_HANDLE_NONE;
static uint32_t state, number, pairing_generation, connection_generation;
static int32_t last_error, stop_error, native_close_result;
static uint8_t descriptor[464];
static uint16_t descriptor_size;
static char device_name[21];
static struct ble_hs_stop_listener stop_listener;
static const struct ble_gatt_svc_def *gap_definition, *gatt_definition;
static atomic_flag guard = ATOMIC_FLAG_INIT;
static void erase(void *p, size_t n) { volatile uint8_t *v = p; while (n--) *v++ = 0; }
static uint64_t now(void) { return clock_api->monotonic_ms(clock_api->context); }
static void put16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
static void put32(uint8_t *p, uint32_t v) { for (unsigned i = 0; i < 4; i++) p[i] = v >> (i * 8); }
static void put64(uint8_t *p, uint64_t v) { for (unsigned i = 0; i < 8; i++) p[i] = v >> (i * 8); }
static void erase_descriptor(void) {
    erase(descriptor, sizeof(descriptor)); descriptor_size = 0;
    pair_wait = confirmed = encrypted = authenticated = false; number = 0;
}
static void end_session(uint32_t next_state, int32_t error) {
    erase_descriptor(); state = next_state; last_error = error;
    advertising_pending = secure_pending = false;
    if (connection != BLE_HS_CONN_HANDLE_NONE) terminate_pending = true;
}
static void expire(void) {
    if (token && !stopping && descriptor_size && now() >= setup_deadline)
        end_session(RISC_SETUP_ENDED, RISC_SETUP_EXPIRED);
}
static bool readable(void) {
    expire();
    return descriptor_size && state == RISC_SETUP_READY && confirmed && encrypted && authenticated &&
           connection != BLE_HS_CONN_HANDLE_NONE && !stopping && !hid_port_faulted();
}
static bool valid(uint64_t t) { return started && token && t == token; }
static void failed(int error) { end_session(RISC_SETUP_FAILED, error); }
static int access_value(uint16_t c, uint16_t a, struct ble_gatt_access_ctxt *x, void *arg) {
    (void)a;
    if (x->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    if (c != connection || !readable()) return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    /* Independent live-security check, beyond NimBLE's ATT permission flags. */
    struct ble_gap_conn_desc d;
    if (ble_gap_conn_find(c, &d) || !d.sec_state.encrypted || !d.sec_state.authenticated ||
        d.sec_state.key_size != 16 || d.sec_state.bonded) return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    if (arg) {
        uint8_t value[16];
        uint64_t t = now();
        if (t >= setup_deadline) { expire(); return BLE_ATT_ERR_INSUFFICIENT_AUTHEN; }
        put32(value, (uint32_t)(setup_deadline - t));
        put32(value + 4, connection_generation);
        put64(value + 8, webdav_deadline - t);
        return os_mbuf_append(x->om, value, sizeof(value)) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
    }
    return os_mbuf_append(x->om, descriptor, descriptor_size) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
}
#define UUID(n) BLE_UUID128_INIT(0x21,0x75,0x24,0x64,0x48,0x1b,0x2d,0xa7,0x86,0x4c,0x62,0x6b,n,0xf0,0x12,0xcc)
static const ble_uuid128_t service_uuid = UUID(1), descriptor_uuid = UUID(2), validity_uuid = UUID(3);
#define SEC_READ (BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_READ_AUTHEN)
static const struct ble_gatt_svc_def services[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &service_uuid.u,
     .characteristics = (struct ble_gatt_chr_def[]){
         {.uuid = &descriptor_uuid.u, .access_cb = access_value, .flags = SEC_READ, .min_key_size = 16},
         {.uuid = &validity_uuid.u, .access_cb = access_value, .flags = SEC_READ, .min_key_size = 16,
          .arg = (void *)1}, {0}}}, {0}};
static int gap_event(struct ble_gap_event *e, void *arg) {
    (void)arg;
    expire();
    uint16_t handle = connection;
    switch (e->type) {
    case BLE_GAP_EVENT_DISCONNECT: handle = e->disconnect.conn.conn_handle; break;
    case BLE_GAP_EVENT_ENC_CHANGE: handle = e->enc_change.conn_handle; break;
    case BLE_GAP_EVENT_PASSKEY_ACTION: handle = e->passkey.conn_handle; break;
    default: break;
    }
    if (handle != connection) return 0;
    if (e->type == BLE_GAP_EVENT_DISCONNECT) {
        connection = BLE_HS_CONN_HANDLE_NONE; terminate_pending = false;
        if (!stopping && state != RISC_SETUP_FAILED && state != RISC_SETUP_ENDED)
            end_session(RISC_SETUP_ENDED, RISC_SETUP_FAULT);
        else erase_descriptor();
        return 0;
    }
    if (stopping || state == RISC_SETUP_FAILED || state == RISC_SETUP_ENDED) {
        if (e->type == BLE_GAP_EVENT_CONNECT && !e->connect.status) {
            connection = e->connect.conn_handle; terminate_pending = true;
        }
        return 0;
    }
    switch (e->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (connection != BLE_HS_CONN_HANDLE_NONE) return 0;
        advertising_pending = false;
        if (e->connect.status) { advertising_pending = true; return 0; }
        connection = e->connect.conn_handle;
        if (connection_generation == UINT32_MAX) { failed(RISC_SETUP_CONTEXT); return 0; }
        connection_generation++;
        pair_wait = confirmed = encrypted = authenticated = false;
        state = RISC_SETUP_CONNECTED; secure_pending = true;
        return 0;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        if (connection == BLE_HS_CONN_HANDLE_NONE) advertising_pending = true;
        return 0;
    case BLE_GAP_EVENT_PASSKEY_ACTION:
        if (pair_wait || confirmed || pairing_generation == UINT32_MAX ||
            e->passkey.params.action != BLE_SM_IOACT_NUMCMP) {
            failed(BLE_HS_EAUTHEN); return 0;
        }
        pair_wait = true; number = e->passkey.params.numcmp; pairing_generation++;
        pair_deadline = now();
        pair_deadline = pair_deadline > UINT64_MAX - 30000 ? UINT64_MAX : pair_deadline + 30000;
        state = RISC_SETUP_PAIR_CONFIRM;
        return 0;
    case BLE_GAP_EVENT_ENC_CHANGE: {
        struct ble_gap_conn_desc d;
        pair_wait = false; number = 0;
        if (e->enc_change.status || !confirmed || ble_gap_conn_find(connection, &d) ||
            !d.sec_state.encrypted || !d.sec_state.authenticated || d.sec_state.key_size != 16 ||
            d.sec_state.bonded) { failed(BLE_HS_EAUTHEN); return 0; }
        encrypted = authenticated = true; state = RISC_SETUP_READY;
        return 0;
    }
    case BLE_GAP_EVENT_REPEAT_PAIRING: return BLE_GAP_REPEAT_PAIRING_IGNORE;
    default: return 0;
    }
}
static void reset_cb(int reason) { if (!stopping) failed(reason); }
static void sync_cb(void) {
    if (!stopping && state == RISC_SETUP_STARTING) advertising_pending = true;
}
static int empty_store(int kind, const union ble_store_key *key, union ble_store_value *value) {
    (void)kind; (void)key; (void)value; return BLE_HS_ENOENT;
}
static int reject_store(int kind, const union ble_store_value *value) {
    (void)kind; (void)value; failed(BLE_HS_ESTORE_FAIL); return BLE_HS_ENOTSUP;
}
static void remember_service(struct ble_gatt_register_ctxt *x, void *arg) {
    (void)arg;
    if (x->op != BLE_GATT_REGISTER_OP_SVC) return;
    const struct ble_gatt_svc_def *s = x->svc.svc_def;
    uint16_t uuid = ble_uuid_u16(s->uuid);
    if (uuid == 0x1800) gap_definition = s;
    if (uuid == 0x1801) gatt_definition = s;
}
static bool advertise(void) {
    struct ble_hs_adv_fields f = {0};
    f.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    f.uuids128 = &service_uuid; f.num_uuids128 = 1; f.uuids128_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&f);
    if (rc) { failed(rc); return false; }
    memset(&f, 0, sizeof(f)); f.name = (const uint8_t *)device_name;
    f.name_len = (uint8_t)strlen(device_name); f.name_is_complete = 1;
    if ((rc = ble_gap_adv_rsp_set_fields(&f))) { failed(rc); return false; }
    struct ble_gap_adv_params a = {0};
    a.conn_mode = BLE_GAP_CONN_MODE_UND; a.disc_mode = BLE_GAP_DISC_MODE_GEN;
    a.itvl_min = 48; a.itvl_max = 80;
    uint8_t address_type;
    if ((rc = ble_hs_id_infer_auto(0, &address_type)) ||
        (rc = ble_gap_adv_start(address_type, NULL, BLE_HS_FOREVER, &a, gap_event, NULL))) {
        failed(rc); return false;
    }
    state = RISC_SETUP_ADVERTISING; return true;
}
static int32_t pump(uint32_t count) {
    if (!lease) return RISC_SETUP_CONTEXT;
    expire();
    if (!count) count = 1;
    if (count > 16) count = 16;
    for (uint32_t i = 0; i < count; i++) {
        if (!hid_port_receive() && !stopping) break;
        hid_port_timers();
        struct ble_npl_event *e = ble_npl_eventq_get(nimble_port_get_dflt_eventq(), 0);
        if (e) ble_npl_event_run(e);
        if (hid_port_faulted() && !stopping) break;
    }
    if (hid_port_faulted() && !stopping) failed(BLE_HS_ECONTROLLER);
    expire();
    if (terminate_pending) {
        terminate_pending = false;
        int rc = ble_gap_terminate(connection, BLE_ERR_AUTH_FAIL);
        if (rc && rc != BLE_HS_EALREADY && rc != BLE_HS_ENOTCONN) poisoned = true;
    }
    if (stopping) return RISC_SETUP_PENDING;
    if (state == RISC_SETUP_FAILED) return RISC_SETUP_FAULT;
    if (state == RISC_SETUP_ENDED) {
        (void)ble_gap_adv_stop();
        return last_error == RISC_SETUP_EXPIRED ? RISC_SETUP_EXPIRED : RISC_SETUP_FAULT;
    }
    if (pair_wait && now() >= pair_deadline) {
        struct ble_sm_io io = {.action = BLE_SM_IOACT_NUMCMP, .numcmp_accept = 0};
        (void)ble_sm_inject_io(connection, &io);
        failed(BLE_HS_ETIMEOUT); return RISC_SETUP_FAULT;
    }
    if (secure_pending) {
        secure_pending = false;
        int rc = ble_gap_security_initiate(connection);
        if (rc && rc != BLE_HS_EALREADY) { failed(rc); return RISC_SETUP_FAULT; }
    }
    if (advertising_pending && connection == BLE_HS_CONN_HANDLE_NONE) {
        advertising_pending = false;
        if (!advertise()) return RISC_SETUP_FAULT;
    }
    return state == RISC_SETUP_STARTING ? RISC_SETUP_PENDING : RISC_SETUP_OK;
}
static size_t text_size(const char *s, size_t capacity) {
    for (size_t i = 0; i < capacity; i++) {
        if (!s[i]) return i;
        if ((unsigned char)s[i] < 32 || (unsigned char)s[i] > 126) return 0;
    }
    return 0;
}
static bool descriptor_valid(const risc_bluetooth_session_descriptor_v1 *d, uint16_t sizes[4]) {
    if (!d || d->struct_size < sizeof(*d) || !d->remaining_lifetime_ms) return false;
    sizes[0] = text_size(d->url, sizeof(d->url)); sizes[1] = text_size(d->username, sizeof(d->username));
    sizes[2] = text_size(d->password, sizeof(d->password)); sizes[3] = text_size(d->session_id, sizeof(d->session_id));
    for (unsigned i = 0; i < 4; i++) if (!sizes[i]) return false;
    size_t prefix = d->transport == RISC_SESSION_HTTP ? 7 : d->transport == RISC_SESSION_HTTPS ? 8 : 0;
    if (!prefix || sizes[0] <= prefix || memcmp(d->url, prefix == 7 ? "http://" : "https://", prefix)) return false;
    bool authority = true; size_t authority_size = 0;
    for (size_t i = prefix; i < sizes[0]; i++) {
        unsigned char v = d->url[i];
        if (v <= 32 || v == '#' || v == '\\') return false;
        if (authority && (v == '/' || v == '?')) authority = false;
        if (authority) { if (v == '@') return false; authority_size++; }
    }
    return authority_size != 0;
}
static int32_t open_impl(const char *name, const risc_bluetooth_session_descriptor_v1 *d,
                         uint32_t lifetime, uint64_t *out) {
    if (!out) return RISC_SETUP_INVALID;
    *out = 0;
    if (!started) return RISC_SETUP_CONTEXT;
    if (token) return RISC_SETUP_BUSY;
    if (poisoned || serial == UINT64_MAX) return RISC_SETUP_FAULT;
    uint16_t sizes[4];
    if (!name || !text_size(name, 21) || !lifetime || lifetime > RISC_SESSION_SETUP_MAX_LIFETIME_MS ||
        !descriptor_valid(d, sizes)) return RISC_SETUP_INVALID;
    uint64_t t = now();
    if (t > UINT64_MAX - lifetime || t > UINT64_MAX - d->remaining_lifetime_ms) return RISC_SETUP_INVALID;
    setup_deadline = t + lifetime;
    webdav_deadline = t + d->remaining_lifetime_ms;
    if (setup_deadline > webdav_deadline) setup_deadline = webdav_deadline;
    erase_descriptor();
    /* Immutable wire value: schema, transport, total bytes, original WebDAV remaining duration,
     * then four uint16 lengths and concatenated non-NUL strings. */
    descriptor[0] = 1; descriptor[1] = d->transport; put32(descriptor + 4, d->remaining_lifetime_ms);
    const char *parts[] = {d->url, d->username, d->password, d->session_id};
    descriptor_size = 16;
    for (unsigned i = 0; i < 4; i++) {
        put16(descriptor + 8 + i * 2, sizes[i]);
        memcpy(descriptor + descriptor_size, parts[i], sizes[i]); descriptor_size += sizes[i];
    }
    put16(descriptor + 2, descriptor_size);
    memcpy(device_name, name, strlen(name) + 1);
    token = ++serial; *out = token; state = RISC_SETUP_STARTING; last_error = stop_error = 0;
    native_close_result = -1; stopping = false; advertising_pending = secure_pending = terminate_pending = false;
    connection = BLE_HS_CONN_HANDLE_NONE;
    if (!host->claim(host->controls.context, &lease) || !lease) { failed(BLE_HS_EBUSY); return RISC_SETUP_FAULT; }
    hid_port_bind(host, clock_api, lease); stopped = false;
    if (!initialized) {
        nimble_port_init();
        ble_hs_cfg.reset_cb = reset_cb; ble_hs_cfg.sync_cb = sync_cb;
        ble_hs_cfg.gatts_register_cb = remember_service;
        ble_hs_cfg.store_read_cb = empty_store; ble_hs_cfg.store_write_cb = reject_store;
        ble_hs_cfg.sm_bonding = 0; ble_hs_cfg.sm_mitm = 1; ble_hs_cfg.sm_sc = 1;
        ble_hs_cfg.sm_our_key_dist = ble_hs_cfg.sm_their_key_dist = 0;
        ble_svc_gap_init(); ble_svc_gatt_init();
        int rc = ble_gatts_count_cfg(services);
        if (!rc) rc = ble_gatts_add_svcs(services);
        if (rc) { failed(rc); poisoned = true; return RISC_SETUP_FAULT; }
        initialized = true;
    } else {
        int rc = (!gap_definition || !gatt_definition) ? BLE_HS_EUNKNOWN : ble_gatts_reset();
        if (!rc) rc = ble_gatts_add_svcs(gap_definition);
        if (!rc) rc = ble_gatts_add_svcs(gatt_definition);
        if (!rc) rc = ble_gatts_add_svcs(services);
        if (!rc) rc = ble_hs_start();
        if (rc) { failed(rc); poisoned = true; return RISC_SETUP_FAULT; }
    }
    if (ble_svc_gap_device_name_set(device_name)) { failed(BLE_HS_EINVAL); return RISC_SETUP_FAULT; }
    return pump(2);
}
static void stopped_cb(int error, void *arg) {
    (void)arg; stopped = true; stop_error = error; if (error) poisoned = true;
}
static int32_t close_impl(uint64_t t) {
    if (!valid(t)) return RISC_SETUP_CONTEXT;
    erase_descriptor(); stopping = true; state = RISC_SETUP_CLOSING;
    advertising_pending = secure_pending = false;
    if (initialized && lease && !stopped) {
        int rc = ble_hs_stop(&stop_listener, stopped_cb, NULL);
        if (rc && rc != BLE_HS_EALREADY && rc != BLE_HS_EBUSY) stop_error = rc;
        if (rc == BLE_HS_EALREADY) stopped = true;
        uint64_t begin = now();
        for (unsigned steps = 0; !stopped && now() - begin < 300 && steps < 300; steps++) {
            (void)pump(8); clock_api->sleep_ms(clock_api->context, 1);
        }
    }
    if (initialized && (!stopped || connection != BLE_HS_CONN_HANDLE_NONE)) poisoned = true;
    if (lease) {
        native_close_result = host->release(host->controls.context, lease);
        if (native_close_result < 0) { last_error = RISC_SETUP_CLEANUP_PENDING; return RISC_SETUP_CLEANUP_PENDING; }
        lease = 0;
    }
    if (initialized && stopped && !stop_error && connection == BLE_HS_CONN_HANDLE_NONE) poisoned = false;
    connection = BLE_HS_CONN_HANDLE_NONE; token = 0; state = RISC_SETUP_OFF; stopping = false;
    terminate_pending = false; return RISC_SETUP_OK;
}
#define ENTER() do { if (atomic_flag_test_and_set(&guard)) return RISC_SETUP_BUSY; } while (0)
#define LEAVE(expr) do { int32_t r = (expr); atomic_flag_clear(&guard); return r; } while (0)
static int32_t api_open(void *c, const char *name, const risc_bluetooth_session_descriptor_v1 *d,
                        uint32_t lifetime, uint64_t *out) { (void)c; ENTER(); LEAVE(open_impl(name,d,lifetime,out)); }
static int32_t api_poll(void *c, uint64_t t, uint32_t count) {
    (void)c; ENTER(); if (!valid(t)) LEAVE(RISC_SETUP_CONTEXT);
    if (stopping) LEAVE(RISC_SETUP_CLEANUP_PENDING);
    LEAVE(pump(count));
}
static int32_t api_status(void *c, uint64_t t, risc_bluetooth_session_setup_status_v1 *out) {
    (void)c; ENTER();
    if (!started || (token ? t != token : t != 0)) LEAVE(RISC_SETUP_CONTEXT);
    if (stopping) LEAVE(RISC_SETUP_CLEANUP_PENDING);
    if (!out || out->struct_size < sizeof(*out)) LEAVE(RISC_SETUP_INVALID);
    expire(); uint64_t time = now();
    *out = (risc_bluetooth_session_setup_status_v1){sizeof(*out), state,
        (encrypted ? RISC_SETUP_ENCRYPTED : 0) | (authenticated ? RISC_SETUP_AUTHENTICATED : 0) |
        (confirmed ? RISC_SETUP_NUMERIC_CONFIRMED : 0) | (readable() ? RISC_SETUP_READABLE : 0),
        pair_wait ? number : 0, pairing_generation, connection_generation,
        descriptor_size && time < setup_deadline ? (uint32_t)(setup_deadline - time) : 0,
        last_error, native_close_result};
    LEAVE(RISC_SETUP_OK);
}
static int32_t api_confirm(void *c, uint64_t t, uint32_t generation, uint32_t shown, bool accept) {
    (void)c; ENTER();
    if (!valid(t)) LEAVE(RISC_SETUP_CONTEXT);
    if (stopping) LEAVE(RISC_SETUP_CLEANUP_PENDING);
    expire();
    if (!pair_wait || state != RISC_SETUP_PAIR_CONFIRM || generation != pairing_generation || shown != number)
        LEAVE(RISC_SETUP_INVALID);
    if (now() >= pair_deadline) { failed(BLE_HS_ETIMEOUT); LEAVE(RISC_SETUP_EXPIRED); }
    struct ble_sm_io io = {.action = BLE_SM_IOACT_NUMCMP, .numcmp_accept = accept};
    pair_wait = false; number = 0; state = RISC_SETUP_CONNECTED;
    int rc = ble_sm_inject_io(connection, &io);
    if (!accept) { end_session(RISC_SETUP_ENDED, RISC_SETUP_FAULT); LEAVE(RISC_SETUP_OK); }
    if (rc) { failed(rc); LEAVE(RISC_SETUP_FAULT); }
    confirmed = true; LEAVE(pump(8));
}
static int32_t api_close(void *c, uint64_t t) { (void)c; ENTER(); LEAVE(close_impl(t)); }
static bool quiesce(void) {
    if (atomic_flag_test_and_set(&guard)) return false;
    if (token && close_impl(token) != RISC_SETUP_OK) { atomic_flag_clear(&guard); return false; }
    erase_descriptor(); hid_port_bind(NULL, NULL, 0); host = NULL; clock_api = NULL; started = false;
    atomic_flag_clear(&guard); return true;
}
static void stop(void) { (void)quiesce(); }
static bool start(const risc_provider_dependency_v1 *d, size_t n) {
    if (atomic_flag_test_and_set(&guard)) return false;
    const portable_bluetooth_host_v1 *h = NULL; const risc_platform_clock_api_v1 *k = NULL;
    bool ok = !started && n == 2 && d;
    for (size_t i = 0; ok && i < n; i++) {
        if (!d[i].capability_id || d[i].api_version != 1 || !d[i].api) ok = false;
        else if (!strcmp(d[i].capability_id, "bluetooth.hci") && !h) h = d[i].api;
        else if (!strcmp(d[i].capability_id, "platform.clock") && !k) k = d[i].api;
        else ok = false;
    }
    ok = ok && h && h->controls.api_version == 1 && h->controls.struct_size >= sizeof(*h) &&
         h->claim && h->release && h->send_owned && h->next_owned && k && k->api_version == 1 &&
         k->struct_size >= sizeof(*k) && k->monotonic_ms && k->sleep_ms;
    if (ok) { host = h; clock_api = k; started = true; state = RISC_SETUP_OFF; last_error = 0; }
    atomic_flag_clear(&guard); return ok;
}
static const risc_bluetooth_session_setup_v1 api = {
    1, sizeof(api), NULL, api_open, api_poll, api_status, api_confirm, api_close};
static const risc_driver_v2 driver = {
    2, sizeof(driver), "ble-session-setup", RISC_BLUETOOTH_SESSION_SETUP_CAPABILITY, 1, &api, start, stop, quiesce};
__attribute__((visibility("default"))) const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == 2 ? &driver : NULL;
}

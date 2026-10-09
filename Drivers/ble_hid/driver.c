/* Generic, cooperative keyboard/mouse BLE peripheral. Apache NimBLE owns GAP,
 * GATT, ATT, L2CAP, SMP, Secure Connections and HCI flow control. This provider
 * owns only the HID services, copied reports, explicit lease and persistence. */
#include "RiscBluetoothHidV1.h"
#include "RiscBluetoothHostV1.h"
#include "RiscBoundKeyValueV1.h"
#include "RiscPlatformClockV1.h"
#include "hid_store.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_stop.h"
#include "host/ble_sm.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "port/ble_port.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include <stdatomic.h>
#include <string.h>
static const portable_bluetooth_host_v1 *host;
static const risc_platform_clock_api_v1 *clock_api;
static const risc_bound_key_value_v1 *storage;
static uint64_t lease, token, serial;
static bool started, initialized, stopping, stopped, poisoned, pair_allowed, pair_wait;
static bool encrypted, authenticated, suspended, advertise_pending, secure_pending,
    terminate_pending;
static uint16_t connection = BLE_HS_CONN_HANDLE_NONE, handles[5];
static bool subscribed[5];
static uint8_t keyboard_report[8], mouse_report[5], leds, protocol = 1, battery_level = 255;
static uint32_t state, number, pair_deadline, keyboard_since, mouse_since, generation;
static int32_t last_error;
static risc_bluetooth_hid_diagnostics_v1 diagnostics;
static uint32_t last_poll;
static bool polled;
static char device_name[21];
static struct ble_hs_stop_listener stop_listener;
static const struct ble_gatt_svc_def *gap_definition, *gatt_definition;
static atomic_flag guard = ATOMIC_FLAG_INIT;
static void failed(int rc);
/* Report IDs identify characteristics through Report Reference descriptors;
 * the ID is not repeated in the characteristic value or notification. */
static const uint8_t report_map[] = {
    0x05, 0x01, 0x09, 0x06, 0xa1, 0x01, 0x85, 0x01, 0x05, 0x07, 0x19, 0xe0, 0x29, 0xe7, 0x15,
    0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x95, 0x01, 0x75, 0x08, 0x81, 0x01,
    0x95, 0x05, 0x75, 0x01, 0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x91, 0x02, 0x95, 0x01, 0x75,
    0x03, 0x91, 0x01, 0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x26, 0xff, 0x00, 0x05, 0x07, 0x19,
    0x00, 0x29, 0xff, 0x81, 0x00, 0xc0, 0x05, 0x01, 0x09, 0x02, 0xa1, 0x01, 0x85, 0x02, 0x09,
    0x01, 0xa1, 0x00, 0x05, 0x09, 0x19, 0x01, 0x29, 0x05, 0x15, 0x00, 0x25, 0x01, 0x95, 0x05,
    0x75, 0x01, 0x81, 0x02, 0x95, 0x01, 0x75, 0x03, 0x81, 0x01, 0x05, 0x01, 0x09, 0x30, 0x09,
    0x31, 0x09, 0x38, 0x15, 0x81, 0x25, 0x7f, 0x75, 0x08, 0x95, 0x03, 0x81, 0x06,
    /* Consumer AC Pan: a separate signed relative horizontal wheel byte. */
    0x05, 0x0c, 0x0a, 0x38, 0x02, 0x95, 0x01, 0x81, 0x06, 0xc0, 0xc0};
enum {
    A_INFO,
    A_MAP,
    A_KEYBOARD,
    A_MOUSE,
    A_KEY_OUT,
    A_CONTROL,
    A_PROTOCOL,
    A_BOOT_KEY,
    A_BOOT_MOUSE,
    A_BOOT_OUT,
    A_BATTERY,
    A_PNP,
    A_MANUFACTURER
};
static int access_value(uint16_t, uint16_t, struct ble_gatt_access_ctxt *, void *);
static int access_reference(uint16_t, uint16_t, struct ble_gatt_access_ctxt *, void *);
#define REF(id, type)                                                                              \
    ((struct ble_gatt_dsc_def[]){{.uuid = BLE_UUID16_DECLARE(0x2908),                              \
                                  .att_flags = BLE_ATT_F_READ,                                     \
                                  .access_cb = access_reference,                                   \
                                  .arg = (void *)(uintptr_t)((id) | ((type) << 8))},               \
                                 {.uuid = NULL}})
#define SEC_READ (BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_READ_AUTHEN)
#define SEC_WRITE (BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC | BLE_GATT_CHR_F_WRITE_AUTHEN)
#define CHR(uuid_, kind_, flags_)                                                                  \
    .uuid = BLE_UUID16_DECLARE(uuid_), .access_cb = access_value,                                  \
    .arg = (void *)(uintptr_t)(kind_), .flags = (flags_), .min_key_size = 16
static const struct ble_gatt_svc_def services[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY,
     .uuid = BLE_UUID16_DECLARE(0x1812),
     .characteristics =
         (struct ble_gatt_chr_def[]){
             {CHR(0x2a4a, A_INFO, SEC_READ)},
             {CHR(0x2a4b, A_MAP, SEC_READ)},
             {CHR(0x2a4d, A_KEYBOARD, SEC_READ | BLE_GATT_CHR_F_NOTIFY), .val_handle = &handles[0],
              .descriptors = REF(1, 1)},
             {CHR(0x2a4d, A_MOUSE, SEC_READ | BLE_GATT_CHR_F_NOTIFY), .val_handle = &handles[1],
              .descriptors = REF(2, 1)},
             {CHR(0x2a4d, A_KEY_OUT, SEC_READ | SEC_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP),
              .val_handle = NULL, .descriptors = REF(1, 2)},
             {CHR(0x2a4c, A_CONTROL,
                  BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_WRITE_ENC |
                      BLE_GATT_CHR_F_WRITE_AUTHEN)},
             {CHR(0x2a4e, A_PROTOCOL,
                  SEC_READ | BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_WRITE_ENC |
                      BLE_GATT_CHR_F_WRITE_AUTHEN)},
             {CHR(0x2a22, A_BOOT_KEY, SEC_READ | BLE_GATT_CHR_F_NOTIFY), .val_handle = &handles[2]},
             {CHR(0x2a33, A_BOOT_MOUSE, SEC_READ | BLE_GATT_CHR_F_NOTIFY),
              .val_handle = &handles[3]},
             {CHR(0x2a32, A_BOOT_OUT, SEC_READ | SEC_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP)},
             {.uuid = NULL}}},
    {.type = BLE_GATT_SVC_TYPE_PRIMARY,
     .uuid = BLE_UUID16_DECLARE(0x180f),
     .characteristics =
         (struct ble_gatt_chr_def[]){
             {CHR(0x2a19, A_BATTERY, SEC_READ | BLE_GATT_CHR_F_NOTIFY), .val_handle = &handles[4]},
             {.uuid = NULL}}},
    {.type = BLE_GATT_SVC_TYPE_PRIMARY,
     .uuid = BLE_UUID16_DECLARE(0x180a),
     .characteristics =
         (struct ble_gatt_chr_def[]){{CHR(0x2a50, A_PNP, BLE_GATT_CHR_F_READ)},
                                     {CHR(0x2a29, A_MANUFACTURER, BLE_GATT_CHR_F_READ)},
                                     {.uuid = NULL}}},
    {.type = 0}};
static void neutral(void) {
    memset(keyboard_report, 0, sizeof(keyboard_report));
    memset(mouse_report, 0, sizeof(mouse_report));
    keyboard_since = mouse_since = 0;
}
static void disconnected(void) {
    neutral();
    memset(subscribed, 0, sizeof(subscribed));
    connection = BLE_HS_CONN_HANDLE_NONE;
    encrypted = authenticated = pair_wait = suspended = secure_pending = terminate_pending = false;
    number = pair_deadline = 0;
    protocol = 1;
    leds = 0;
}
static bool secure(void) {
    return encrypted && authenticated && !suspended && connection != BLE_HS_CONN_HANDLE_NONE &&
           !hid_store_failed() && !hid_port_faulted();
}
static uint32_t ready_flags(void) {
    if (!secure() || state != RISC_HID_READY || stopping)
        return 0;
    return (subscribed[protocol ? 0 : 2] ? RISC_HID_KEYBOARD_READY : 0) |
           (subscribed[protocol ? 1 : 3] ? RISC_HID_MOUSE_READY : 0);
}
static bool notify(unsigned which, const uint8_t *p, size_t n) {
    if (!secure() || !subscribed[which])
        return false;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(p, (uint16_t)n);
    int rc = om ? ble_gatts_notify_custom(connection, handles[which], om) : BLE_HS_ENOMEM;
    diagnostics.notify_error = rc;
    if (rc) diagnostics.notify_failures++;
    return rc == 0;
}
static bool release_impl(void) {
    if (connection == BLE_HS_CONN_HANDLE_NONE) {
        neutral();
        return true;
    }
    bool ok = true;
    uint8_t z[8] = {0};
    bool keys_held = false;
    for (unsigned i = 0; i < sizeof(keyboard_report); i++)
        keys_held |= keyboard_report[i] != 0;
    if (keys_held) {
        if (!notify(protocol ? 0 : 2, z, 8)) {
            ok = false;
        } else {
            memset(keyboard_report, 0, sizeof(keyboard_report));
            keyboard_since = 0;
        }
    }
    if (mouse_report[0]) {
        if (!notify(protocol ? 1 : 3, z, protocol ? sizeof(mouse_report) : 3)) {
            ok = false;
        } else {
            memset(mouse_report, 0, sizeof(mouse_report));
            mouse_since = 0;
        }
    }
    /* A rejected neutral must not erase the last accepted pressed report.
     * The caller can retry, or keep that custody until confirmed disconnect. */
    return ok;
}
static bool automatic_release(void) {
    if (release_impl())
        return true;
    failed(BLE_HS_EBUSY);
    return false;
}
static int access_reference(uint16_t c, uint16_t a, struct ble_gatt_access_ctxt *x, void *arg) {
    (void)c;
    (void)a;
    if (x->op != BLE_GATT_ACCESS_OP_READ_DSC)
        return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    uint8_t value[] = {(uint8_t)(uintptr_t)arg, (uint8_t)((uintptr_t)arg >> 8)};
    return os_mbuf_append(x->om, value, 2) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
}
static int access_value(uint16_t c, uint16_t a, struct ble_gatt_access_ctxt *x, void *arg) {
    (void)c;
    (void)a;
    unsigned kind = (unsigned)(uintptr_t)arg;
    if (x->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint8_t v;
        if (OS_MBUF_PKTLEN(x->om) != 1 || os_mbuf_copydata(x->om, 0, 1, &v))
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        if (kind == A_KEY_OUT || kind == A_BOOT_OUT) {
            if (v & 0xe0u)
                return BLE_ATT_ERR_UNLIKELY;
            leds = v;
            return 0;
        }
        if (kind == A_CONTROL) {
            if (v > 1)
                return BLE_ATT_ERR_UNLIKELY;
            if (!automatic_release())
                return BLE_ATT_ERR_INSUFFICIENT_RES;
            suspended = v == 0;
            return 0;
        }
        if (kind == A_PROTOCOL) {
            if (v > 1)
                return BLE_ATT_ERR_UNLIKELY;
            if (!automatic_release())
                return BLE_ATT_ERR_INSUFFICIENT_RES;
            protocol = v;
            neutral();
            return 0;
        }
        return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    }
    if (x->op != BLE_GATT_ACCESS_OP_READ_CHR)
        return BLE_ATT_ERR_REQ_NOT_SUPPORTED;
    const uint8_t *p = NULL;
    size_t n = 0;
    static const uint8_t info[] = {0x11, 0x01, 0,
                                   0x02}; /* HID 1.11, normally connectable; no remote wake. */
    static const uint8_t pnp[] = {
        1, 0, 0, 0, 0, 0, 1}; /* Unassigned development VID/PID, never another vendor's ID. */
    static const uint8_t maker[] = "RiscRTE";
    switch (kind) {
    case A_INFO:
        p = info;
        n = sizeof(info);
        break;
    case A_MAP:
        p = report_map;
        n = sizeof(report_map);
        break;
    case A_KEYBOARD:
    case A_BOOT_KEY:
        p = keyboard_report;
        n = 8;
        break;
    case A_MOUSE:
    case A_BOOT_MOUSE:
        p = mouse_report;
        n = kind == A_MOUSE ? sizeof(mouse_report) : 3;
        break;
    case A_KEY_OUT:
    case A_BOOT_OUT:
        p = &leds;
        n = 1;
        break;
    case A_PROTOCOL:
        p = &protocol;
        n = 1;
        break;
    case A_BATTERY:
        if (battery_level > 100)
            return BLE_ATT_ERR_UNLIKELY;
        p = &battery_level;
        n = 1;
        break;
    case A_PNP:
        p = pnp;
        n = sizeof(pnp);
        break;
    case A_MANUFACTURER:
        p = maker;
        n = sizeof(maker) - 1;
        break;
    default:
        return BLE_ATT_ERR_READ_NOT_PERMITTED;
    }
    return os_mbuf_append(x->om, p, (uint16_t)n) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
}
static void failed(int rc) {
    last_error = rc ? rc : BLE_HS_EUNKNOWN;
    state = RISC_HID_FAULT;
    if (connection == BLE_HS_CONN_HANDLE_NONE)
        neutral();
    else
        terminate_pending = true;
    advertise_pending = secure_pending = false;
}
static bool disconnect_fault(void) {
    /* Run only outside the host's access/GAP callbacks. Controller commands
     * have independent credits, so a full ACL queue cannot prevent this stop. */
    if (terminate_pending && connection != BLE_HS_CONN_HANDLE_NONE && !hid_port_faulted()) {
        terminate_pending = false;
        int rc = ble_gap_terminate(connection, BLE_ERR_REM_USER_CONN_TERM);
        if (rc && rc != BLE_HS_EALREADY && rc != BLE_HS_ENOTCONN)
            poisoned = true;
    }
    return false;
}
static int gap_event(struct ble_gap_event *e, void *arg) {
    (void)arg;
    /* A queued event belongs to its connection, never to a newer session. */
    uint16_t event_connection = connection;
    switch (e->type) {
    case BLE_GAP_EVENT_DISCONNECT: event_connection = e->disconnect.conn.conn_handle; break;
    case BLE_GAP_EVENT_ENC_CHANGE: event_connection = e->enc_change.conn_handle; break;
    case BLE_GAP_EVENT_PASSKEY_ACTION: event_connection = e->passkey.conn_handle; break;
    case BLE_GAP_EVENT_SUBSCRIBE: event_connection = e->subscribe.conn_handle; break;
    default: break;
    }
    if (event_connection != connection) { diagnostics.stale_events++; return 0; }
    if (e->type == BLE_GAP_EVENT_DISCONNECT)
        diagnostics.disconnect_reason = (uint32_t)e->disconnect.reason;
    if (e->type == BLE_GAP_EVENT_ENC_CHANGE)
        diagnostics.security_status = (uint32_t)e->enc_change.status;
    /* A later queued encryption refresh or connection event cannot revive a
     * fault in the same cooperative pump batch. Only safe teardown ends it. */
    if (state == RISC_HID_FAULT) {
        if (e->type == BLE_GAP_EVENT_DISCONNECT) {
            disconnected();
        } else if (e->type == BLE_GAP_EVENT_CONNECT && !e->connect.status) {
            connection = e->connect.conn_handle;
            generation++;
            terminate_pending = true;
        }
        return 0;
    }
    switch (e->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (connection != BLE_HS_CONN_HANDLE_NONE) { diagnostics.stale_events++; return 0; }
        advertise_pending = false;
        disconnected();
        if (e->connect.status) {
            if (!stopping)
                advertise_pending = true;
            return 0;
        }
        connection = e->connect.conn_handle;
        generation++;
        state = RISC_HID_CONNECTED;
        secure_pending = true;
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        disconnected();
        if (!stopping && state != RISC_HID_FAULT) {
            state = RISC_HID_ADVERTISING;
            advertise_pending = true;
        }
        return 0;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        if (!stopping && connection == BLE_HS_CONN_HANDLE_NONE && state != RISC_HID_FAULT)
            advertise_pending = true;
        return 0;
    case BLE_GAP_EVENT_ENC_CHANGE: {
        struct ble_gap_conn_desc d;
        pair_wait = false;
        number = 0;
        if (e->enc_change.status || ble_gap_conn_find(connection, &d) || !d.sec_state.encrypted ||
            !d.sec_state.authenticated || d.sec_state.key_size != 16) {
            encrypted = authenticated = false;
            (void)ble_gap_terminate(connection, BLE_ERR_AUTH_FAIL);
            return 0;
        }
        encrypted = d.sec_state.encrypted;
        authenticated = d.sec_state.authenticated;
        if (!automatic_release())
            return 0;
        state = RISC_HID_READY;
        pair_allowed = false;
        return 0;
    }
    case BLE_GAP_EVENT_PASSKEY_ACTION:
        if (!pair_allowed || hid_store_bonded() ||
            e->passkey.params.action != BLE_SM_IOACT_NUMCMP) {
            (void)ble_gap_terminate(connection, BLE_ERR_AUTH_FAIL);
            return 0;
        }
        if (!automatic_release())
            return 0;
        pair_wait = true;
        number = e->passkey.params.numcmp;
        pair_deadline = ble_npl_time_get() + 30000;
        state = RISC_HID_PAIR_CONFIRM;
        return 0;
    case BLE_GAP_EVENT_SUBSCRIBE:
        /* Termination callbacks precede the disconnect callback, after
         * NimBLE has already removed the connection's notification state. */
        if (e->subscribe.reason != BLE_GAP_SUBSCRIBE_REASON_TERM)
            (void)automatic_release();
        for (unsigned i = 0; i < 5; i++)
            if (e->subscribe.attr_handle == handles[i])
                subscribed[i] = e->subscribe.cur_notify;
        return 0;
    case BLE_GAP_EVENT_REPEAT_PAIRING:
        return BLE_GAP_REPEAT_PAIRING_IGNORE;
    default:
        return 0;
    }
}
static void reset_cb(int reason) {
    if (!stopping)
        failed(reason);
}
static void remember_service(struct ble_gatt_register_ctxt *x, void *arg) {
    (void)arg;
    if (x->op != BLE_GATT_REGISTER_OP_SVC)
        return;
    const struct ble_gatt_svc_def *s = x->svc.svc_def;
    uint16_t uuid = ble_uuid_u16(s->uuid);
    if (uuid == 0x1800)
        gap_definition = s;
    else if (uuid == 0x1801)
        gatt_definition = s;
}
static void sync_cb(void) {
    if (!stopping && state != RISC_HID_FAULT)
        advertise_pending = true;
}
static bool advertise(void) {
    struct ble_hs_adv_fields f = {0};
    ble_uuid16_t uuid = BLE_UUID16_INIT(0x1812);
    f.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    f.uuids16 = &uuid;
    f.num_uuids16 = 1;
    f.uuids16_is_complete = 1;
    f.appearance = 0x03c0;
    f.appearance_is_present = 1;
    int rc = ble_gap_adv_set_fields(&f);
    if (rc) {
        failed(rc);
        return false;
    }
    memset(&f, 0, sizeof(f));
    f.name = (const uint8_t *)device_name;
    f.name_len = (uint8_t)strlen(device_name);
    f.name_is_complete = 1;
    if ((rc = ble_gap_adv_rsp_set_fields(&f))) {
        failed(rc);
        return false;
    }
    struct ble_gap_adv_params a = {0};
    a.conn_mode = BLE_GAP_CONN_MODE_UND;
    a.disc_mode = BLE_GAP_DISC_MODE_GEN;
    a.itvl_min = 48;
    a.itvl_max = 80;
    uint8_t address_type;
    if ((rc = ble_hs_id_infer_auto(0, &address_type)) ||
        (rc = ble_gap_adv_start(address_type, NULL, BLE_HS_FOREVER, &a, gap_event, NULL))) {
        failed(rc);
        return false;
    }
    state = RISC_HID_ADVERTISING;
    return true;
}
static bool pump(uint32_t count) {
    if (!lease)
        return false;
    if (count > 16)
        count = 16;
    if (!count)
        count = 1;
    for (uint32_t i = 0; i < count; i++) {
        /* Once closing, the native transport may already be faulted. Drain
         * only the bounded host work/timers as needed to prove its stop; the
         * faulted port rejects all further I/O. */
        if (!hid_port_receive() && !stopping)
            break;
        hid_port_timers();
        struct ble_npl_event *e = ble_npl_eventq_get(nimble_port_get_dflt_eventq(), 0);
        if (e)
            ble_npl_event_run(e);
        if ((hid_port_faulted() || hid_store_failed()) && !stopping)
            break;
    }
    if (hid_port_faulted() || hid_store_failed()) {
        failed(hid_store_failed() ? BLE_HS_ESTORE_FAIL : BLE_HS_ECONTROLLER);
    }
    if (terminate_pending)
        (void)disconnect_fault();
    if (stopping)
        return true;
    if (state == RISC_HID_FAULT)
        return false;
    if (pair_wait && (int32_t)(ble_npl_time_get() - pair_deadline) >= 0) {
        struct ble_sm_io io = {.action = BLE_SM_IOACT_NUMCMP, .numcmp_accept = 0};
        (void)ble_sm_inject_io(connection, &io);
        pair_wait = false;
        pair_allowed = false;
        number = 0;
        state = RISC_HID_CONNECTED;
        (void)ble_gap_terminate(connection, BLE_ERR_AUTH_FAIL);
    }
    if (secure_pending) {
        secure_pending = false;
        struct ble_gap_conn_desc d;
        if (ble_gap_conn_find(connection, &d) ||
            (!pair_allowed && !hid_store_peer(&d.peer_id_addr))) {
            (void)ble_gap_terminate(connection, BLE_ERR_AUTH_FAIL);
        } else {
            int rc = ble_gap_security_initiate(connection);
            if (rc && rc != BLE_HS_EALREADY) {
                failed(rc);
                return disconnect_fault();
            }
        }
    }
    if (advertise_pending && connection == BLE_HS_CONN_HANDLE_NONE && state != RISC_HID_FAULT) {
        advertise_pending = false;
        if (!advertise())
            return disconnect_fault();
    }
    /* One device's traffic cannot keep another device's modifiers/buttons
     * latched. Timestamp zero and uint32 wrap are ordinary clock values. */
    uint32_t now = ble_npl_time_get();
    bool keys_held = false;
    for (unsigned i = 0; i < sizeof(keyboard_report); i++)
        keys_held |= keyboard_report[i] != 0;
    uint8_t zero[8] = {0};
    if (keys_held && (uint32_t)(now - keyboard_since) >= 1000) {
        bool ok = notify(protocol ? 0 : 2, zero, 8);
        if (!ok) {
            failed(BLE_HS_EBUSY);
            return disconnect_fault();
        }
        memset(keyboard_report, 0, sizeof(keyboard_report));
    }
    if (mouse_report[0] && (uint32_t)(now - mouse_since) >= 1000) {
        bool ok = notify(protocol ? 1 : 3, zero, protocol ? sizeof(mouse_report) : 3);
        if (!ok) {
            failed(BLE_HS_EBUSY);
            return disconnect_fault();
        }
        memset(mouse_report, 0, sizeof(mouse_report));
    }
    return state != RISC_HID_FAULT;
}
static bool valid(uint64_t t) { return started && token && t == token; }
static bool open_impl(const char *name, bool pairing, uint64_t *out) {
    if (!out)
        return false;
    *out = 0;
    if (!started || token || poisoned || serial == UINT64_MAX || !name)
        return false;
    size_t n = 0;
    while (n < 21 && name[n]) {
        if ((unsigned char)name[n] < 32 || (unsigned char)name[n] > 126)
            return false;
        n++;
    }
    if (!n || n > 20)
        return false;
    if (!hid_store_init(storage)) {
        failed(BLE_HS_ESTORE_FAIL);
        return false;
    }
    if (!pairing && !hid_store_bonded()) {
        last_error = BLE_HS_ENOENT;
        return false;
    }
    memcpy(device_name, name, n + 1);
    token = ++serial;
    *out = token;
    state = RISC_HID_STARTING;
    last_error = 0;
    pair_allowed = pairing && !hid_store_bonded();
    stopping = false;
    diagnostics = (risc_bluetooth_hid_diagnostics_v1){.struct_size = sizeof(diagnostics)};
    polled = false;
    advertise_pending = secure_pending = false;
    disconnected();
    if (!host->claim(host->controls.context, &lease) || !lease) {
        failed(BLE_HS_EBUSY);
        return false;
    }
    hid_port_bind(host, clock_api, lease);
    /* A refused claim does not invalidate a previous proven host stop. */
    stopped = false;
    if (!initialized) {
        nimble_port_init();
        ble_hs_cfg.reset_cb = reset_cb;
        ble_hs_cfg.sync_cb = sync_cb;
        ble_hs_cfg.gatts_register_cb = remember_service;
        ble_hs_cfg.store_read_cb = hid_store_read;
        ble_hs_cfg.store_write_cb = hid_store_write;
        ble_hs_cfg.store_delete_cb = hid_store_delete;
        ble_hs_cfg.store_gen_key_cb = hid_store_key;
        ble_svc_gap_init();
        ble_svc_gatt_init();
        int rc = ble_gatts_count_cfg(services);
        if (!rc)
            rc = ble_gatts_add_svcs(services);
        if (rc) {
            failed(rc);
            poisoned = true;
            return false;
        }
        initialized = true;
    } else {
        /* NimBLE frees its temporary registration list after first start.
         * Reset the stopped ATT database and register the same immutable
         * definitions again; do not count/inflate resource maxima again. */
        int rc = (!gap_definition || !gatt_definition) ? BLE_HS_EUNKNOWN : ble_gatts_reset();
        if (!rc)
            rc = ble_gatts_add_svcs(gap_definition);
        if (!rc)
            rc = ble_gatts_add_svcs(gatt_definition);
        if (!rc)
            rc = ble_gatts_add_svcs(services);
        if (!rc)
            rc = ble_hs_start();
        if (rc) {
            failed(rc);
            poisoned = true;
            return false;
        }
    }
    if (ble_svc_gap_device_name_set(device_name)) {
        failed(BLE_HS_EINVAL);
        return false;
    }
    /* Complete the initial host start stages while this explicit open owns
     * the lease. An immediate close before the caller's first poll is safe. */
    return pump(2);
}
static void stopped_cb(int status, void *arg) {
    (void)arg;
    stopped = true;
    diagnostics.host_stop_error = status;
    if (status)
        poisoned = true;
}
static bool close_impl(uint64_t t) {
    if (!valid(t))
        return false;
    stopping = true;
    advertise_pending = secure_pending = false;
    /* Host teardown remains necessary after a transport failure. NimBLE can
     * clear its software state even when controller commands fail; successful
     * native release below is the independent proof that callbacks are gone. */
    if (initialized && lease && !stopped) {
        (void)release_impl();
        int rc = ble_hs_stop(&stop_listener, stopped_cb, NULL);
        if (rc && rc != BLE_HS_EALREADY && rc != BLE_HS_EBUSY) diagnostics.host_stop_error = rc;
        if (rc == BLE_HS_EALREADY)
            stopped = true;
        uint32_t begin = ble_npl_time_get();
        while (!stopped && (uint32_t)(ble_npl_time_get() - begin) < 300) {
            (void)pump(8);
            clock_api->sleep_ms(clock_api->context, 1);
        }
    }
    if (initialized && (!stopped || connection != BLE_HS_CONN_HANDLE_NONE))
        poisoned = true;
    /* Native close is the final proof that radio callbacks and packets cannot
     * reference this provider. Failure retains every token and dependency. */
    if (lease) {
        int32_t rc = host->release(host->controls.context, lease);
        diagnostics.native_close_result = rc;
        if (rc < 0) {
            failed(BLE_HS_ECONTROLLER);
            return false;
        }
        if (rc == 0)
            last_error = BLE_HS_ECONTROLLER;
        lease = 0;
    }
    disconnected();
    token = 0;
    state = RISC_HID_OFF;
    stopping = false;
    return true;
}
static bool status_impl(uint64_t t, risc_bluetooth_hid_status_v1 *out) {
    if (!started || !out || out->struct_size < sizeof(*out) || (token ? t != token : t != 0))
        return false;
    *out = (risc_bluetooth_hid_status_v1){sizeof(*out),
                                          state,
                                          ready_flags() | (encrypted ? RISC_HID_ENCRYPTED : 0) |
                                              (authenticated ? RISC_HID_AUTHENTICATED : 0) |
                                              (hid_store_bonded() ? RISC_HID_BONDED : 0),
                                          pair_wait ? number : 0,
                                          last_error,
                                          generation};
    return true;
}
static bool keyboard_impl(uint64_t t, uint8_t mods, const uint8_t *keys) {
    if (!valid(t) || !keys || !(ready_flags() & RISC_HID_KEYBOARD_READY))
        return false;
    uint8_t b[8] = {mods, 0};
    for (unsigned i = 0; i < 6; i++) {
        if (keys[i] >= 0xe0 || keys[i] == 1 || keys[i] == 2 || keys[i] == 3)
            return false;
        for (unsigned j = 0; j < i; j++)
            if (keys[i] && keys[i] == keys[j])
                return false;
        b[i + 2] = keys[i];
    }
    if (!notify(protocol ? 0 : 2, b, sizeof(b)))
        return false;
    memcpy(keyboard_report, b, sizeof(b));
    keyboard_since = ble_npl_time_get();
    return true;
}
static int8_t mouse_delta(int16_t value) {
    return value < -127 ? -127 : value > 127 ? 127 : (int8_t)value;
}
static bool mouse_scroll_impl(uint64_t t, uint8_t buttons, int16_t dx, int16_t dy,
                              int16_t wheel_x, int16_t wheel_y) {
    if (!valid(t) || buttons > 31 ||
        (!protocol && (buttons > 7 || wheel_x != 0 || wheel_y != 0)) ||
        !(ready_flags() & RISC_HID_MOUSE_READY))
        return false;
    /* Keep vertical Wheel in the existing report prefix; AC Pan is the tail. */
    uint8_t b[] = {buttons, (uint8_t)mouse_delta(dx), (uint8_t)mouse_delta(dy),
                   (uint8_t)mouse_delta(wheel_y), (uint8_t)mouse_delta(wheel_x)};
    if (!notify(protocol ? 1 : 3, b, protocol ? sizeof(b) : 3))
        return false;
    memset(mouse_report, 0, sizeof(mouse_report));
    mouse_report[0] = buttons;
    mouse_since = ble_npl_time_get();
    return true;
}
static bool mouse_impl(uint64_t t, uint8_t buttons, int8_t dx, int8_t dy, int8_t wheel) {
    /* Preserve the original API's rejection of the unrepresentable -128. */
    if (dx == -128 || dy == -128 || wheel == -128)
        return false;
    return mouse_scroll_impl(t, buttons, dx, dy, 0, wheel);
}
/* Serialize the public API. No busy caller can enter the cooperative host. */
#define ENTER()                                                                                    \
    do {                                                                                           \
        if (atomic_flag_test_and_set(&guard))                                                      \
            return false;                                                                          \
    } while (0)
#define LEAVE(value)                                                                               \
    do {                                                                                           \
        bool result = (value);                                                                     \
        atomic_flag_clear(&guard);                                                                 \
        return result;                                                                             \
    } while (0)
static bool api_open(void *c, const char *n, bool p, uint64_t *t) {
    (void)c;
    ENTER();
    LEAVE(open_impl(n, p, t));
}
static bool api_poll(void *c, uint64_t t, uint32_t n) {
    (void)c;
    ENTER();
    uint32_t now = ble_npl_time_get();
    if (valid(t)) {
        uint32_t gap = now - last_poll;
        if (polled && gap > diagnostics.max_poll_gap_ms) diagnostics.max_poll_gap_ms = gap;
        last_poll = now; polled = true;
    }
    LEAVE(valid(t) && pump(n));
}
static bool api_status(void *c, uint64_t t, risc_bluetooth_hid_status_v1 *s) {
    (void)c;
    ENTER();
    LEAVE(status_impl(t, s));
}
static bool api_confirm(void *c, uint64_t t, bool accept) {
    (void)c;
    ENTER();
    if (!valid(t) || !pair_wait || state != RISC_HID_PAIR_CONFIRM) {
        atomic_flag_clear(&guard);
        return false;
    }
    bool expired = (int32_t)(ble_npl_time_get() - pair_deadline) >= 0;
    struct ble_sm_io io = {.action = BLE_SM_IOACT_NUMCMP, .numcmp_accept = accept && !expired};
    pair_wait = false;
    number = 0;
    state = RISC_HID_CONNECTED;
    int rc = ble_sm_inject_io(connection, &io);
    if (!accept || expired) {
        pair_allowed = false;
        (void)ble_gap_terminate(connection, BLE_ERR_AUTH_FAIL);
    } else if (rc == 0) {
        /* Numeric comparison is the only application-driven SMP boundary.
         * Advance the cooperative host immediately after accepting it so the
         * DHKey-check transaction is emitted/consumed while the confirmation
         * call still owns the HCI lease. Real centrals may wait at this exact
         * boundary and do not guarantee another packet before our next UI poll. */
        if (!pump(8)) {
            atomic_flag_clear(&guard);
            return false;
        }
    }
    LEAVE(!expired && (rc == 0 || (!accept && rc == BLE_HS_SM_US_ERR(BLE_SM_ERR_NUMCMP))));
}
static bool api_keyboard(void *c, uint64_t t, uint8_t m, const uint8_t *k) {
    (void)c;
    ENTER();
    LEAVE(keyboard_impl(t, m, k));
}
static bool api_mouse(void *c, uint64_t t, uint8_t b, int8_t x, int8_t y, int8_t w) {
    (void)c;
    ENTER();
    LEAVE(mouse_impl(t, b, x, y, w));
}
static bool api_mouse_scroll(void *c, uint64_t t, uint8_t b, int16_t x, int16_t y,
                              int16_t wheel_x, int16_t wheel_y) {
    (void)c;
    ENTER();
    LEAVE(mouse_scroll_impl(t, b, x, y, wheel_x, wheel_y));
}
static bool api_release(void *c, uint64_t t) {
    (void)c;
    ENTER();
    LEAVE(valid(t) && release_impl());
}
static bool api_close(void *c, uint64_t t) {
    (void)c;
    ENTER();
    LEAVE(close_impl(t));
}
static bool api_diagnostics(void *c, uint64_t t, risc_bluetooth_hid_diagnostics_v1 *out) {
    (void)c;
    ENTER();
    if (!started || !out || out->struct_size < sizeof(*out) || (token ? t != token : t != 0)) {
        atomic_flag_clear(&guard); return false;
    }
    *out = diagnostics;
    out->struct_size = sizeof(*out);
    out->port_fault = hid_port_fault_reason();
    out->poisoned = poisoned;
    out->host_stopped = stopped;
    LEAVE(true);
}
static bool api_forget(void *c) {
    (void)c;
    ENTER();
    LEAVE(started && !token && hid_store_forget());
}
static bool api_battery(void *c, uint64_t t, uint8_t p) {
    (void)c;
    ENTER();
    if (!valid(t) || (p > 100 && p != 255)) {
        atomic_flag_clear(&guard);
        return false;
    }
    battery_level = p;
    if (p <= 100 && secure() && subscribed[4]) {
        LEAVE(notify(4, &p, 1));
    }
    LEAVE(true);
}
static bool quiesce(void) {
    ENTER();
    if (token && !close_impl(token)) {
        atomic_flag_clear(&guard);
        return false;
    }
    hid_store_clear();
    hid_port_bind(NULL, NULL, 0);
    started = false;
    host = NULL;
    clock_api = NULL;
    storage = NULL;
    LEAVE(true);
}
static void stop(void) { (void)quiesce(); }
static bool start(const risc_provider_dependency_v1 *d, size_t n) {
    ENTER();
    if (started || n != 3 || !d) {
        atomic_flag_clear(&guard);
        return false;
    }
    const portable_bluetooth_host_v1 *h = NULL;
    const risc_platform_clock_api_v1 *k = NULL;
    const risc_bound_key_value_v1 *s = NULL;
    for (size_t i = 0; i < n; i++) {
        if (!d[i].capability_id || d[i].api_version != 1 || !d[i].api) {
            atomic_flag_clear(&guard);
            return false;
        }
        if (!strcmp(d[i].capability_id, "bluetooth.hci") && !h)
            h = d[i].api;
        else if (!strcmp(d[i].capability_id, "platform.clock") && !k)
            k = d[i].api;
        else if (!strcmp(d[i].capability_id, RISC_BOUND_KEY_VALUE_CAPABILITY) && !s)
            s = d[i].api;
        else {
            atomic_flag_clear(&guard);
            return false;
        }
    }
    if (!h || h->controls.api_version != 1 || h->controls.struct_size < sizeof(*h) || !h->claim ||
        !h->release || !h->send_owned || !h->next_owned || !k || k->api_version != 1 ||
        k->struct_size < sizeof(*k) || !k->monotonic_ms || !k->sleep_ms || !s ||
        s->api_version != 1 || s->struct_size < sizeof(*s) || !s->get || !s->put) {
        atomic_flag_clear(&guard);
        return false;
    }
    host = h;
    clock_api = k;
    storage = s;
    started = true;
    state = RISC_HID_OFF;
    last_error = 0;
    /* Read-only admission, no controller activation, key generation or writes. */
    if (!hid_store_init(storage))
        last_error = BLE_HS_ESTORE_FAIL;
    LEAVE(true);
}
static const risc_bluetooth_hid_scroll_api_v1 api = {{{1,          sizeof(api), NULL,        api_open,
                                          api_poll,   api_status,  api_confirm, api_keyboard,
                                          api_mouse,  api_release, api_close,   api_forget,
                                          api_battery}, api_diagnostics}, api_mouse_scroll};
static const risc_driver_v2 driver = {
    2, sizeof(driver), "ble-hid", RISC_BLUETOOTH_HID_CAPABILITY, 1, &api, start, stop, quiesce};
__attribute__((visibility("default"))) const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == 2 ? &driver : NULL;
}

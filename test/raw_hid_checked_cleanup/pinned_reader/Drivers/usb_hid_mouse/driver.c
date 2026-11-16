#include "RiscUsbMouseV1.h"
#include "RiscPlatformClockV1.h"
#include "Descriptor.h"

/* All entry points execute on the serialized provider executor. There are no
 * retained consumer buffers, callbacks, native USB imports, or allocations. */
#define DISCOVERY_ATTEMPTS 3u
#define DISCOVERY_DEADLINE_MS 1000u
#define DISCOVERY_BACKOFF_MS 100u
#define CLOSE_ATTEMPTS 3u
#define REPORT_WORK_MS 20u

typedef struct {
    uint64_t raw_session;
    risc_usb_mouse_state_v1 state;
    mouse_layout layout;
    uint8_t close_attempts;
    bool closing;
} mouse;
typedef struct {
    uint64_t token, filter;
    risc_usb_mouse_event_v1 events[RISC_USB_INPUT_QUEUE_LENGTH];
    uint8_t head, count;
    bool gap;
} subscriber;
typedef struct {
    uint64_t device, began_ms, attempted_ms;
    uint8_t iface, alt, attempts;
    bool done;
} inspection;
static const risc_usb_hid_api_v1 *hid;
static const risc_platform_clock_api_v1 *clock_api;
static mouse mice[RISC_USB_MOUSE_MAX_DEVICES];
static subscriber subscribers[RISC_USB_INPUT_MAX_SUBSCRIBERS];
static inspection inspected[RISC_USB_HID_MAX_INTERFACES];
static uint64_t serial, session_serial, sequence;
static uint32_t phase = RISC_USB_MOUSE_STOPPED;
static unsigned read_cursor;
static unsigned retained_close = RISC_USB_MOUSE_MAX_DEVICES;
static bool stopping;

static bool equal(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static subscriber *subscription(uint64_t token) {
    if (!token) return 0;
    for (size_t i = 0; i < RISC_USB_INPUT_MAX_SUBSCRIBERS; ++i)
        if (subscribers[i].token == token) return &subscribers[i];
    return 0;
}
static void gaps(uint64_t device) {
    for (size_t i = 0; i < RISC_USB_INPUT_MAX_SUBSCRIBERS; ++i) {
        subscriber *s = &subscribers[i];
        if (s->token && (!device || !s->filter || s->filter == device)) {
            s->head = s->count = 0; s->gap = true;
        }
    }
}
static void emit(risc_usb_mouse_event_v1 *event) {
    if (sequence == UINT64_MAX) { gaps(0); return; }
    event->sequence = ++sequence;
    for (size_t i = 0; i < RISC_USB_INPUT_MAX_SUBSCRIBERS; ++i) {
        subscriber *s = &subscribers[i];
        if (!s->token || s->gap || (s->filter && s->filter != event->state.device)) continue;
        if (s->count == RISC_USB_INPUT_QUEUE_LENGTH) {
            s->head = s->count = 0; s->gap = true; continue;
        }
        s->events[(s->head + s->count) % RISC_USB_INPUT_QUEUE_LENGTH] = *event;
        ++s->count;
    }
}
static void disconnect_mouse(mouse *m, bool lost) {
    if (!m->raw_session) return;
    if (lost) gaps(m->state.device);
    if (m->state.connected) {
        risc_usb_mouse_event_v1 event = {0};
        event.kind = RISC_USB_MOUSE_DISCONNECTED;
        event.released = m->state.buttons;
        m->state.buttons = 0; m->state.connected = 0;
        event.state = m->state;
        emit(&event);
    }
    m->closing = true;
}
static void close_all(bool lost) {
    for (size_t i = 0; i < RISC_USB_MOUSE_MAX_DEVICES; ++i)
        disconnect_mouse(&mice[i], lost);
    phase = RISC_USB_MOUSE_CLOSING;
}
static bool have_closing(void) {
    for (size_t i = 0; i < RISC_USB_MOUSE_MAX_DEVICES; ++i)
        if (mice[i].raw_session && mice[i].closing) return true;
    return false;
}
/* False close keeps the exact handle. Retrying never re-enters scan/present/
 * read/descriptor or admission. Three false closes permanently pin this ELF
 * and its dependencies; every subsequent public entry point is local only. */
static bool close_step(void) {
    if (phase == RISC_USB_MOUSE_RETAINED) return false;
    for (size_t i = 0; i < RISC_USB_MOUSE_MAX_DEVICES; ++i) {
        const size_t index = retained_close < RISC_USB_MOUSE_MAX_DEVICES ? retained_close : i;
        mouse *m = &mice[index];
        if (!m->raw_session || !m->closing) continue;
        ++m->close_attempts;
        if (!hid->close(hid->context, m->raw_session)) {
            retained_close = (unsigned)index;
            close_all(true);
            if (m->close_attempts >= CLOSE_ATTEMPTS) phase = RISC_USB_MOUSE_RETAINED;
            return false;
        }
        *m = (mouse){0};
        retained_close = RISC_USB_MOUSE_MAX_DEVICES;
        break; /* one lower close per invocation */
    }
    if (have_closing()) return false;
    phase = RISC_USB_MOUSE_RUNNING;
    return true;
}
static bool same_interface(const risc_usb_hid_interface_v1 *item,
                           uint64_t device, uint8_t iface, uint8_t alt) {
    return item->device == device && item->interface_number == iface && item->alternate == alt;
}
static bool listed(const risc_usb_hid_interface_v1 *items, size_t count,
                   uint64_t device, uint8_t iface, uint8_t alt) {
    for (size_t i = 0; i < count; ++i)
        if (same_interface(&items[i], device, iface, alt)) return true;
    return false;
}
static bool apply_report(mouse *m, const uint8_t *report, size_t n) {
    risc_usb_mouse_event_v1 event = {0};
    event.kind = RISC_USB_MOUSE_REPORT;
    event.state = m->state;
    event.state.buttons = 0;
    if (m->layout.boot) {
        /* SET_PROTOCOL(BOOT) fixes these three bytes. No trailing bytes are
         * interpreted as wheel/pan. Ordinary boot mode provides 3 buttons. */
        if (n != 3 || (report[0] & 0xf8u)) return false;
        event.state.buttons = report[0];
        event.x = signed_item(report[1], 1); event.y = signed_item(report[2], 1);
    } else {
        bool known = false;
        uint8_t id = m->layout.report_id ? (n ? report[0] : 0) : 0;
        for (size_t i = 0; i < m->layout.report_count; ++i) {
            const mouse_report *r = &m->layout.reports[i];
            if (r->id != id) continue;
            if (n != r->bytes) return false;
            known = true; break;
        }
        if (!known) return false;
        if (id != m->layout.report_id) return true; /* verified unrelated input layout */
        if (id) ++report;
        for (size_t i = 0; i < m->layout.field_count; ++i) {
            const mouse_field *f = &m->layout.fields[i];
            const int32_t v = extract_field(report, f);
            if (v < f->minimum || v > f->maximum) return false;
            switch (f->kind) {
                case 1: event.x = v; break;
                case 2: event.y = v; break;
                case 3: event.wheel = v; break;
                case 4: event.pan = v; break;
                case 5: if (v) event.state.buttons |= 1u << f->index; break;
                default: return false;
            }
        }
    }
    event.pressed = event.state.buttons & ~m->state.buttons;
    event.released = m->state.buttons & ~event.state.buttons;
    m->state = event.state;
    if (event.pressed || event.released || event.x || event.y || event.wheel || event.pan)
        emit(&event);
    return true;
}
static bool discover(const risc_usb_hid_interface_v1 *items, size_t count, uint64_t now) {
    for (size_t i = 0; i < count; ++i) {
        const risc_usb_hid_interface_v1 *item = &items[i];
        if (!item->device || (item->subclass == 1 && item->protocol == 1)) continue;
        bool known = false;
        mouse *slot = 0;
        for (size_t j = 0; j < RISC_USB_MOUSE_MAX_DEVICES; ++j) {
            mouse *m = &mice[j];
            if (!m->raw_session && !slot) slot = m;
            if (m->raw_session && same_interface(item, m->state.device,
                  m->state.interface_number, m->state.alternate)) known = true;
        }
        if (known) continue;
        if (!slot) break; /* no admission; existing sessions still get serviced */
        inspection *entry = 0, *empty = 0;
        for (size_t j = 0; j < RISC_USB_HID_MAX_INTERFACES; ++j) {
            inspection *p = &inspected[j];
            if (!p->device && !empty) empty = p;
            if (same_interface(item, p->device, p->iface, p->alt)) entry = p;
        }
        if (!entry) {
            if (!empty) continue;
            entry = empty;
            *entry = (inspection){.device = item->device, .iface = item->interface_number,
                .alt = item->alternate, .began_ms = now};
        }
        if (entry->done) continue;
        if (entry->attempts) {
            if (now < entry->attempted_ms || now - entry->began_ms >= DISCOVERY_DEADLINE_MS ||
                entry->attempts >= DISCOVERY_ATTEMPTS) { entry->done = true; continue; }
            if (now - entry->attempted_ms < DISCOVERY_BACKOFF_MS) continue;
        }
        if (session_serial == UINT64_MAX) { entry->done = true; return false; }
        ++entry->attempts; entry->attempted_ms = now;
        uint64_t raw = hid->open(hid->context, item->device, item->interface_number, item->alternate);
        if (!raw) return false;
        /* Record ownership before the next dependency call, including a
         * failed protocol selection or failed/unsupported descriptor. */
        *slot = (mouse){0}; slot->raw_session = raw;
        slot->state = (risc_usb_mouse_state_v1){item->device, ++session_serial, 0,
            item->interface_number, item->alternate, 0, 0};
        bool valid = false;
        uint8_t descriptor[RISC_USB_HID_MAX_DESCRIPTOR];
        size_t length = sizeof(descriptor);
        bool received = hid->report_descriptor(hid->context, raw, descriptor, &length);
        if (received) valid = parse_layout(&slot->layout, descriptor, length);
        if (item->subclass == 1 && item->protocol == 2) {
            /* Prefer the declared report layout, preserving wheel/pan on
             * boot-capable mice. An unsupported layout can only fall back to
             * the independently specified boot protocol after SET_PROTOCOL. */
            bool boot = !valid;
            valid = hid->set_boot_protocol(hid->context, raw, boot);
            slot->layout.boot = boot;
        } else if (received && !valid) entry->done = true;
        if (!valid) {
            slot->closing = true; phase = RISC_USB_MOUSE_CLOSING;
            (void)close_step(); return false;
        }
        entry->done = true;
        slot->state.connected = 1;
        risc_usb_mouse_event_v1 event = {0};
        event.kind = RISC_USB_MOUSE_CONNECTED; event.state = slot->state; emit(&event);
        break; /* at most one claim/setup attempt per invocation */
    }
    return true;
}
static bool poll(void *context, size_t max_reports) {
    (void)context;
    if (!hid || !max_reports || max_reports > 16 || phase == RISC_USB_MOUSE_RETAINED || stopping)
        return false;
    if (phase == RISC_USB_MOUSE_CLOSING) return close_step();
    if (sequence == UINT64_MAX) {
        close_all(true); (void)close_step(); return false;
    }
    const uint64_t begun = clock_api->monotonic_ms(clock_api->context);
    if (begun == UINT64_MAX || !hid->scan(hid->context, 16)) {
        close_all(true); (void)close_step(); return false;
    }
    risc_usb_hid_interface_v1 items[RISC_USB_HID_MAX_INTERFACES];
    size_t count = RISC_USB_HID_MAX_INTERFACES;
    if (!hid->interfaces(hid->context, items, &count) || count > RISC_USB_HID_MAX_INTERFACES) {
        close_all(true); (void)close_step(); return false;
    }
    for (size_t j = 0; j < RISC_USB_MOUSE_MAX_DEVICES; ++j) {
        mouse *m = &mice[j];
        if (m->raw_session && (!listed(items, count, m->state.device,
            m->state.interface_number, m->state.alternate) || !hid->present(hid->context, m->raw_session)))
            disconnect_mouse(m, false);
    }
    if (have_closing()) { phase = RISC_USB_MOUSE_CLOSING; return close_step(); }
    for (size_t j = 0; j < RISC_USB_HID_MAX_INTERFACES; ++j) {
        inspection *p = &inspected[j];
        if (p->device && !listed(items, count, p->device, p->iface, p->alt)) *p = (inspection){0};
    }
    bool ok = discover(items, count, begun);
    if (phase != RISC_USB_MOUSE_RUNNING) return false;
    bool quiet[RISC_USB_MOUSE_MAX_DEVICES] = {0};
    for (size_t attempted = 0, visited = 0; attempted < max_reports && visited < 4u * 16u; ++visited) {
        uint64_t now = clock_api->monotonic_ms(clock_api->context);
        if (now == UINT64_MAX || now < begun) {
            close_all(true); (void)close_step(); return false;
        }
        if (now - begun >= REPORT_WORK_MS) break;
        unsigned index = read_cursor++ % RISC_USB_MOUSE_MAX_DEVICES;
        mouse *m = &mice[index];
        if (!m->raw_session || quiet[index]) continue;
        uint8_t report[RISC_USB_HID_MAX_REPORT];
        int32_t n = hid->read(hid->context, m->raw_session, report, sizeof(report), 1);
        ++attempted;
        if (n < 0 || n > (int32_t)sizeof(report) || (n && !apply_report(m, report, (size_t)n))) {
            disconnect_mouse(m, true);
            phase = RISC_USB_MOUSE_CLOSING; (void)close_step(); return false;
        }
        if (!n) quiet[index] = true;
    }
    /* Even an immediately-ready controller or an empty scan hands CPU back. */
    clock_api->sleep_ms(clock_api->context, 1);
    return ok;
}
static uint64_t subscribe(void *context, uint64_t filter) {
    (void)context;
    if (!hid || phase != RISC_USB_MOUSE_RUNNING || stopping || serial == UINT64_MAX) return 0;
    for (size_t i = 0; i < RISC_USB_INPUT_MAX_SUBSCRIBERS; ++i) if (!subscribers[i].token) {
        subscribers[i] = (subscriber){0};
        subscribers[i].token = ++serial; subscribers[i].filter = filter;
        subscribers[i].gap = true;
        return subscribers[i].token;
    }
    return 0;
}
static bool unsubscribe(void *context, uint64_t token) {
    (void)context;
    subscriber *s = subscription(token);
    if (!s) return false;
    *s = (subscriber){0}; return true;
}
static int32_t next(void *context, uint64_t token, risc_usb_mouse_event_v1 *out) {
    (void)context;
    subscriber *s = subscription(token);
    if (!s || !out) return -1;
    if (s->gap) {
        *out = (risc_usb_mouse_event_v1){0};
        out->kind = RISC_USB_MOUSE_GAP; out->sequence = sequence; return -2;
    }
    if (!s->count) return 0;
    *out = s->events[s->head];
    s->head = (s->head + 1u) % RISC_USB_INPUT_QUEUE_LENGTH; --s->count;
    return 1;
}
static bool snapshot(void *context, uint64_t token, risc_usb_mouse_state_v1 *out, size_t *capacity) {
    (void)context;
    subscriber *s = subscription(token);
    if (!s || !capacity) return false;
    size_t count = 0;
    for (size_t i = 0; i < RISC_USB_MOUSE_MAX_DEVICES; ++i)
        if (mice[i].state.connected && (!s->filter || s->filter == mice[i].state.device)) ++count;
    if (*capacity < count || (count && !out)) { *capacity = count; return false; }
    size_t n = 0;
    for (size_t i = 0; i < RISC_USB_MOUSE_MAX_DEVICES; ++i)
        if (mice[i].state.connected && (!s->filter || s->filter == mice[i].state.device))
            out[n++] = mice[i].state;
    *capacity = count; s->gap = false; s->head = s->count = 0;
    return true;
}
static uint32_t status(void *context) { (void)context; return phase; }
static bool start(const risc_provider_dependency_v1 *deps, size_t count) {
    if (hid || phase != RISC_USB_MOUSE_STOPPED || !deps || count != 2) return false;
    const risc_usb_hid_api_v1 *h = 0;
    const risc_platform_clock_api_v1 *c = 0;
    for (size_t i = 0; i < count; ++i) {
        if (equal(deps[i].capability_id, "usb.hid") && deps[i].api_version == 1 && !h)
            h = deps[i].api;
        else if (equal(deps[i].capability_id, "platform.clock") && deps[i].api_version == 1 && !c)
            c = deps[i].api;
        else return false;
    }
    if (!h || h->api_version != RISC_USB_HID_API_V1 || h->struct_size < sizeof(*h) ||
        !h->scan || !h->interfaces || !h->open || !h->report_descriptor || !h->set_boot_protocol ||
        !h->read || !h->present || !h->close || !c || c->api_version != RISC_PLATFORM_CLOCK_API_V1 ||
        c->struct_size < sizeof(*c) || !c->monotonic_ms || !c->sleep_ms) return false;
    hid = h; clock_api = c; phase = RISC_USB_MOUSE_RUNNING; stopping = false;
    return true;
}
static bool quiesce(void) {
    if (phase == RISC_USB_MOUSE_RETAINED) return false;
    for (size_t i = 0; i < RISC_USB_INPUT_MAX_SUBSCRIBERS; ++i)
        if (subscribers[i].token) return false;
    if (!hid) return true;
    stopping = true;
    close_all(false);
    if (!close_step()) return false;
    for (size_t i = 0; i < RISC_USB_HID_MAX_INTERFACES; ++i) inspected[i] = (inspection){0};
    return true;
}
static void stop(void) {
    if (!quiesce()) return;
    hid = 0; clock_api = 0; phase = RISC_USB_MOUSE_STOPPED;
}
static const risc_usb_mouse_api_v1 api = {
    RISC_USB_MOUSE_API_V1, sizeof(risc_usb_mouse_api_v1), 0,
    subscribe, unsubscribe, poll, next, snapshot, status
};
static const risc_driver_v2 driver = {
    RISC_PROVIDER_DRIVER_ABI_V2, sizeof(risc_driver_v2),
    "usb-hid-mouse", "usb.hid.mouse", RISC_USB_MOUSE_API_V1,
    &api, start, stop, quiesce
};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &driver : 0;
}

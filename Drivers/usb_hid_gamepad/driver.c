#include "RiscUsbHidV1.h"
#include "RiscUsbGamepadDiagnosticsV1.h"

/* Descriptor-driven HID gamepad provider. Driver calls and queues are
 * serialized by the provider executor; no firmware HID handlers exist. */
#define PADS 4u
#define FIELDS 40u
#define GLOBAL_STACK 4u
#define USAGES 32u
typedef struct { uint16_t bit; int32_t minimum, maximum; uint8_t size, kind, index; } field;
typedef struct {
    uint64_t session, device;
    uint8_t iface, alt, report_id;
    uint16_t report_bits;
    uint8_t field_count;
    field fields[FIELDS];
    risc_usb_gamepad_state_v1 state;
} gamepad;
#include "StateMailbox.h"
typedef struct {
    uint32_t page, size, count, report_id;
    int32_t minimum, maximum;
} globals;
static const risc_usb_hid_api_v1 *hid;
static gamepad pads[PADS];
static subscriber subscribers[RISC_USB_INPUT_MAX_SUBSCRIBERS];
static uint64_t serial, sequence;
static const char *discovery_status = "GAMEPAD DRIVER NOT STARTED";

static bool diagnostic(void *context, char *out, size_t capacity) {
    (void)context;
    if (!out || !capacity) return false;
    size_t i = 0;
    for (; i + 1 < capacity && discovery_status[i]; ++i) out[i] = discovery_status[i];
    out[i] = 0;
    return true;
}

static bool equal(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static uint32_t unsigned_item(const uint8_t *p, size_t n) {
    uint32_t v = 0;
    for (size_t i = 0; i < n; ++i) v |= (uint32_t)p[i] << (8u * i);
    return v;
}
static int32_t signed_item(const uint8_t *p, size_t n) {
    uint32_t v = unsigned_item(p, n);
    if (n && n < 4 && (v & (1u << (n * 8u - 1u))))
        v |= ~((1u << (n * 8u)) - 1u);
    return (int32_t)v;
}
static bool field_add(gamepad *pad, uint32_t bit, uint32_t size,
                      uint8_t kind, uint8_t index, const globals *g) {
    if (pad->field_count >= FIELDS || bit + size > RISC_USB_HID_MAX_REPORT * 8u ||
        size == 0 || size > 16) return false;
    if (kind == 2 && (g->minimum < -32768 || g->maximum > 65535 ||
        g->maximum <= g->minimum ||
        (uint32_t)g->maximum - (uint32_t)g->minimum > 65535u)) return false;
    field *f = &pad->fields[pad->field_count++];
    *f = (field){(uint16_t)bit, g->minimum, g->maximum,
                 (uint8_t)size, kind, index};
    return true;
}
/* Select the first usable gamepad input layout. Report IDs describe separate
 * input/output/feature layouts, not a reason to reject the whole interface.
 * Keep input bit offsets per ID and ignore other layouts after selection. */
static bool layout(gamepad *pad, const uint8_t *data, size_t length) {
    globals g = {0}, stack[GLOBAL_STACK];
    unsigned sp = 0, depth = 0;
    uint32_t usages[USAGES], usage_min = 0, usage_max = 0;
    unsigned usage_count = 0;
    bool min_set = false, max_set = false, selected = false, found = false;
    bool report_chosen = false;
    uint16_t positions[256] = {0};
    for (size_t at = 0; at < length;) {
        uint8_t prefix = data[at++];
        if (prefix == 0xfe) {
            if (length - at < 2) return false;
            size_t n = data[at];
            at += 2;
            if (n > length - at) return false;
            at += n; continue;
        }
        size_t n = prefix & 3u;
        if (n == 3) n = 4;
        if (n > length - at) return false;
        uint32_t value = unsigned_item(data + at, n);
        int32_t signed_value = signed_item(data + at, n);
        at += n;
        uint8_t type = (prefix >> 2u) & 3u, tag = prefix >> 4u;
        if (type == 1) {
            switch (tag) {
                case 0: g.page = value; break;
                case 1: g.minimum = signed_value; break;
                case 2: g.maximum = g.minimum < 0 ? signed_value : (int32_t)value; break;
                case 7: g.size = value; break;
                case 8:
                    if (!value || value > 255) return false;
                    g.report_id = value; break;
                case 9: g.count = value; break;
                case 10:
                    if (sp == GLOBAL_STACK) return false;
                    stack[sp++] = g; break;
                case 11:
                    if (!sp) return false;
                    g = stack[--sp]; break;
                default: break;
            }
        } else if (type == 2) {
            if (tag == 0) {
                if (usage_count == USAGES) return false;
                usages[usage_count++] = value;
            } else if (tag == 1) { usage_min = value; min_set = true; }
            else if (tag == 2) { usage_max = value; max_set = true; }
        } else if (type == 0) {
            if (tag == 10) {
                uint32_t usage = usage_count ? usages[0] : (min_set ? usage_min : 0);
                if (depth == 0 && value == 1 && g.page == 1 &&
                    (usage == 4 || usage == 5) && !found) {
                    selected = found = true;
                }
                if (++depth > 16) return false;
            } else if (tag == 12) {
                if (!depth) return false;
                if (--depth == 0) selected = false;
            } else if (tag == 8) {
                const uint32_t bit = positions[g.report_id];
                const uint32_t limit = (RISC_USB_HID_MAX_REPORT -
                                         (g.report_id ? 1u : 0u)) * 8u;
                if (!g.size || !g.count || g.size > 32 || g.count > 64 ||
                    g.size * g.count > limit || bit + g.size * g.count > limit)
                    return false;
                if (selected && !(value & 1u) && (value & 2u) &&
                    (!report_chosen || g.report_id == pad->report_id)) {
                    for (uint32_t i = 0; i < g.count; ++i) {
                        uint32_t usage = i < usage_count ? usages[i] :
                            (min_set && max_set && usage_min + i <= usage_max ?
                             usage_min + i : 0);
                        uint8_t kind = 0, index = 0;
                        if (g.page == 9 && usage >= 1 && usage <= 32 && g.size == 1) {
                            kind = 1; index = (uint8_t)(usage - 1u);
                        } else if (g.page == 1 && g.size <= 16) {
                            if (usage >= 0x30 && usage <= 0x35) {
                                kind = 2; index = (uint8_t)(usage - 0x30);
                            } else if (usage == 0x39 && g.size <= 8) kind = 3;
                        }
                        if (kind) {
                            if (!report_chosen) {
                                pad->report_id = (uint8_t)g.report_id;
                                report_chosen = true;
                            }
                            if (!field_add(pad, bit + i * g.size,
                                           g.size, kind, index, &g)) return false;
                        }
                    }
                }
                positions[g.report_id] = (uint16_t)(bit + g.size * g.count);
            }
            usage_count = 0; min_set = max_set = false;
        }
    }
    if (!found || depth || !pad->field_count || !report_chosen || sp) return false;
    pad->report_bits = positions[pad->report_id];
    return true;
}
static int32_t extract(const uint8_t *data, const field *f) {
    uint32_t value = 0;
    for (unsigned i = 0; i < f->size; ++i) {
        unsigned bit = f->bit + i;
        if (data[bit / 8u] & (1u << (bit % 8u))) value |= (1u << i);
    }
    if (f->minimum < 0 && (value & (1u << (f->size - 1u))))
        value |= ~((1u << f->size) - 1u);
    return (int32_t)value;
}
/* Bounded unsigned division avoids libgcc's 64-bit divider, which contributes
 * an unsupported R_XTENSA_NONE RELA. Denominator is nonzero and <=65535. */
static uint32_t divide_bounded(uint32_t numerator, uint32_t denominator) {
    uint32_t quotient = 0, remainder = 0;
    for (unsigned i = 32; i-- > 0;) {
        remainder = (remainder << 1u) | ((numerator >> i) & 1u);
        if (remainder >= denominator) {
            remainder -= denominator;
            quotient |= 1u << i;
        }
    }
    return quotient;
}
static int16_t normalize(int32_t v, int32_t lo, int32_t hi) {
    if (hi <= lo) return 0;
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    uint32_t offset = (uint32_t)v - (uint32_t)lo;
    uint32_t span = (uint32_t)hi - (uint32_t)lo;
    return (int16_t)(-32767 + (int32_t)divide_bounded(offset * 65534u, span));
}
static bool emit(uint8_t kind, const risc_usb_gamepad_state_v1 *state) {
    if (sequence == UINT64_MAX) return false;
    risc_usb_gamepad_event_v1 event = {0};
    event.sequence = ++sequence; event.kind = kind; event.state = *state;
    for (size_t i = 0; i < RISC_USB_INPUT_MAX_SUBSCRIBERS; ++i)
        mailbox_publish(&subscribers[i], &event);
    return true;
}
static bool state_changed(const risc_usb_gamepad_state_v1 *a,
                          const risc_usb_gamepad_state_v1 *b) {
    return a->buttons != b->buttons || a->x != b->x || a->y != b->y ||
           a->z != b->z || a->rx != b->rx || a->ry != b->ry ||
           a->rz != b->rz || a->hat != b->hat;
}
static bool apply(gamepad *pad, const uint8_t *report, size_t n) {
    if (pad->report_id) {
        if (!n || report[0] != pad->report_id) return true;
        ++report; --n;
    }
    if (n * 8u < pad->report_bits) return true;
    risc_usb_gamepad_state_v1 state = pad->state;
    state.buttons = 0; state.hat = 8;
    for (unsigned i = 0; i < pad->field_count; ++i) {
        const field *f = &pad->fields[i];
        int32_t v = extract(report, f);
        if (f->kind == 1 && v) state.buttons |= 1u << f->index;
        else if (f->kind == 2) {
            int16_t norm = normalize(v, f->minimum, f->maximum);
            switch (f->index) {
                case 0: state.x = norm; break;
                case 1: state.y = norm; break;
                case 2: state.z = norm; break;
                case 3: state.rx = norm; break;
                case 4: state.ry = norm; break;
                case 5: state.rz = norm; break;
            }
        } else if (f->kind == 3)
            state.hat = v >= f->minimum && v <= f->maximum &&
                        v - f->minimum < 8 ? (uint8_t)(v - f->minimum) : 8;
    }
    if (state_changed(&pad->state, &state)) {
        pad->state = state;
        return emit(3, &state);
    }
    return true;
}
static bool release_pad(gamepad *pad) {
    if (!pad->session) return true;
    pad->state.connected = 0; pad->state.buttons = 0; pad->state.hat = 8;
    pad->state.x = pad->state.y = pad->state.z = 0;
    pad->state.rx = pad->state.ry = pad->state.rz = 0;
    if (!emit(2, &pad->state) || !hid->close(hid->context, pad->session)) return false;
    *pad = (gamepad){0};
    return true;
}
static bool poll(void *ctx, size_t max_reports) {
    (void)ctx;
    discovery_status = "HID DISCOVERY POLL FAILED";
    if (!hid || !max_reports || max_reports > 16 ||
        !hid->scan(hid->context, 16)) return false;
    risc_usb_hid_interface_v1 items[RISC_USB_HID_MAX_INTERFACES];
    size_t count = RISC_USB_HID_MAX_INTERFACES;
    if (!hid->interfaces(hid->context, items, &count) ||
        count > RISC_USB_HID_MAX_INTERFACES) return false;
    discovery_status = count ? "NO GAMEPAD HID INTERFACE" : "NO HID INTERFACE DISCOVERED";
    for (unsigned j = 0; j < PADS; ++j) {
        gamepad *p = &pads[j];
        if (!p->session) continue;
        bool found = false;
        for (size_t i = 0; i < count; ++i)
            if (items[i].device == p->device && items[i].interface_number == p->iface &&
                items[i].alternate == p->alt) found = true;
        if ((!found || !hid->present(hid->context, p->session)) &&
            !release_pad(p)) return false;
    }
    for (size_t i = 0; i < count; ++i) {
        const risc_usb_hid_interface_v1 *item = &items[i];
        bool known = false;
        for (unsigned j = 0; j < PADS; ++j)
            if (pads[j].session && pads[j].device == item->device &&
                pads[j].iface == item->interface_number && pads[j].alt == item->alternate)
                known = true;
        if (known || (item->subclass == 1 &&
                      (item->protocol == 1 || item->protocol == 2))) continue;
        gamepad *slot = 0;
        for (unsigned j = 0; j < PADS; ++j)
            if (!pads[j].session) { slot = &pads[j]; break; }
        if (!slot) { discovery_status = "GAMEPAD CAPACITY EXHAUSTED"; return false; }
        uint64_t session = hid->open(hid->context, item->device,
                                     item->interface_number, item->alternate);
        if (!session) { discovery_status = "HID INTERFACE CLAIM FAILED"; continue; }
        uint8_t descriptor[RISC_USB_HID_MAX_DESCRIPTOR];
        size_t length = sizeof(descriptor);
        gamepad candidate = {0};
        const bool received = hid->report_descriptor(hid->context, session, descriptor, &length);
        if (!received || length > sizeof(descriptor) || !layout(&candidate, descriptor, length)) {
            discovery_status = received ? "HID REPORT DESCRIPTOR UNSUPPORTED" :
                                          "HID REPORT DESCRIPTOR READ FAILED";
            if (!hid->close(hid->context, session)) return false;
            continue;
        }
        candidate.session = session; candidate.device = item->device;
        candidate.iface = item->interface_number; candidate.alt = item->alternate;
        candidate.state.device = item->device;
        candidate.state.hat = 8; candidate.state.connected = 1;
        candidate.state.report_id = candidate.report_id;
        *slot = candidate;
        if (!emit(1, &slot->state)) return false;
    }
    /* Drain bursts fairly across active pads, within the caller's work budget.
     * Each read has a cooperative 10 ms deadline; an idle pad is tried once. */
    bool quiet[PADS] = {0};
    size_t attempted = 0;
    for (size_t round = 0; round < max_reports && attempted < max_reports; ++round) {
        bool active = false;
        for (unsigned j = 0; j < PADS && attempted < max_reports; ++j) {
            gamepad *p = &pads[j];
            if (!p->session || quiet[j]) continue;
            active = true;
            uint8_t report[RISC_USB_HID_MAX_REPORT] = {0};
            int32_t n = hid->read(hid->context, p->session, report, sizeof(report), 10);
            ++attempted;
            if (n < 0) {
                if (!hid->present(hid->context, p->session)) {
                    if (!release_pad(p)) return false;
                } else { discovery_status = "HID INTERRUPT READ FAILED"; return false; }
            } else if (!n) quiet[j] = true;
            else if (!apply(p, report, (size_t)n)) return false;
        }
        if (!active) break;
    }
    for (unsigned j = 0; j < PADS; ++j)
        if (pads[j].session) { discovery_status = "GAMEPAD INPUT LAYOUT CONNECTED"; break; }
    return true;
}
static uint64_t subscribe(void *ctx, uint64_t filter) {
    (void)ctx;
    if (!hid || serial == UINT64_MAX) return 0;
    for (unsigned i = 0; i < RISC_USB_INPUT_MAX_SUBSCRIBERS; ++i)
        if (!subscribers[i].token) {
            subscribers[i] = (subscriber){0};
            subscribers[i].token = ++serial;
            subscribers[i].filter = filter;
            return subscribers[i].token;
        }
    return 0;
}
static bool unsubscribe(void *ctx, uint64_t token) {
    (void)ctx;
    if (!token) return false;
    for (unsigned i = 0; i < RISC_USB_INPUT_MAX_SUBSCRIBERS; ++i)
        if (subscribers[i].token == token) {
            subscribers[i] = (subscriber){0}; return true;
        }
    return false;
}
static int32_t next(void *ctx, uint64_t token, risc_usb_gamepad_event_v1 *out) {
    (void)ctx;
    if (!hid || !token || !out) return -1;
    for (unsigned i = 0; i < RISC_USB_INPUT_MAX_SUBSCRIBERS; ++i) {
        subscriber *s = &subscribers[i];
        if (s->token != token) continue;
        return mailbox_take(s, out);
    }
    return -1;
}
static bool snapshot(void *ctx, risc_usb_gamepad_state_v1 *out, size_t *capacity) {
    (void)ctx;
    if (!hid || !capacity) return false;
    size_t count = 0;
    for (unsigned i = 0; i < PADS; ++i) if (pads[i].session) ++count;
    if (*capacity < count || (count && !out)) { *capacity = count; return false; }
    size_t n = 0;
    for (unsigned i = 0; i < PADS; ++i)
        if (pads[i].session) out[n++] = pads[i].state;
    *capacity = count;
    return true;
}
static bool start(const risc_provider_dependency_v1 *deps, size_t count) {
    if (hid || !deps || count != 1 || !equal(deps[0].capability_id, "usb.hid") ||
        deps[0].api_version != RISC_USB_HID_API_V1 || !deps[0].api) return false;
    const risc_usb_hid_api_v1 *api = (const risc_usb_hid_api_v1 *)deps[0].api;
    if (api->api_version != RISC_USB_HID_API_V1 ||
        api->struct_size < sizeof(*api) || !api->scan || !api->interfaces ||
        !api->open || !api->report_descriptor || !api->read ||
        !api->present || !api->close) return false;
    hid = api;
    discovery_status = "WAITING FOR HID DISCOVERY";
    return true;
}
static bool quiesce(void) {
    /* Final lease release must close physical claims even when the controller
     * remains attached. A live subscriber still forbids module unload. */
    for (unsigned i = 0; i < RISC_USB_INPUT_MAX_SUBSCRIBERS; ++i)
        if (subscribers[i].token) return false;
    for (unsigned i = 0; i < PADS; ++i)
        if (pads[i].session && !release_pad(&pads[i])) return false;
    return true;
}
static void stop(void) { if (quiesce()) hid = 0; }
static const risc_usb_gamepad_diagnostics_v1 api = {
    {RISC_USB_GAMEPAD_API_V1, sizeof(risc_usb_gamepad_diagnostics_v1), 0,
     subscribe, unsubscribe, poll, next, snapshot}, diagnostic
};
static const risc_driver_v2 driver = {
    RISC_PROVIDER_DRIVER_ABI_V2, sizeof(risc_driver_v2),
    "usb-hid-gamepad", "usb.hid.gamepad", RISC_USB_GAMEPAD_API_V1,
    &api, start, stop, quiesce
};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &driver : 0;
}

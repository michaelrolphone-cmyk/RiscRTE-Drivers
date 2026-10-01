#include "RiscInputNavigationV1.h"
#include "RiscTextInputV1.h"
#include "RiscUsbHidV1.h"

/* UI policy is an installed composite provider. Keyboard navigation consumes
 * transport-neutral input.text; gamepads retain their current class providers.
 * Physical transport startup/power stays in lower provider ELFs. */
#define DEVICES 4u
#define KEYBOARD 1u
#define HID_PAD 2u
#define XINPUT_PAD 4u

static const risc_text_input_api_v1 *text_input;
static const risc_usb_gamepad_api_v1 *pads[2];
static uint64_t subscription;
static uint8_t blocked, gated = 7u;
static uint32_t states[3], previous;
static bool sync_frame;

typedef struct {
    uint64_t source;
    uint16_t keys;
} key_state;

static key_state keys[DEVICES];
static uint64_t pad_devices[2][DEVICES];

static bool equal(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

static uint16_t key_bit(const risc_text_input_event_v1 *event) {
    if (!event) return 0;
    if (event->codepoint == ' ' &&
        !(event->modifiers &
          (RISC_TEXT_MOD_CTRL | RISC_TEXT_MOD_ALT | RISC_TEXT_MOD_META)))
        return 1u << 3;
    switch (event->key) {
        case RISC_TEXT_KEY_ESCAPE: return 1u << 0;
        case RISC_TEXT_KEY_ENTER: return 1u << 1;
        case RISC_TEXT_KEY_LEFT: return 1u << 4;
        case RISC_TEXT_KEY_RIGHT: return 1u << 5;
        case RISC_TEXT_KEY_UP: return 1u << 6;
        case RISC_TEXT_KEY_DOWN: return 1u << 7;
        case RISC_TEXT_KEY_PAGE_UP: return 1u << 8;
        case RISC_TEXT_KEY_PAGE_DOWN: return 1u << 9;
        case RISC_TEXT_KEY_HOME: return 1u << 10;
        default: return 0;
    }
}

static const uint32_t actions[] = {
    RISC_NAV_BACK, RISC_NAV_CONFIRM, RISC_NAV_CONFIRM, RISC_NAV_CONFIRM,
    RISC_NAV_LEFT, RISC_NAV_RIGHT, RISC_NAV_UP, RISC_NAV_DOWN,
    RISC_NAV_PAGE_BACK, RISC_NAV_PAGE_FORWARD, RISC_NAV_HOME
};

static uint32_t key_buttons(void) {
    uint16_t held = 0;
    for (unsigned i = 0; i < DEVICES; ++i) held |= keys[i].keys;
    uint32_t result = 0;
    for (unsigned i = 0; i < sizeof(actions) / sizeof(actions[0]); ++i)
        if (held & (1u << i)) result |= actions[i];
    return result;
}

static bool unsubscribe_text(void) {
    if (subscription &&
        !text_input->unsubscribe(text_input->context, subscription))
        return false;
    subscription = 0;
    return true;
}

static void clear_source(unsigned source) {
    if (states[source]) sync_frame = true;
    states[source] = 0;
    gated |= (uint8_t)(1u << source);
    if (!source)
        for (unsigned i = 0; i < DEVICES; ++i)
            keys[i] = (key_state){0};
}

static bool reset(void *context) {
    (void)context;
    for (unsigned i = 0; i < 3; ++i) clear_source(i);
    previous = 0;
    sync_frame = true;
    return !text_input || unsubscribe_text();
}

static bool foreground(void *context, const risc_input_foreground_v1 *claims,
                       size_t count) {
    (void)context;
    if (count > RISC_INPUT_NAVIGATION_MAX_FOREGROUND ||
        (count && !claims)) return false;
    uint8_t next = 0;
    for (size_t i = 0; i < count; ++i) {
        const char *cap = claims[i].capability;
        if (!cap || !claims[i].api_version) return false;
        if (equal(cap, "input.text") || equal(cap, "usb.hid.keyboard"))
            next |= KEYBOARD;
        if (equal(cap, "usb.hid.gamepad")) next |= HID_PAD;
        if (equal(cap, "usb.xinput.gamepad")) next |= XINPUT_PAD;
        if (equal(cap, "usb.hid")) next |= KEYBOARD | HID_PAD;
        if (equal(cap, "usb.host") || equal(cap, "usb.controller") ||
            equal(cap, "board.power.vbus") || equal(cap, "input.navigation"))
            next = 7u;
    }
    const uint8_t changed = blocked ^ next;
    blocked = next;
    for (unsigned i = 0; i < 3; ++i)
        if (changed & (1u << i)) clear_source(i);
    return !((changed | blocked) & KEYBOARD) || unsubscribe_text();
}

static bool poll_keyboard(void) {
    if (!subscription) {
        subscription = text_input->subscribe(text_input->context, 0);
        if (!subscription) return false;
    }
    if (gated & KEYBOARD) {
        /* A fresh semantic subscription has no historical key-down state.
         * Releases for keys held across the handoff are ignored below, so this
         * is a neutral rearm without a transport-specific snapshot. */
        gated &= (uint8_t)~KEYBOARD;
        states[0] = 0;
        return true;
    }
    if (!text_input->poll(text_input->context, 4)) return false;

    for (unsigned n = 0; n < RISC_TEXT_INPUT_QUEUE_LENGTH; ++n) {
        risc_text_input_event_v1 event = {0};
        const int32_t rc =
            text_input->next(text_input->context, subscription, &event);
        if (!rc) break;
        if (rc < 0 || event.kind == RISC_TEXT_EVENT_GAP) {
            clear_source(0);
            return unsubscribe_text();
        }

        key_state *slot = 0;
        for (unsigned i = 0; i < DEVICES; ++i)
            if (keys[i].source == event.source) {
                slot = &keys[i];
                break;
            }

        if (event.kind == RISC_TEXT_EVENT_DISCONNECTED) {
            if (slot) *slot = (key_state){0};
            sync_frame = true;
        } else if (event.kind == RISC_TEXT_EVENT_CONNECTED ||
                   event.kind == RISC_TEXT_EVENT_KEY_DOWN ||
                   event.kind == RISC_TEXT_EVENT_KEY_UP) {
            if (!slot) {
                for (unsigned i = 0; i < DEVICES; ++i) {
                    if (!keys[i].source) {
                        slot = &keys[i];
                        slot->source = event.source;
                        break;
                    }
                }
            }
            if (!slot) return false;
            const uint16_t bit = key_bit(&event);
            if (event.kind == RISC_TEXT_EVENT_KEY_DOWN) slot->keys |= bit;
            if (event.kind == RISC_TEXT_EVENT_KEY_UP)
                slot->keys &= (uint16_t)~bit;
        }

        const uint32_t next = key_buttons();
        if (next != states[0]) {
            states[0] = next;
            break;
        }
    }
    return true;
}

static uint32_t pad_buttons(const risc_usb_gamepad_state_v1 *pad, bool xinput) {
    if (!pad->connected) return 0;
    uint32_t out = 0;
    if (pad->buttons & (xinput ? 2u : 1u)) out |= RISC_NAV_CONFIRM;
    /* X is HID Button 3 on the tested receiver; XInput publishes X at bit 3. */
    if (pad->buttons & (xinput ? 8u : 4u)) out |= RISC_NAV_BACK;
    if (pad->buttons & 0x10u) out |= RISC_NAV_PAGE_BACK;
    if (pad->buttons & 0x20u) out |= RISC_NAV_PAGE_FORWARD;
    if (pad->x < -16000 || (pad->hat >= 5 && pad->hat <= 7))
        out |= RISC_NAV_LEFT;
    if (pad->x > 16000 || (pad->hat >= 1 && pad->hat <= 3))
        out |= RISC_NAV_RIGHT;
    if (pad->y < -16000 || pad->hat == 7 || pad->hat == 0 || pad->hat == 1)
        out |= RISC_NAV_UP;
    if (pad->y > 16000 || (pad->hat >= 3 && pad->hat <= 5))
        out |= RISC_NAV_DOWN;
    return out;
}

static bool poll_pad(unsigned index) {
    const risc_usb_gamepad_api_v1 *api = pads[index];
    risc_usb_gamepad_state_v1 snapshot[DEVICES] = {{0}};
    size_t count = DEVICES;
    if (!api->poll(api->context, 4) ||
        !api->snapshot(api->context, snapshot, &count) || count > DEVICES)
        return false;
    uint32_t next = 0;
    for (unsigned old = 0; old < DEVICES; ++old) if (pad_devices[index][old]) {
        bool present = false;
        for (size_t n = 0; n < count; ++n)
            if (snapshot[n].device == pad_devices[index][old] &&
                snapshot[n].connected) present = true;
        if (!present) sync_frame = true;
    }
    for (unsigned i = 0; i < DEVICES; ++i) pad_devices[index][i] = 0;
    for (size_t i = 0; i < count; ++i) {
        if (snapshot[i].connected) pad_devices[index][i] = snapshot[i].device;
        next |= pad_buttons(&snapshot[i], index == 1);
    }
    const uint8_t bit = (uint8_t)(1u << (index + 1));
    if (gated & bit) {
        if (!next) gated &= (uint8_t)~bit;
        next = 0;
    }
    states[index + 1] = next;
    return true;
}

static bool poll(void *context, risc_input_navigation_frame_v1 *out) {
    (void)context;
    if (!text_input || !out) return false;
    if (!(blocked & KEYBOARD) && !poll_keyboard()) clear_source(0);
    for (unsigned i = 0; i < 2; ++i)
        if (!(blocked & (1u << (i + 1))) && !poll_pad(i))
            clear_source(i + 1);
    const uint32_t current = states[0] | states[1] | states[2];
    *out = (risc_input_navigation_frame_v1){
        current,
        sync_frame ? 0 : current & ~previous,
        sync_frame ? 0 : previous & ~current
    };
    previous = current;
    sync_frame = false;
    return true;
}

static bool start(const risc_provider_dependency_v1 *deps, size_t count) {
    if (text_input || !deps || count != 3) return false;
    const char *names[] = {
        "input.text", "usb.hid.gamepad", "usb.xinput.gamepad"
    };
    for (unsigned i = 0; i < 3; ++i)
        if (!equal(deps[i].capability_id, names[i]) ||
            deps[i].api_version != 1 || !deps[i].api)
            return false;

    const risc_text_input_api_v1 *t = deps[0].api;
    if (t->api_version != RISC_TEXT_INPUT_API_V1 ||
        t->struct_size < sizeof(*t) || !t->subscribe || !t->unsubscribe ||
        !t->poll || !t->next)
        return false;

    for (unsigned i = 0; i < 2; ++i) {
        const risc_usb_gamepad_api_v1 *p = deps[i + 1].api;
        if (p->api_version != 1 || p->struct_size < sizeof(*p) ||
            !p->poll || !p->snapshot)
            return false;
    }
    text_input = t;
    pads[0] = deps[1].api;
    pads[1] = deps[2].api;
    blocked = 0;
    return reset(0);
}

static bool quiesce(void) { return !text_input || reset(0); }

static void stop(void) {
    if (!quiesce()) return;
    text_input = 0;
    pads[0] = pads[1] = 0;
}

static const risc_input_navigation_api_v1 api = {
    RISC_INPUT_NAVIGATION_API_V1, sizeof(risc_input_navigation_api_v1), 0,
    poll, foreground, reset
};

static const risc_driver_v2 driver = {
    RISC_PROVIDER_DRIVER_ABI_V2, sizeof(risc_driver_v2),
    "usb-ui-navigation", "input.navigation", RISC_INPUT_NAVIGATION_API_V1,
    &api, start, stop, quiesce
};

__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &driver : 0;
}

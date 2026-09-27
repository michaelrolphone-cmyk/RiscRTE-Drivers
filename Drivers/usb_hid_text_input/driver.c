#include "RiscProviderV2.h"
#include "RiscTextInputV1.h"
#include "RiscUsbHidV1.h"

/* USB boot-keyboard translation stays entirely in this ELF. Consumers see
 * only input.text events and do not know which transport produced them. */
#define SOURCES 4u

typedef struct {
    uint64_t source;
    bool caps;
} source_state;

typedef struct {
    uint64_t token;
    uint64_t filter;
    risc_text_input_event_v1 queue[RISC_TEXT_INPUT_QUEUE_LENGTH];
    uint8_t head;
    uint8_t count;
    bool gap;
} subscriber;

static const risc_usb_keyboard_api_v1 *keyboard;
static uint64_t keyboard_subscription;
static subscriber subscribers[RISC_TEXT_INPUT_MAX_SUBSCRIBERS];
static source_state sources[SOURCES];
static uint64_t sequence;
static uint64_t token_serial;

static bool equal(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

static source_state *source_for(uint64_t source, bool create) {
    source_state *free_slot = 0;
    for (unsigned i = 0; i < SOURCES; ++i) {
        if (sources[i].source == source) return &sources[i];
        if (!sources[i].source && !free_slot) free_slot = &sources[i];
    }
    if (create && free_slot) {
        free_slot->source = source;
        free_slot->caps = false;
        return free_slot;
    }
    return 0;
}

static void remove_source(uint64_t source) {
    source_state *slot = source_for(source, false);
    if (slot) *slot = (source_state){0};
}

static uint8_t semantic_modifiers(uint8_t raw, bool caps) {
    uint8_t out = 0;
    if (raw & 0x22u) out |= RISC_TEXT_MOD_SHIFT;
    if (raw & 0x11u) out |= RISC_TEXT_MOD_CTRL;
    if (raw & 0x44u) out |= RISC_TEXT_MOD_ALT;
    if (raw & 0x88u) out |= RISC_TEXT_MOD_META;
    if (caps) out |= RISC_TEXT_MOD_CAPS;
    return out;
}

static uint32_t printable(uint8_t usage, uint8_t modifiers) {
    const bool shift = (modifiers & RISC_TEXT_MOD_SHIFT) != 0;
    const bool caps = (modifiers & RISC_TEXT_MOD_CAPS) != 0;
    if (usage >= 4 && usage <= 29) {
        const bool upper = shift != caps;
        return (uint32_t)((upper ? 'A' : 'a') + usage - 4);
    }
    if (usage >= 30 && usage <= 39) {
        static const char normal[] = "1234567890";
        static const char shifted[] = "!@#$%^&*()";
        return (uint32_t)(shift ? shifted[usage - 30] : normal[usage - 30]);
    }
    switch (usage) {
        case 0x2c: return ' ';
        case 0x2d: return shift ? '_' : '-';
        case 0x2e: return shift ? '+' : '=';
        case 0x2f: return shift ? '{' : '[';
        case 0x30: return shift ? '}' : ']';
        case 0x31: return shift ? '|' : '\\';
        case 0x33: return shift ? ':' : ';';
        case 0x34: return shift ? '"' : '\'';
        case 0x35: return shift ? '~' : '`';
        case 0x36: return shift ? '<' : ',';
        case 0x37: return shift ? '>' : '.';
        case 0x38: return shift ? '?' : '/';
        case 0x54: return '/';
        case 0x55: return '*';
        case 0x56: return '-';
        case 0x57: return '+';
        case 0x59: return '1';
        case 0x5a: return '2';
        case 0x5b: return '3';
        case 0x5c: return '4';
        case 0x5d: return '5';
        case 0x5e: return '6';
        case 0x5f: return '7';
        case 0x60: return '8';
        case 0x61: return '9';
        case 0x62: return '0';
        case 0x63: return '.';
        case 0x67: return '=';
        default: return 0;
    }
}

static uint16_t semantic_key(uint8_t usage) {
    switch (usage) {
        case 0x28:
        case 0x58: return RISC_TEXT_KEY_ENTER;
        case 0x29: return RISC_TEXT_KEY_ESCAPE;
        case 0x2a: return RISC_TEXT_KEY_BACKSPACE;
        case 0x2b: return RISC_TEXT_KEY_TAB;
        case 0x39: return RISC_TEXT_KEY_CAPS_LOCK;
        case 0x4a: return RISC_TEXT_KEY_HOME;
        case 0x4b: return RISC_TEXT_KEY_PAGE_UP;
        case 0x4c: return RISC_TEXT_KEY_DELETE;
        case 0x4d: return RISC_TEXT_KEY_END;
        case 0x4e: return RISC_TEXT_KEY_PAGE_DOWN;
        case 0x4f: return RISC_TEXT_KEY_RIGHT;
        case 0x50: return RISC_TEXT_KEY_LEFT;
        case 0x51: return RISC_TEXT_KEY_DOWN;
        case 0x52: return RISC_TEXT_KEY_UP;
        default: return RISC_TEXT_KEY_NONE;
    }
}

static void mark_gap(void) {
    for (unsigned i = 0; i < RISC_TEXT_INPUT_MAX_SUBSCRIBERS; ++i) {
        subscriber *s = &subscribers[i];
        if (!s->token) continue;
        s->gap = true;
        s->head = s->count = 0;
    }
}

static bool emit(uint64_t source, uint8_t kind, uint16_t key,
                 uint32_t codepoint, uint8_t modifiers) {
    if (sequence == UINT64_MAX) return false;
    risc_text_input_event_v1 event = {
        ++sequence, source, codepoint, key, kind, modifiers
    };
    for (unsigned i = 0; i < RISC_TEXT_INPUT_MAX_SUBSCRIBERS; ++i) {
        subscriber *s = &subscribers[i];
        if (!s->token || (s->filter && s->filter != source) || s->gap) continue;
        if (s->count == RISC_TEXT_INPUT_QUEUE_LENGTH) {
            s->gap = true;
            s->head = s->count = 0;
            continue;
        }
        const unsigned tail = (s->head + s->count) % RISC_TEXT_INPUT_QUEUE_LENGTH;
        s->queue[tail] = event;
        ++s->count;
    }
    return true;
}

static bool translate(const risc_usb_keyboard_event_v1 *raw) {
    if (!raw) return false;
    if (raw->kind == 5) {
        mark_gap();
        for (unsigned i = 0; i < SOURCES; ++i) sources[i] = (source_state){0};
        return true;
    }
    if (raw->kind == 1) {
        (void)source_for(raw->device, true);
        return emit(raw->device, RISC_TEXT_EVENT_CONNECTED,
                    RISC_TEXT_KEY_NONE, 0, 0);
    }
    if (raw->kind == 2) {
        remove_source(raw->device);
        return emit(raw->device, RISC_TEXT_EVENT_DISCONNECTED,
                    RISC_TEXT_KEY_NONE, 0, 0);
    }
    if (raw->kind != 3 && raw->kind != 4) return true;

    /* Modifier transitions are carried on each ordinary key event. They do not
     * need their own semantic key records. */
    if (raw->usage >= 0xe0u && raw->usage <= 0xe7u) return true;

    source_state *source = source_for(raw->device, true);
    if (!source) {
        mark_gap();
        return true;
    }
    if (raw->usage == 0x39u && raw->kind == 3) source->caps = !source->caps;

    const uint8_t modifiers = semantic_modifiers(raw->modifiers, source->caps);
    const uint16_t key = semantic_key(raw->usage);
    const uint32_t codepoint = printable(raw->usage, modifiers);
    if (!key && !codepoint) return true;

    return emit(raw->device,
                raw->kind == 3 ? RISC_TEXT_EVENT_KEY_DOWN : RISC_TEXT_EVENT_KEY_UP,
                key, codepoint, modifiers);
}

static uint64_t subscribe(void *context, uint64_t filter) {
    (void)context;
    if (!keyboard || token_serial == UINT64_MAX) return 0;
    for (unsigned i = 0; i < RISC_TEXT_INPUT_MAX_SUBSCRIBERS; ++i) {
        if (subscribers[i].token) continue;
        subscribers[i] = (subscriber){0};
        subscribers[i].token = ++token_serial;
        subscribers[i].filter = filter;
        return subscribers[i].token;
    }
    return 0;
}

static bool unsubscribe(void *context, uint64_t token) {
    (void)context;
    if (!token) return false;
    for (unsigned i = 0; i < RISC_TEXT_INPUT_MAX_SUBSCRIBERS; ++i) {
        if (subscribers[i].token != token) continue;
        subscribers[i] = (subscriber){0};
        return true;
    }
    return false;
}

static bool poll(void *context, size_t max_reports) {
    (void)context;
    if (!keyboard || !keyboard_subscription || !max_reports || max_reports > 16u)
        return false;
    if (!keyboard->poll(keyboard->context, max_reports)) return false;

    for (unsigned i = 0; i < RISC_USB_INPUT_QUEUE_LENGTH; ++i) {
        risc_usb_keyboard_event_v1 raw = {0};
        const int32_t result =
            keyboard->next(keyboard->context, keyboard_subscription, &raw);
        if (!result) break;
        if (result < 0) {
            mark_gap();
            for (unsigned n = 0; n < SOURCES; ++n) sources[n] = (source_state){0};
            break;
        }
        if (!translate(&raw)) return false;
    }
    return true;
}

static int32_t next(void *context, uint64_t token, risc_text_input_event_v1 *out) {
    (void)context;
    if (!keyboard || !token || !out) return -1;
    for (unsigned i = 0; i < RISC_TEXT_INPUT_MAX_SUBSCRIBERS; ++i) {
        subscriber *s = &subscribers[i];
        if (s->token != token) continue;
        if (s->gap) {
            s->gap = false;
            s->head = s->count = 0;
            return -1;
        }
        if (!s->count) return 0;
        *out = s->queue[s->head];
        s->head = (s->head + 1u) % RISC_TEXT_INPUT_QUEUE_LENGTH;
        --s->count;
        return 1;
    }
    return -1;
}

static bool start(const risc_provider_dependency_v1 *deps, size_t count) {
    if (keyboard || !deps || count != 1 ||
        !equal(deps[0].capability_id, "usb.hid.keyboard") ||
        deps[0].api_version != RISC_USB_KEYBOARD_API_V1 || !deps[0].api)
        return false;
    const risc_usb_keyboard_api_v1 *candidate =
        (const risc_usb_keyboard_api_v1 *)deps[0].api;
    if (candidate->api_version != RISC_USB_KEYBOARD_API_V1 ||
        candidate->struct_size < sizeof(*candidate) ||
        !candidate->subscribe || !candidate->unsubscribe ||
        !candidate->poll || !candidate->next || !candidate->snapshot)
        return false;
    const uint64_t raw_subscription = candidate->subscribe(candidate->context, 0);
    if (!raw_subscription) return false;
    keyboard = candidate;
    keyboard_subscription = raw_subscription;
    return true;
}

static bool quiesce(void) {
    for (unsigned i = 0; i < RISC_TEXT_INPUT_MAX_SUBSCRIBERS; ++i)
        if (subscribers[i].token) return false;
    if (!keyboard) return true;
    if (keyboard_subscription &&
        !keyboard->unsubscribe(keyboard->context, keyboard_subscription))
        return false;
    keyboard_subscription = 0;
    return true;
}

static void stop(void) {
    if (!quiesce()) return;
    keyboard = 0;
    for (unsigned i = 0; i < SOURCES; ++i) sources[i] = (source_state){0};
}

static const risc_text_input_api_v1 api = {
    RISC_TEXT_INPUT_API_V1, sizeof(risc_text_input_api_v1), 0,
    subscribe, unsubscribe, poll, next
};

static const risc_driver_v2 driver = {
    RISC_PROVIDER_DRIVER_ABI_V2, sizeof(risc_driver_v2),
    "usb-hid-text-input", "input.text", RISC_TEXT_INPUT_API_V1,
    &api, start, stop, quiesce
};

__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &driver : 0;
}

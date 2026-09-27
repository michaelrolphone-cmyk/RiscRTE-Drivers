#pragma once
/* Transport-neutral text/key input capability. Physical transports translate
 * their native reports inside provider ELFs before publishing this interface.
 * Calls are serialized by the provider executor. Subscribers receive copied,
 * bounded events and never pointers into another module. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define RISC_TEXT_INPUT_API_V1 1u
#define RISC_TEXT_INPUT_MAX_SUBSCRIBERS 4u
#define RISC_TEXT_INPUT_QUEUE_LENGTH 32u

enum {
    RISC_TEXT_EVENT_CONNECTED = 1,
    RISC_TEXT_EVENT_DISCONNECTED = 2,
    RISC_TEXT_EVENT_KEY_DOWN = 3,
    RISC_TEXT_EVENT_KEY_UP = 4,
    RISC_TEXT_EVENT_GAP = 5
};

enum {
    RISC_TEXT_KEY_NONE = 0,
    RISC_TEXT_KEY_ENTER = 1,
    RISC_TEXT_KEY_ESCAPE = 2,
    RISC_TEXT_KEY_BACKSPACE = 3,
    RISC_TEXT_KEY_DELETE = 4,
    RISC_TEXT_KEY_LEFT = 5,
    RISC_TEXT_KEY_RIGHT = 6,
    RISC_TEXT_KEY_UP = 7,
    RISC_TEXT_KEY_DOWN = 8,
    RISC_TEXT_KEY_HOME = 9,
    RISC_TEXT_KEY_END = 10,
    RISC_TEXT_KEY_PAGE_UP = 11,
    RISC_TEXT_KEY_PAGE_DOWN = 12,
    RISC_TEXT_KEY_TAB = 13,
    RISC_TEXT_KEY_CAPS_LOCK = 14
};

enum {
    RISC_TEXT_MOD_SHIFT = 1u << 0,
    RISC_TEXT_MOD_CTRL = 1u << 1,
    RISC_TEXT_MOD_ALT = 1u << 2,
    RISC_TEXT_MOD_META = 1u << 3,
    RISC_TEXT_MOD_CAPS = 1u << 4
};

typedef struct {
    uint64_t sequence;
    uint64_t source;
    uint32_t codepoint;
    uint16_t key;
    uint8_t kind;
    uint8_t modifiers;
} risc_text_input_event_v1;

typedef struct {
    uint32_t api_version;
    uint32_t struct_size;
    void *context;
    uint64_t (*subscribe)(void *context, uint64_t source_or_zero);
    bool (*unsubscribe)(void *context, uint64_t subscription);
    bool (*poll)(void *context, size_t max_reports);
    /* 1 event, 0 empty, negative on a stale/overflowed subscription. A
     * negative result is a stream gap: discard derived held state. */
    int32_t (*next)(void *context, uint64_t subscription,
                    risc_text_input_event_v1 *out);
} risc_text_input_api_v1;

#ifdef __cplusplus
}
#endif

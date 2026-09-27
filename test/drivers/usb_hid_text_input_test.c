#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../Drivers/usb_hid_text_input/driver.c"

static risc_usb_keyboard_event_v1 raw_events[128];
static size_t raw_count, raw_cursor;
static bool raw_unsubscribe_ok = true;
static unsigned raw_polls;

static uint64_t raw_subscribe(void *ctx, uint64_t filter) {
    (void)ctx;
    (void)filter;
    raw_cursor = raw_count;
    return 77;
}
static bool raw_unsubscribe(void *ctx, uint64_t token) {
    (void)ctx;
    assert(token == 77);
    return raw_unsubscribe_ok;
}
static bool raw_poll(void *ctx, size_t bound) {
    (void)ctx;
    assert(bound > 0 && bound <= 16);
    ++raw_polls;
    return true;
}
static int32_t raw_next(void *ctx, uint64_t token,
                        risc_usb_keyboard_event_v1 *out) {
    (void)ctx;
    assert(token == 77);
    if (raw_cursor == raw_count) return 0;
    *out = raw_events[raw_cursor++];
    return 1;
}
static bool raw_snapshot(void *ctx, risc_usb_keyboard_state_v1 *out,
                         size_t *count) {
    (void)ctx;
    if (*count && out) memset(out, 0, sizeof(*out));
    *count = 0;
    return true;
}
static void raw(uint8_t kind, uint8_t usage, uint8_t modifiers) {
    assert(raw_count < sizeof(raw_events) / sizeof(raw_events[0]));
    raw_events[raw_count++] = (risc_usb_keyboard_event_v1){
        .device = 11, .kind = kind, .usage = usage, .modifiers = modifiers
    };
}
static risc_text_input_event_v1 take(uint64_t token) {
    risc_text_input_event_v1 event = {0};
    assert(next(0, token, &event) == 1);
    return event;
}
static void expect_empty(uint64_t token) {
    risc_text_input_event_v1 event = {0};
    assert(next(0, token, &event) == 0);
}

int main(void) {
    risc_usb_keyboard_api_v1 raw_api = {
        1, sizeof(raw_api), 0, raw_subscribe, raw_unsubscribe,
        raw_poll, raw_next, raw_snapshot
    };
    risc_provider_dependency_v1 dependency = {
        "usb.hid.keyboard", 1, &raw_api
    };
    assert(start(&dependency, 1));
    const uint64_t consumer = subscribe(0, 0);
    assert(consumer);

    raw(1, 0, 0);
    raw(3, 0x04, 0);  /* a */
    raw(4, 0x04, 0);
    assert(poll(0, 4));
    risc_text_input_event_v1 e = take(consumer);
    assert(e.kind == RISC_TEXT_EVENT_CONNECTED && e.source == 11);
    e = take(consumer);
    assert(e.kind == RISC_TEXT_EVENT_KEY_DOWN && e.codepoint == 'a');
    e = take(consumer);
    assert(e.kind == RISC_TEXT_EVENT_KEY_UP && e.codepoint == 'a');

    raw(3, 0x04, 0x02); /* Shift+A */
    raw(4, 0x04, 0x02);
    assert(poll(0, 4));
    assert(take(consumer).codepoint == 'A');
    assert(take(consumer).codepoint == 'A');

    raw(3, 0x39, 0); /* Caps on. */
    raw(4, 0x39, 0);
    raw(3, 0x04, 0);
    raw(4, 0x04, 0);
    assert(poll(0, 4));
    e = take(consumer);
    assert(e.key == RISC_TEXT_KEY_CAPS_LOCK &&
           (e.modifiers & RISC_TEXT_MOD_CAPS));
    (void)take(consumer);
    assert(take(consumer).codepoint == 'A');
    assert(take(consumer).codepoint == 'A');

    raw(3, 0x04, 0x02); /* Shift XOR Caps -> lower. */
    assert(poll(0, 4));
    e = take(consumer);
    assert(e.codepoint == 'a' &&
           (e.modifiers & (RISC_TEXT_MOD_SHIFT | RISC_TEXT_MOD_CAPS)) ==
               (RISC_TEXT_MOD_SHIFT | RISC_TEXT_MOD_CAPS));

    raw(3, 0x1e, 0x02); /* ! */
    raw(3, 0x38, 0x02); /* ? */
    raw(3, 0x59, 0);    /* keypad 1 */
    assert(poll(0, 4));
    assert(take(consumer).codepoint == '!');
    assert(take(consumer).codepoint == '?');
    assert(take(consumer).codepoint == '1');

    raw(3, 0x04, 0x01); /* Ctrl+A retains the command modifier. */
    raw(3, 0x28, 0);
    raw(3, 0x2a, 0);
    raw(3, 0x50, 0);
    assert(poll(0, 4));
    e = take(consumer);
    assert(e.codepoint == 'A' && (e.modifiers & RISC_TEXT_MOD_CTRL));
    e = take(consumer);
    assert(e.key == RISC_TEXT_KEY_ENTER);
    e = take(consumer);
    assert(e.key == RISC_TEXT_KEY_BACKSPACE);
    e = take(consumer);
    assert(e.key == RISC_TEXT_KEY_LEFT);

    /* Modifier-only reports do not become text records. */
    raw(3, 0xe1, 0x02);
    raw(4, 0xe1, 0);
    assert(poll(0, 2));
    expect_empty(consumer);

    raw(2, 0, 0);
    assert(poll(0, 2));
    e = take(consumer);
    assert(e.kind == RISC_TEXT_EVENT_DISCONNECTED);

    assert(unsubscribe(0, consumer));
    assert(quiesce());
    stop();
    assert(!keyboard);
    assert(raw_polls > 0);
    puts("USB HID to transport-neutral text translation and lifecycle: PASS");
}

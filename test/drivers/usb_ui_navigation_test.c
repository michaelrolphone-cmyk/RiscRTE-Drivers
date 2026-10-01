#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../Drivers/usb_ui_navigation/driver.c"

static risc_text_input_event_v1 events[32];
static size_t event_count, event_cursor;
static unsigned text_polls, pad_polls[2], subscriptions;
static bool unsubscribe_ok = true, pad_ok[2] = {true, true};
static risc_usb_gamepad_state_v1 pad_state[2] = {
    {.device = 21, .hat = 8, .connected = 1},
    {.device = 31, .hat = 8, .connected = 1}
};

static uint64_t subscribe_test(void *ctx, uint64_t filter) {
    (void)ctx;
    (void)filter;
    ++subscriptions;
    event_cursor = event_count;
    return subscriptions;
}
static bool unsubscribe_test(void *ctx, uint64_t token) {
    (void)ctx;
    assert(token);
    return unsubscribe_ok;
}
static bool text_poll_test(void *ctx, size_t bound) {
    (void)ctx;
    assert(bound <= 4);
    ++text_polls;
    return true;
}
static int32_t text_next_test(void *ctx, uint64_t token,
                              risc_text_input_event_v1 *event) {
    (void)ctx;
    assert(token);
    if (event_cursor == event_count) return 0;
    *event = events[event_cursor++];
    return 1;
}
static bool pad_poll_test(void *ctx, size_t bound) {
    const unsigned index = (unsigned)(uintptr_t)ctx;
    assert(bound <= 4);
    ++pad_polls[index];
    return pad_ok[index];
}
static bool pad_snapshot_test(void *ctx, risc_usb_gamepad_state_v1 *out,
                              size_t *count) {
    assert(*count >= 1);
    out[0] = pad_state[(uintptr_t)ctx];
    *count = 1;
    return true;
}
static void key_event(uint8_t kind, uint16_t key, uint32_t codepoint) {
    assert(event_count < 32);
    events[event_count++] = (risc_text_input_event_v1){
        .source = 11, .codepoint = codepoint, .key = key, .kind = kind
    };
}
static risc_input_navigation_frame_v1 frame(void) {
    risc_input_navigation_frame_v1 out;
    assert(poll(0, &out));
    return out;
}

int main(void) {
    risc_text_input_api_v1 text_api = {
        1, sizeof(text_api), 0,
        subscribe_test, unsubscribe_test, text_poll_test, text_next_test
    };
    risc_usb_gamepad_api_v1 pad_api[2] = {
        {1, sizeof(pad_api[0]), 0, 0, 0, pad_poll_test, 0, pad_snapshot_test},
        {1, sizeof(pad_api[1]), (void*)1, 0, 0, pad_poll_test, 0, pad_snapshot_test}
    };
    risc_provider_dependency_v1 dependencies[] = {
        {"input.text", 1, &text_api},
        {"usb.hid.gamepad", 1, &pad_api[0]},
        {"usb.xinput.gamepad", 1, &pad_api[1]}
    };
    assert(start(dependencies, 3));
    assert(!frame().buttons);

    pad_state[0].hat = 2;
    assert(frame().pressed == RISC_NAV_RIGHT);
    assert(frame().buttons == RISC_NAV_RIGHT && !frame().pressed);
    pad_state[0].hat = 8;
    assert(frame().released == RISC_NAV_RIGHT);
    pad_state[1].buttons = 2;
    assert(frame().pressed == RISC_NAV_CONFIRM);
    pad_state[1].buttons = 0;
    assert(frame().released == RISC_NAV_CONFIRM);

    /* X alone is Back in both protocols; A/B cannot accidentally back out.
     * Check held state and release as well as the initial edge. */
    for (unsigned source = 0; source < 2; ++source) {
        const uint32_t face[] = {1u, 2u, 4u, 8u};
        const uint32_t expected[2][4] = {
            {RISC_NAV_CONFIRM, 0, RISC_NAV_BACK, 0},
            {0, RISC_NAV_CONFIRM, 0, RISC_NAV_BACK}
        };
        for (unsigned button = 0; button < 4; ++button) {
            pad_state[source].buttons = face[button];
            assert(frame().pressed == expected[source][button]);
            risc_input_navigation_frame_v1 held = frame();
            assert(held.buttons == expected[source][button] && !held.pressed);
            pad_state[source].buttons = 0;
            assert(frame().released == expected[source][button]);
        }
    }

    key_event(RISC_TEXT_EVENT_KEY_DOWN, RISC_TEXT_KEY_ENTER, 0);
    key_event(RISC_TEXT_EVENT_KEY_UP, RISC_TEXT_KEY_ENTER, 0);
    assert(frame().pressed == RISC_NAV_CONFIRM);
    assert(frame().released == RISC_NAV_CONFIRM);

    key_event(RISC_TEXT_EVENT_KEY_DOWN, RISC_TEXT_KEY_NONE, ' ');
    key_event(RISC_TEXT_EVENT_KEY_UP, RISC_TEXT_KEY_NONE, ' ');
    assert(frame().pressed == RISC_NAV_CONFIRM);
    assert(frame().released == RISC_NAV_CONFIRM);

    /* A text field claims only semantic keyboard input; gamepads continue. */
    risc_input_foreground_v1 claim = {"input.text", 1};
    assert(foreground(0, &claim, 1));
    unsigned polls = text_polls;
    key_event(RISC_TEXT_EVENT_KEY_DOWN, RISC_TEXT_KEY_ENTER, 0);
    key_event(RISC_TEXT_EVENT_KEY_UP, RISC_TEXT_KEY_ENTER, 0);
    const size_t pending = event_cursor;
    assert(!frame().buttons && text_polls == polls && event_cursor == pending);
    pad_state[0].buttons = 1;
    assert(frame().pressed == RISC_NAV_CONFIRM);
    pad_state[0].buttons = 0;
    (void)frame();

    assert(foreground(0, 0, 0));
    assert(!frame().buttons && !frame().released);
    key_event(RISC_TEXT_EVENT_KEY_DOWN, RISC_TEXT_KEY_UP, 0);
    key_event(RISC_TEXT_EVENT_KEY_UP, RISC_TEXT_KEY_UP, 0);
    assert(frame().pressed == RISC_NAV_UP);
    assert(frame().released == RISC_NAV_UP);

    /* Direct/raw keyboard apps still suppress semantic keyboard navigation. */
    claim.capability = "usb.hid.keyboard";
    assert(foreground(0, &claim, 1));
    polls = text_polls;
    (void)frame();
    assert(text_polls == polls);
    assert(foreground(0, 0, 0));
    (void)frame();

    claim.capability = "usb.hid.gamepad";
    assert(foreground(0, &claim, 1));
    polls = pad_polls[0];
    pad_state[0].buttons = 1;
    assert(!frame().buttons && pad_polls[0] == polls);
    assert(foreground(0, 0, 0));
    assert(!frame().buttons);
    pad_state[0].buttons = 0;
    (void)frame();
    pad_state[0].buttons = 1;
    assert(frame().pressed == RISC_NAV_CONFIRM);
    pad_state[0].connected = 0;
    assert(!frame().released && !frame().buttons);
    pad_state[0].buttons = 0;
    pad_state[0].connected = 1;
    (void)frame();

    pad_ok[0] = false;
    key_event(RISC_TEXT_EVENT_KEY_DOWN, RISC_TEXT_KEY_ESCAPE, 0);
    assert(frame().pressed == RISC_NAV_BACK);
    key_event(RISC_TEXT_EVENT_KEY_UP, RISC_TEXT_KEY_ESCAPE, 0);
    assert(frame().released == RISC_NAV_BACK);
    pad_ok[0] = true;

    unsubscribe_ok = false;
    claim.capability = "input.text";
    assert(!foreground(0, &claim, 1));
    polls = text_polls;
    (void)frame();
    assert(text_polls == polls);
    assert(!quiesce());
    unsubscribe_ok = true;
    assert(quiesce());
    stop();
    assert(!text_input);
    puts("Navigation mapping, semantic text focus, gamepad coexistence and cleanup: PASS");
}

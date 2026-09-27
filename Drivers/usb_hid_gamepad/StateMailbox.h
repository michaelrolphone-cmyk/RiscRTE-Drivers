#pragma once

/* Compatibility notifications contain only the latest state of each device.
 * There is no button-transition FIFO. New consumers use poll + snapshot.
 * Keyboard subscriptions deliberately do not use this mailbox. */
typedef struct {
    uint64_t token, filter;
    risc_usb_gamepad_event_v1 current[PADS];
    uint8_t count;
    bool gap;
} subscriber;

static void mailbox_publish(subscriber *s, const risc_usb_gamepad_event_v1 *event) {
    if (!s->token || s->gap || (s->filter && s->filter != event->state.device)) return;
    for (size_t i = 0; i < s->count; ++i) {
        if (s->current[i].state.device != event->state.device) continue;
        const bool connected = s->current[i].kind == 1 && event->kind == 3;
        s->current[i] = *event;
        if (connected) s->current[i].kind = 1;
        return;
    }
    if (s->count == PADS) {
        /* Too many distinct attachment generations, never button activity. */
        s->count = 0; s->gap = true; return;
    }
    s->current[s->count++] = *event;
}

static int32_t mailbox_take(subscriber *s, risc_usb_gamepad_event_v1 *out) {
    if (s->gap) { s->gap = false; s->count = 0; return -1; }
    if (!s->count) return 0;
    size_t oldest = 0;
    for (size_t i = 1; i < s->count; ++i)
        if (s->current[i].sequence < s->current[oldest].sequence) oldest = i;
    *out = s->current[oldest];
    s->current[oldest] = s->current[--s->count];
    return 1;
}

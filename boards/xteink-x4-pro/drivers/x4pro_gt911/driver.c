/* X4 Pro GT911. Power is GPIO1 high + GPIO2 low. Reset uses RST=4 and INT=10
 * to select 0x5D. CrossPoint reports X at byte 0 and a portrait controller on
 * a landscape panel, so this provider swaps axes. flipX/flipY are not applied. */
#include "RiscGpioBankV1.h"
#include "RiscI2cBusV1.h"
#include "RiscPlatformClockV1.h"
#include "RiscTouchV1.h"
#include "x4pro_pins.h"
#include "x4pro_proto.h"
#include <stddef.h>
#include <string.h>

static const risc_gpio_bank_api_v1 *gpio_api;
static const risc_i2c_bus_api_v1 *bus;
static const risc_platform_clock_api_v1 *clock_api;
static uint64_t periph, touch_pwr, rst, intr, bus_claim;
static bool started;
static uint64_t sequence, subscription_serial;
static risc_touch_contact_v1 contacts[RISC_TOUCH_MAX_CONTACTS];
static uint8_t contact_count;
static uint32_t buttons;
static uint64_t snapshot_ms;
typedef struct {
    uint64_t token;
    risc_touch_event_v1 queue[RISC_TOUCH_QUEUE_LENGTH];
    uint8_t head, count;
    bool gap;
} sub_t;
static sub_t subs[RISC_TOUCH_MAX_SUBSCRIBERS];

static bool equal(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static void sleep_ms(uint32_t ms) { clock_api->sleep_ms(clock_api->context, ms); }
static bool read_reg(uint16_t reg, uint8_t *out, size_t length) {
    const uint8_t addr[2] = {(uint8_t)(reg >> 8), (uint8_t)reg};
    return bus->transact(bus->context, bus_claim, addr, 2, out, length, 30);
}
static bool write_reg8(uint16_t reg, uint8_t value) {
    const uint8_t cmd[3] = {(uint8_t)(reg >> 8), (uint8_t)reg, value};
    return bus->transact(bus->context, bus_claim, cmd, 3, NULL, 0, 30);
}
static bool emit(uint8_t kind, uint8_t id, uint16_t x, uint16_t y) {
    if (sequence == UINT64_MAX) return false;
    risc_touch_event_v1 event = {++sequence, snapshot_ms, kind, id, x, y};
    for (uint8_t i = 0; i < RISC_TOUCH_MAX_SUBSCRIBERS; ++i) {
        if (!subs[i].token || subs[i].gap) continue;
        if (subs[i].count == RISC_TOUCH_QUEUE_LENGTH) {
            subs[i].gap = true; subs[i].head = subs[i].count = 0; continue;
        }
        uint8_t tail = (uint8_t)((subs[i].head + subs[i].count) % RISC_TOUCH_QUEUE_LENGTH);
        subs[i].queue[tail] = event;
        ++subs[i].count;
    }
    return true;
}
static int find_id(const risc_touch_contact_v1 *items, uint8_t count, uint8_t id) {
    for (uint8_t i = 0; i < count; ++i) if (items[i].id == id) return i;
    return -1;
}
static bool apply(const risc_touch_contact_v1 *next, uint8_t count, uint32_t next_buttons) {
    for (uint8_t i = 0; i < contact_count; ++i)
        if (find_id(next, count, contacts[i].id) < 0 &&
            !emit(RISC_TOUCH_EVENT_UP, contacts[i].id, contacts[i].x, contacts[i].y))
            return false;
    for (uint8_t i = 0; i < count; ++i) {
        int old = find_id(contacts, contact_count, next[i].id);
        uint8_t kind = old < 0 ? RISC_TOUCH_EVENT_DOWN : RISC_TOUCH_EVENT_MOVE;
        if (old >= 0 && contacts[old].x == next[i].x && contacts[old].y == next[i].y) continue;
        if (!emit(kind, next[i].id, next[i].x, next[i].y)) return false;
    }
    if ((buttons & RISC_TOUCH_BUTTON_PRIMARY) && !(next_buttons & RISC_TOUCH_BUTTON_PRIMARY) &&
        !emit(RISC_TOUCH_EVENT_BUTTON_UP, 0, 0, 0)) return false;
    if (!(buttons & RISC_TOUCH_BUTTON_PRIMARY) && (next_buttons & RISC_TOUCH_BUTTON_PRIMARY) &&
        !emit(RISC_TOUCH_EVENT_BUTTON_DOWN, 0, 0, 0)) return false;
    contact_count = count;
    buttons = next_buttons;
    for (uint8_t i = 0; i < count; ++i) contacts[i] = next[i];
    return true;
}
static bool map_point(const uint8_t raw[8], risc_touch_contact_v1 *out) {
    if (!x4pro_gt911_map(raw, &out->x, &out->y, &out->id)) return false;
    out->reserved = 0;
    return true;
}
static bool poll(void *context, size_t max_reports) {
    (void)context;
    if (!started || !max_reports) return false;
    uint8_t status = 0;
    if (!read_reg(0x814E, &status, 1)) return false;
    if (!(status & 0x80u)) return true;
    uint8_t count = status & 0x0fu;
    uint32_t next_buttons = (status & 0x10u) ? RISC_TOUCH_BUTTON_PRIMARY : 0;
    if (count > RISC_TOUCH_MAX_CONTACTS) { (void)write_reg8(0x814E, 0); return false; }
    uint8_t raw[RISC_TOUCH_MAX_CONTACTS * 8u] = {0};
    risc_touch_contact_v1 next[RISC_TOUCH_MAX_CONTACTS] = {0};
    if (count && !read_reg(0x8150, raw, (size_t)count * 8u)) return false;
    for (uint8_t i = 0; i < count; ++i)
        if (!map_point(raw + (size_t)i * 8u, &next[i])) { (void)write_reg8(0x814E, 0); return false; }
    snapshot_ms = clock_api->monotonic_ms(clock_api->context);
    if (!apply(next, count, next_buttons)) return false;
    return write_reg8(0x814E, 0);
}
static uint64_t subscribe(void *context) {
    (void)context;
    if (!started || subscription_serial == UINT64_MAX) return 0;
    for (uint8_t i = 0; i < RISC_TOUCH_MAX_SUBSCRIBERS; ++i)
        if (!subs[i].token) { subs[i] = (sub_t){0}; subs[i].token = ++subscription_serial; return subs[i].token; }
    return 0;
}
static bool unsubscribe(void *context, uint64_t token) {
    (void)context;
    for (uint8_t i = 0; i < RISC_TOUCH_MAX_SUBSCRIBERS; ++i)
        if (subs[i].token == token) { subs[i] = (sub_t){0}; return true; }
    return false;
}
static int32_t next_event(void *context, uint64_t token, risc_touch_event_v1 *out) {
    (void)context;
    if (!started || !token || !out) return -1;
    for (uint8_t i = 0; i < RISC_TOUCH_MAX_SUBSCRIBERS; ++i) {
        if (subs[i].token != token) continue;
        if (subs[i].gap) { subs[i].gap = false; subs[i].head = subs[i].count = 0; return -1; }
        if (!subs[i].count) return 0;
        *out = subs[i].queue[subs[i].head];
        subs[i].head = (uint8_t)((subs[i].head + 1u) % RISC_TOUCH_QUEUE_LENGTH);
        --subs[i].count;
        return 1;
    }
    return -1;
}
static bool snapshot(void *context, risc_touch_snapshot_v1 *out) {
    (void)context;
    if (!started || !out) return false;
    *out = (risc_touch_snapshot_v1){0};
    out->sequence = sequence;
    out->timestamp_ms = snapshot_ms;
    out->width = X4PRO_PANEL_WIDTH;
    out->height = X4PRO_PANEL_HEIGHT;
    out->contact_count = contact_count;
    out->buttons = buttons;
    for (uint8_t i = 0; i < contact_count; ++i) out->contacts[i] = contacts[i];
    return true;
}
static bool release_all(void) {
    bool ok = true;
    if (bus_claim && bus && !bus->release_device(bus->context, bus_claim)) ok = false;
    bus_claim = 0;
    const uint64_t pins[] = {periph, touch_pwr, rst, intr};
    for (size_t i = 0; i < 4; ++i)
        if (pins[i] && gpio_api && !gpio_api->release(gpio_api->context, pins[i])) ok = false;
    periph = touch_pwr = rst = intr = 0;
    return ok;
}
static bool quiesce(void) {
    for (uint8_t i = 0; i < RISC_TOUCH_MAX_SUBSCRIBERS; ++i)
        if (subs[i].token) return false;
    if (!release_all()) return false;
    gpio_api = NULL; bus = NULL; clock_api = NULL; started = false;
    return true;
}
static bool start(const risc_provider_dependency_v1 *deps, size_t count) {
    if (started || !deps || count != 3u) return false;
    for (size_t i = 0; i < count; ++i) {
        if (equal(deps[i].capability_id, RISC_GPIO_BANK_CAPABILITY)) gpio_api = deps[i].api;
        else if (equal(deps[i].capability_id, "i2c.bus")) bus = deps[i].api;
        else if (equal(deps[i].capability_id, "platform.clock")) clock_api = deps[i].api;
    }
    if (!gpio_api || !bus || !clock_api || !bus->claim_device || !clock_api->sleep_ms) return false;
    if (!gpio_api->claim(gpio_api->context, X4PRO_PIN_PERIPH_EN, RISC_GPIO_OUTPUT, &periph) ||
        !gpio_api->claim(gpio_api->context, X4PRO_PIN_TOUCH_PWR, RISC_GPIO_OUTPUT, &touch_pwr) ||
        !gpio_api->claim(gpio_api->context, X4PRO_PIN_TOUCH_RST, RISC_GPIO_OUTPUT, &rst) ||
        !gpio_api->claim(gpio_api->context, X4PRO_PIN_TOUCH_INT, RISC_GPIO_OUTPUT, &intr)) {
        (void)quiesce(); return false;
    }
    (void)gpio_api->write(gpio_api->context, periph, true);
    (void)gpio_api->write(gpio_api->context, touch_pwr, false);
    (void)gpio_api->write(gpio_api->context, intr, false);
    (void)gpio_api->write(gpio_api->context, rst, false);
    sleep_ms(2);
    (void)gpio_api->write(gpio_api->context, rst, true);
    sleep_ms(10);
    if (!gpio_api->release(gpio_api->context, intr)) { (void)quiesce(); return false; }
    intr = 0;
    if (!gpio_api->claim(gpio_api->context, X4PRO_PIN_TOUCH_INT, RISC_GPIO_INPUT | RISC_GPIO_PULLUP, &intr)) {
        (void)quiesce(); return false;
    }
    sleep_ms(50);
    if (!bus->claim_device(bus->context, X4PRO_I2C_GT911, &bus_claim)) { (void)quiesce(); return false; }
    uint8_t id[4] = {0};
    if (!read_reg(0x8140, id, 4) || id[0] < 0x20) { (void)quiesce(); return false; }
    (void)write_reg8(0x814E, 0);
    started = true;
    snapshot_ms = clock_api->monotonic_ms(clock_api->context);
    return true;
}
static void stop(void) { (void)quiesce(); }
static const risc_touch_api_v1 touch_api = {
    RISC_TOUCH_API_V1, sizeof(touch_api), NULL,
    subscribe, unsubscribe, poll, next_event, snapshot
};
static const risc_driver_v2 driver = {
    RISC_PROVIDER_DRIVER_ABI_V2, sizeof(driver),
    "x4pro-gt911", "input.touch.raw", RISC_TOUCH_API_V1,
    &touch_api, start, stop, quiesce
};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &driver : NULL;
}

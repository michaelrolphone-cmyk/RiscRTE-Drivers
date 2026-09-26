#include "RiscTouchV1.h"
#include "RiscI2cBusV1.h"
#include "RiscPlatformClockV1.h"
#include <stddef.h>
#include <stdint.h>

#define GT911_PRIMARY_ADDRESS 0x5du
#define GT911_FALLBACK_ADDRESS 0x14u
#define GT911_PRODUCT_ID_REG 0x8140u
#define GT911_STATUS_REG 0x814eu
#define GT911_FIRST_POINT_REG 0x814fu
#define GT911_READY_MASK 0x80u
#define GT911_TOUCH_COUNT_MASK 0x0fu
#define GT911_HAVE_KEY_MASK 0x10u
#define GT911_TIMEOUT_MS 20u

typedef struct {
    uint64_t token;
    risc_touch_event_v1 queue[RISC_TOUCH_QUEUE_LENGTH];
    uint8_t head;
    uint8_t count;
    bool gap;
} touch_subscriber;

static const risc_i2c_bus_api_v1 *bus;
static const risc_platform_clock_api_v1 *clock_api;
static uint64_t bus_claim;
static uint64_t sequence;
static uint64_t subscription_serial;
static uint16_t surface_width;
static uint16_t surface_height;
static risc_touch_contact_v1 contacts[RISC_TOUCH_MAX_CONTACTS];
static uint8_t contact_count;
static uint32_t touch_buttons;
static uint64_t snapshot_timestamp_ms;
static touch_subscriber subscribers[RISC_TOUCH_MAX_SUBSCRIBERS];

static bool equal(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static uint64_t monotonic_ms(void) {
    if (!clock_api || !clock_api->monotonic_ms) return UINT64_MAX;
    return clock_api->monotonic_ms(clock_api->context);
}
static bool read_reg(uint16_t reg, uint8_t *out, size_t length) {
    const uint8_t address[2] = {(uint8_t)(reg >> 8u), (uint8_t)reg};
    return bus && bus_claim && out && length &&
        bus->transact(bus->context, bus_claim, address, sizeof(address),
                      out, length, GT911_TIMEOUT_MS);
}
static bool write_reg8(uint16_t reg, uint8_t value) {
    const uint8_t command[3] = {(uint8_t)(reg >> 8u), (uint8_t)reg, value};
    return bus && bus_claim &&
        bus->transact(bus->context, bus_claim, command, sizeof(command),
                      NULL, 0, GT911_TIMEOUT_MS);
}
static bool probe(uint8_t address) {
    uint64_t claim = 0;
    if (!bus->claim_device(bus->context, address, &claim) || !claim) return false;
    bus_claim = claim;
    uint8_t info[11] = {0};
    if (!read_reg(GT911_PRODUCT_ID_REG, info, sizeof(info))) {
        (void)bus->release_device(bus->context, bus_claim);
        bus_claim = 0;
        return false;
    }
    const uint16_t width = (uint16_t)(info[6] | ((uint16_t)info[7] << 8u));
    const uint16_t height = (uint16_t)(info[8] | ((uint16_t)info[9] << 8u));
    if (!width || !height || width > 4096u || height > 4096u ||
        !write_reg8(GT911_STATUS_REG, 0u)) {
        (void)bus->release_device(bus->context, bus_claim);
        bus_claim = 0;
        return false;
    }
    surface_width = width;
    surface_height = height;
    return true;
}
static int find_contact(const risc_touch_contact_v1 *items, uint8_t count, uint8_t id) {
    for (uint8_t i = 0; i < count; ++i) if (items[i].id == id) return (int)i;
    return -1;
}
static bool emit(uint8_t kind, uint8_t id, uint16_t x, uint16_t y, uint64_t timestamp_ms) {
    if (sequence == UINT64_MAX) return false;
    risc_touch_event_v1 event = {0};
    event.sequence = ++sequence; event.timestamp_ms = timestamp_ms;
    event.kind = kind; event.id = id; event.x = x; event.y = y;
    for (uint8_t i = 0; i < RISC_TOUCH_MAX_SUBSCRIBERS; ++i) {
        touch_subscriber *subscriber = &subscribers[i];
        if (!subscriber->token || subscriber->gap) continue;
        if (subscriber->count == RISC_TOUCH_QUEUE_LENGTH) {
            subscriber->gap = true; subscriber->head = 0; subscriber->count = 0; continue;
        }
        const uint8_t tail = (uint8_t)((subscriber->head + subscriber->count) % RISC_TOUCH_QUEUE_LENGTH);
        subscriber->queue[tail] = event;
        ++subscriber->count;
    }
    return true;
}
static bool apply_state(const risc_touch_contact_v1 *next, uint8_t next_count,
                        uint32_t next_buttons, uint64_t timestamp_ms) {
    for (uint8_t i = 0; i < contact_count; ++i)
        if (find_contact(next, next_count, contacts[i].id) < 0 &&
            !emit(RISC_TOUCH_EVENT_UP, contacts[i].id, contacts[i].x, contacts[i].y, timestamp_ms)) return false;
    for (uint8_t i = 0; i < next_count; ++i) {
        const int old_index = find_contact(contacts, contact_count, next[i].id);
        if (old_index < 0) {
            if (!emit(RISC_TOUCH_EVENT_DOWN, next[i].id, next[i].x, next[i].y, timestamp_ms)) return false;
        } else {
            const risc_touch_contact_v1 *old = &contacts[old_index];
            if ((old->x != next[i].x || old->y != next[i].y) &&
                !emit(RISC_TOUCH_EVENT_MOVE, next[i].id, next[i].x, next[i].y, timestamp_ms)) return false;
        }
    }
    if ((touch_buttons & RISC_TOUCH_BUTTON_PRIMARY) && !(next_buttons & RISC_TOUCH_BUTTON_PRIMARY) &&
        !emit(RISC_TOUCH_EVENT_BUTTON_UP, 0u, 0u, 0u, timestamp_ms)) return false;
    if (!(touch_buttons & RISC_TOUCH_BUTTON_PRIMARY) && (next_buttons & RISC_TOUCH_BUTTON_PRIMARY) &&
        !emit(RISC_TOUCH_EVENT_BUTTON_DOWN, 0u, 0u, 0u, timestamp_ms)) return false;
    for (uint8_t i = 0; i < next_count; ++i) contacts[i] = next[i];
    for (uint8_t i = next_count; i < RISC_TOUCH_MAX_CONTACTS; ++i) contacts[i] = (risc_touch_contact_v1){0};
    contact_count = next_count; touch_buttons = next_buttons; snapshot_timestamp_ms = timestamp_ms;
    return true;
}
static bool service_one(bool *had_report) {
    if (had_report) *had_report = false;
    uint8_t status = 0;
    if (!read_reg(GT911_STATUS_REG, &status, 1u)) return false;
    if (!(status & GT911_READY_MASK)) return true;
    if (had_report) *had_report = true;
    const uint8_t count = (uint8_t)(status & GT911_TOUCH_COUNT_MASK);
    const uint32_t next_buttons = (status & GT911_HAVE_KEY_MASK) ? RISC_TOUCH_BUTTON_PRIMARY : 0u;
    if (count > RISC_TOUCH_MAX_CONTACTS) { (void)write_reg8(GT911_STATUS_REG, 0u); return false; }
    uint8_t raw[RISC_TOUCH_MAX_CONTACTS * 8u] = {0};
    risc_touch_contact_v1 next[RISC_TOUCH_MAX_CONTACTS] = {0};
    if (count && !read_reg(GT911_FIRST_POINT_REG, raw, (size_t)count * 8u)) {
        (void)write_reg8(GT911_STATUS_REG, 0u); return false;
    }
    for (uint8_t i = 0; i < count; ++i) {
        const size_t at = (size_t)i * 8u;
        next[i].id = raw[at];
        next[i].x = (uint16_t)(raw[at + 1u] | ((uint16_t)raw[at + 2u] << 8u));
        next[i].y = (uint16_t)(raw[at + 3u] | ((uint16_t)raw[at + 4u] << 8u));
        if (next[i].x >= surface_width || next[i].y >= surface_height ||
            find_contact(next, i, next[i].id) >= 0) {
            (void)write_reg8(GT911_STATUS_REG, 0u); return false;
        }
    }
    if (!write_reg8(GT911_STATUS_REG, 0u)) return false;
    uint64_t now = monotonic_ms();
    if (now == UINT64_MAX) now = snapshot_timestamp_ms;
    return apply_state(next, count, next_buttons, now);
}
static uint64_t subscribe(void *context) {
    (void)context;
    if (!bus || !bus_claim || subscription_serial == UINT64_MAX) return 0;
    for (uint8_t i = 0; i < RISC_TOUCH_MAX_SUBSCRIBERS; ++i) {
        if (!subscribers[i].token) {
            subscribers[i] = (touch_subscriber){0};
            subscribers[i].token = ++subscription_serial;
            return subscribers[i].token;
        }
    }
    return 0;
}
static bool unsubscribe(void *context, uint64_t token) {
    (void)context;
    if (!token) return false;
    for (uint8_t i = 0; i < RISC_TOUCH_MAX_SUBSCRIBERS; ++i)
        if (subscribers[i].token == token) { subscribers[i] = (touch_subscriber){0}; return true; }
    return false;
}
static bool poll(void *context, size_t max_reports) {
    (void)context;
    if (!bus || !clock_api || !bus_claim || !max_reports || max_reports > 16u) return false;
    for (size_t i = 0; i < max_reports; ++i) {
        bool had_report = false;
        if (!service_one(&had_report)) return false;
        if (!had_report) break;
    }
    return true;
}
static int32_t next_event(void *context, uint64_t token, risc_touch_event_v1 *out) {
    (void)context;
    if (!bus || !bus_claim || !token || !out) return -1;
    for (uint8_t i = 0; i < RISC_TOUCH_MAX_SUBSCRIBERS; ++i) {
        touch_subscriber *subscriber = &subscribers[i];
        if (subscriber->token != token) continue;
        if (subscriber->gap) {
            subscriber->gap = false; subscriber->head = 0; subscriber->count = 0; return -1;
        }
        if (!subscriber->count) return 0;
        *out = subscriber->queue[subscriber->head];
        subscriber->head = (uint8_t)((subscriber->head + 1u) % RISC_TOUCH_QUEUE_LENGTH);
        --subscriber->count;
        return 1;
    }
    return -1;
}
static bool snapshot(void *context, risc_touch_snapshot_v1 *out) {
    (void)context;
    if (!bus || !bus_claim || !out) return false;
    *out = (risc_touch_snapshot_v1){0};
    out->sequence = sequence; out->timestamp_ms = snapshot_timestamp_ms;
    out->width = surface_width; out->height = surface_height;
    out->contact_count = contact_count; out->buttons = touch_buttons;
    for (uint8_t i = 0; i < contact_count; ++i) out->contacts[i] = contacts[i];
    return true;
}
static bool start(const risc_provider_dependency_v1 *dependencies, size_t dependency_count) {
    if (bus || clock_api || bus_claim || !dependencies || dependency_count != 2u) return false;
    const risc_i2c_bus_api_v1 *candidate_bus = NULL;
    const risc_platform_clock_api_v1 *candidate_clock = NULL;
    for (size_t i = 0; i < dependency_count; ++i) {
        if (equal(dependencies[i].capability_id, "i2c.bus") &&
            dependencies[i].api_version == RISC_I2C_BUS_API_V1)
            candidate_bus = (const risc_i2c_bus_api_v1 *)dependencies[i].api;
        else if (equal(dependencies[i].capability_id, "platform.clock") &&
                 dependencies[i].api_version == RISC_PLATFORM_CLOCK_API_V1)
            candidate_clock = (const risc_platform_clock_api_v1 *)dependencies[i].api;
    }
    if (!candidate_bus || candidate_bus->api_version != RISC_I2C_BUS_API_V1 ||
        candidate_bus->struct_size < sizeof(*candidate_bus) ||
        !candidate_bus->claim_device || !candidate_bus->transact || !candidate_bus->release_device ||
        !candidate_clock || candidate_clock->api_version != RISC_PLATFORM_CLOCK_API_V1 ||
        candidate_clock->struct_size < sizeof(*candidate_clock) || !candidate_clock->monotonic_ms)
        return false;
    bus = candidate_bus; clock_api = candidate_clock;
    if (monotonic_ms() == UINT64_MAX ||
        (!probe(GT911_PRIMARY_ADDRESS) && !probe(GT911_FALLBACK_ADDRESS))) {
        bus = NULL; clock_api = NULL; bus_claim = 0; return false;
    }
    sequence = 0; subscription_serial = 0; contact_count = 0; touch_buttons = 0;
    snapshot_timestamp_ms = monotonic_ms();
    for (uint8_t i = 0; i < RISC_TOUCH_MAX_CONTACTS; ++i) contacts[i] = (risc_touch_contact_v1){0};
    for (uint8_t i = 0; i < RISC_TOUCH_MAX_SUBSCRIBERS; ++i) subscribers[i] = (touch_subscriber){0};
    return true;
}
static bool quiesce(void) {
    if (!bus && !clock_api && !bus_claim) return true;
    for (uint8_t i = 0; i < RISC_TOUCH_MAX_SUBSCRIBERS; ++i)
        if (subscribers[i].token) return false;
    if (bus_claim && (!bus || !bus->release_device(bus->context, bus_claim))) return false;
    bus_claim = 0; bus = NULL; clock_api = NULL; contact_count = 0; touch_buttons = 0;
    surface_width = surface_height = 0;
    return true;
}
static void stop(void) { (void)quiesce(); }
static const risc_touch_api_v1 touch_api = {
    RISC_TOUCH_API_V1, sizeof(risc_touch_api_v1), NULL,
    subscribe, unsubscribe, poll, next_event, snapshot
};
static const risc_driver_v2 driver = {
    RISC_PROVIDER_DRIVER_ABI_V2, sizeof(risc_driver_v2),
    "gt911-touch", "input.touch.raw", RISC_TOUCH_API_V1,
    &touch_api, start, stop, quiesce
};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &driver : NULL;
}

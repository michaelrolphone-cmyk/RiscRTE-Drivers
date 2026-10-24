#include "RiscTouchV1.h"
#include "RiscI2cBusV1.h"
#include "RiscPlatformClockV1.h"
#include <stddef.h>
#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

/* Vendor CST9217 component 2.0.0 protocol: 16-bit big-endian register
 * selection, STOP, 2 ms settling, then a separate read. Never substitute
 * the repeated-START FT3168/GT911 transaction shape. */
#define CST9217_ADDRESS 0x5au
#define CST9217_DATA 0xd000u
#define CST9217_RESOLUTION 0xd1f8u
#define CST9217_TIMEOUT_MS 20u

typedef struct {
    uint64_t token;
    risc_touch_event_v1 queue[RISC_TOUCH_QUEUE_LENGTH];
    uint8_t head;
    uint8_t count;
    bool gap;
} touch_subscriber;

/* Public API calls can originate from a capture task and an app. The bus
 * mutex protects one transfer, not this complete status/read/ack/state cycle.
 * Lifecycle start/stop remains serialized by the grant-owning loader. */
static SemaphoreHandle_t state_lock;
static bool lock_state(void) {
    TickType_t ticks = pdMS_TO_TICKS(CST9217_TIMEOUT_MS);
    if (!ticks) ticks = 1;
    return state_lock && xSemaphoreTake(state_lock, ticks) == pdTRUE;
}
static void unlock_state(void) { (void)xSemaphoreGive(state_lock); }

static const risc_i2c_bus_api_v1 *bus;
static const risc_platform_clock_api_v1 *clock_api;
static uint64_t bus_claim;
static bool ready;
static bool state_valid;
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

static bool select_reg(uint16_t reg) {
    const uint8_t address[2]={(uint8_t)(reg>>8u),(uint8_t)reg};
    if(!bus->transact(bus->context,bus_claim,address,2,NULL,0,CST9217_TIMEOUT_MS))return false;
    clock_api->sleep_ms(clock_api->context,2);
    return true;
}
static bool read_reg(uint16_t reg,uint8_t *out,size_t length) {
    return select_reg(reg) && bus->transact(bus->context,bus_claim,NULL,0,out,length,CST9217_TIMEOUT_MS);
}
static bool probe(void) {
    uint64_t token=0;
    bool claimed=bus->claim_device(bus->context,CST9217_ADDRESS,&token);
    bus_claim=token; /* Keep even a false/nonzero result until checked release. */
    if(!claimed || !token)return false;
    const uint8_t mode[2]={0xd1,0x01};
    if(!select_reg(0xd101u) ||
       !bus->transact(bus->context,bus_claim,mode,2,NULL,0,CST9217_TIMEOUT_MS))return false;
    clock_api->sleep_ms(clock_api->context,10);
    uint8_t check[4],resolution[4],project[4];
    if(!read_reg(0xd1fcu,check,4) || !read_reg(CST9217_RESOLUTION,resolution,4) ||
       !read_reg(0xd204u,project,4))return false;
    surface_width=(uint16_t)(resolution[0]|((uint16_t)resolution[1]<<8));
    surface_height=(uint16_t)(resolution[2]|((uint16_t)resolution[3]<<8));
    return surface_width && surface_height && surface_width<=4096u && surface_height<=4096u;
}

static int find_contact(const risc_touch_contact_v1 *items, uint8_t count,
                        uint8_t id) {
    for (uint8_t i = 0; i < count; ++i)
        if (items[i].id == id) return (int)i;
    return -1;
}

static bool emit(uint8_t kind, uint8_t id, uint16_t x, uint16_t y,
                 uint64_t timestamp_ms) {
    if (sequence == UINT64_MAX) return false;
    risc_touch_event_v1 event = {0};
    event.sequence = ++sequence;
    event.timestamp_ms = timestamp_ms;
    event.kind = kind;
    event.id = id;
    event.x = x;
    event.y = y;

    for (uint8_t i = 0; i < RISC_TOUCH_MAX_SUBSCRIBERS; ++i) {
        touch_subscriber *subscriber = &subscribers[i];
        if (!subscriber->token || subscriber->gap) continue;
        if (subscriber->count == RISC_TOUCH_QUEUE_LENGTH) {
            subscriber->gap = true;
            subscriber->head = 0;
            subscriber->count = 0;
            continue;
        }
        const uint8_t tail =
            (uint8_t)((subscriber->head + subscriber->count) %
                      RISC_TOUCH_QUEUE_LENGTH);
        subscriber->queue[tail] = event;
        ++subscriber->count;
    }
    return true;
}

static bool apply_state(const risc_touch_contact_v1 *next,
                        uint8_t next_count, uint32_t next_buttons,
                        uint64_t timestamp_ms) {
    for (uint8_t i = 0; i < contact_count; ++i) {
        if (find_contact(next, next_count, contacts[i].id) < 0 &&
            !emit(RISC_TOUCH_EVENT_UP, contacts[i].id,
                  contacts[i].x, contacts[i].y, timestamp_ms))
            return false;
    }

    for (uint8_t i = 0; i < next_count; ++i) {
        const int old_index = find_contact(contacts, contact_count, next[i].id);
        if (old_index < 0) {
            if (!emit(RISC_TOUCH_EVENT_DOWN, next[i].id,
                      next[i].x, next[i].y, timestamp_ms))
                return false;
        } else {
            const risc_touch_contact_v1 *old = &contacts[old_index];
            if ((old->x != next[i].x || old->y != next[i].y) &&
                !emit(RISC_TOUCH_EVENT_MOVE, next[i].id,
                      next[i].x, next[i].y, timestamp_ms))
                return false;
        }
    }

    if ((touch_buttons & RISC_TOUCH_BUTTON_PRIMARY) &&
        !(next_buttons & RISC_TOUCH_BUTTON_PRIMARY) &&
        !emit(RISC_TOUCH_EVENT_BUTTON_UP, 0u, 0u, 0u, timestamp_ms))
        return false;
    if (!(touch_buttons & RISC_TOUCH_BUTTON_PRIMARY) &&
        (next_buttons & RISC_TOUCH_BUTTON_PRIMARY) &&
        !emit(RISC_TOUCH_EVENT_BUTTON_DOWN, 0u, 0u, 0u, timestamp_ms))
        return false;

    for (uint8_t i = 0; i < next_count; ++i) contacts[i] = next[i];
    for (uint8_t i = next_count; i < RISC_TOUCH_MAX_CONTACTS; ++i)
        contacts[i] = (risc_touch_contact_v1){0};
    contact_count = next_count;
    touch_buttons = next_buttons;
    snapshot_timestamp_ms = timestamp_ms;
    return true;
}

/* Discarding an invalid hardware report is a real stream discontinuity.
 * Tell every subscriber instead of allowing a later UP to fabricate a tap. */
static void invalidate_subscribers(void) {
    state_valid=false;
    for (uint8_t i = 0; i < RISC_TOUCH_MAX_SUBSCRIBERS; ++i) {
        if (!subscribers[i].token) continue;
        subscribers[i].gap = true;
        subscribers[i].head = subscribers[i].count = 0;
    }
}

static bool service_one(void) {
    uint8_t raw[10]={0};
    if(!read_reg(CST9217_DATA,raw,sizeof(raw))) {
        invalidate_subscribers();return false;
    }
    uint8_t count=raw[5]&0x7fu;
    if(raw[6]!=0xabu || count>1u) {invalidate_subscribers();return false;}
    risc_touch_contact_v1 next={0};
    /* The vendor's active status is 0x06; other valid statuses release. */
    if(count && (raw[0]&0x0fu)==0x06u) {
        next.x=(uint16_t)(((uint16_t)raw[1]<<4)|(raw[3]>>4));
        next.y=(uint16_t)(((uint16_t)raw[2]<<4)|(raw[3]&15u));
        if(next.x>=surface_width || next.y>=surface_height) {
            invalidate_subscribers();return false;
        }
    } else count=0;
    uint64_t now=monotonic_ms();
    if(now==UINT64_MAX || sequence>UINT64_MAX-2u) {invalidate_subscribers();return false;}
    if(!apply_state(&next,count,0,now))return false;
    state_valid=true;return true;
}

static uint64_t subscribe_locked(void *context) {
    (void)context;
    if (!ready || !bus || !bus_claim || subscription_serial == UINT64_MAX) return 0;
    for (uint8_t i = 0; i < RISC_TOUCH_MAX_SUBSCRIBERS; ++i) {
        if (!subscribers[i].token) {
            subscribers[i] = (touch_subscriber){0};
            subscribers[i].token = ++subscription_serial;
            return subscribers[i].token;
        }
    }
    return 0;
}

static bool unsubscribe_locked(void *context, uint64_t token) {
    (void)context;
    if (!token) return false;
    for (uint8_t i = 0; i < RISC_TOUCH_MAX_SUBSCRIBERS; ++i) {
        if (subscribers[i].token == token) {
            subscribers[i] = (touch_subscriber){0};
            return true;
        }
    }
    return false;
}

static bool poll_locked(void *context, size_t max_reports) {
    (void)context;
    if (!ready || !bus || !clock_api || !bus_claim || !max_reports || max_reports > 16u)
        return false;
    /* At most two 20 ms transfers and one 2 ms settling yield. */
    return service_one();
}

static int32_t next_event_locked(void *context, uint64_t token,
                          risc_touch_event_v1 *out) {
    (void)context;
    if (!ready || !bus || !bus_claim || !token || !out) return -1;
    for (uint8_t i = 0; i < RISC_TOUCH_MAX_SUBSCRIBERS; ++i) {
        touch_subscriber *subscriber = &subscribers[i];
        if (subscriber->token != token) continue;
        if (subscriber->gap) {
            subscriber->gap = false;
            subscriber->head = 0;
            subscriber->count = 0;
            return -1;
        }
        if (!subscriber->count) return 0;
        *out = subscriber->queue[subscriber->head];
        subscriber->head =
            (uint8_t)((subscriber->head + 1u) % RISC_TOUCH_QUEUE_LENGTH);
        --subscriber->count;
        return 1;
    }
    return -1;
}

static bool snapshot_locked(void *context, risc_touch_snapshot_v1 *out) {
    (void)context;
    if (!ready || !state_valid || !bus || !bus_claim || !out) return false;
    *out = (risc_touch_snapshot_v1){0};
    out->sequence = sequence;
    out->timestamp_ms = snapshot_timestamp_ms;
    out->width = surface_width;
    out->height = surface_height;
    out->contact_count = contact_count;
    out->buttons = touch_buttons;
    for (uint8_t i = 0; i < contact_count; ++i) out->contacts[i] = contacts[i];
    return true;
}

static bool start(const risc_provider_dependency_v1 *dependencies,
                  size_t dependency_count) {
    if (state_lock || bus || clock_api || bus_claim || !dependencies || dependency_count != 2u)
        return false;

    const risc_i2c_bus_api_v1 *candidate_bus = NULL;
    const risc_platform_clock_api_v1 *candidate_clock = NULL;
    for (size_t i = 0; i < dependency_count; ++i) {
        if (equal(dependencies[i].capability_id, "i2c.bus") &&
            dependencies[i].api_version == RISC_I2C_BUS_API_V1)
            candidate_bus = (const risc_i2c_bus_api_v1 *)dependencies[i].api;
        else if (equal(dependencies[i].capability_id, "platform.clock") &&
                 dependencies[i].api_version == RISC_PLATFORM_CLOCK_API_V1)
            candidate_clock =
                (const risc_platform_clock_api_v1 *)dependencies[i].api;
    }
    if (!candidate_bus || candidate_bus->api_version != RISC_I2C_BUS_API_V1 ||
        candidate_bus->struct_size < sizeof(*candidate_bus) ||
        !candidate_bus->claim_device || !candidate_bus->transact ||
        !candidate_bus->release_device ||
        !candidate_clock ||
        candidate_clock->api_version != RISC_PLATFORM_CLOCK_API_V1 ||
        candidate_clock->struct_size < sizeof(*candidate_clock) ||
        !candidate_clock->monotonic_ms || !candidate_clock->sleep_ms)
        return false;

    state_lock = xSemaphoreCreateMutex();
    if (!state_lock) return false;
    bus = candidate_bus;
    clock_api = candidate_clock;
    uint64_t initial_timestamp = UINT64_MAX;
    if (monotonic_ms() == UINT64_MAX ||
        !probe() || (initial_timestamp = monotonic_ms()) == UINT64_MAX) {
        if(bus_claim && !bus->release_device(bus->context,bus_claim))return false;
        bus = NULL;
        clock_api = NULL;
        bus_claim = 0;
        vSemaphoreDelete(state_lock);
        state_lock = NULL;
        return false;
    }

    ready = true;
    state_valid = true;
    sequence = 0;
    /* Keep subscription generations monotonic across start/stop. */
    contact_count = 0;
    touch_buttons = 0;
    snapshot_timestamp_ms = initial_timestamp;
    for (uint8_t i = 0; i < RISC_TOUCH_MAX_CONTACTS; ++i)
        contacts[i] = (risc_touch_contact_v1){0};
    for (uint8_t i = 0; i < RISC_TOUCH_MAX_SUBSCRIBERS; ++i)
        subscribers[i] = (touch_subscriber){0};
    return true;
}

static bool quiesce_locked(void) {
    if (!bus && !clock_api && !bus_claim) return true;
    for (uint8_t i = 0; i < RISC_TOUCH_MAX_SUBSCRIBERS; ++i)
        if (subscribers[i].token) return false;
    if (bus_claim && (!bus || !bus->release_device(bus->context, bus_claim)))
        return false;
    ready = false;
    state_valid = false;
    bus_claim = 0;
    bus = NULL;
    clock_api = NULL;
    contact_count = 0;
    touch_buttons = 0;
    surface_width = surface_height = 0;
    return true;
}

static uint64_t subscribe(void *context) {
    if (!lock_state()) return 0;
    const uint64_t result = subscribe_locked(context);
    unlock_state();
    return result;
}
static bool unsubscribe(void *context, uint64_t token) {
    if (!lock_state()) return false;
    const bool result = unsubscribe_locked(context, token);
    unlock_state();
    return result;
}
static bool poll(void *context, size_t max_reports) {
    if (!lock_state()) return false;
    const bool result = poll_locked(context, max_reports);
    unlock_state();
    return result;
}
static int32_t next_event(void *context, uint64_t token, risc_touch_event_v1 *out) {
    if (!lock_state()) return -2;
    const int32_t result = next_event_locked(context, token, out);
    unlock_state();
    return result;
}
static bool snapshot(void *context, risc_touch_snapshot_v1 *out) {
    if (!lock_state()) return false;
    const bool result = snapshot_locked(context, out);
    unlock_state();
    return result;
}
static bool quiesce(void) {
    if (!state_lock) return !bus && !clock_api && !bus_claim;
    if (!lock_state()) return false;
    const bool result = quiesce_locked();
    unlock_state();
    return result;
}
static void stop(void) {
    if (!quiesce()) return;
    /* The loader calls stop only after the last grant and API caller drained. */
    if (state_lock) vSemaphoreDelete(state_lock);
    state_lock = NULL;
}

static const risc_touch_api_v1 touch_api = {
    RISC_TOUCH_API_V1, sizeof(risc_touch_api_v1), NULL,
    subscribe, unsubscribe, poll, next_event, snapshot
};

static const risc_driver_v2 driver = {
    RISC_PROVIDER_DRIVER_ABI_V2, sizeof(risc_driver_v2),
    "cst9217-touch", "input.touch.raw", RISC_TOUCH_API_V1,
    &touch_api, start, stop, quiesce
};

__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &driver : NULL;
}

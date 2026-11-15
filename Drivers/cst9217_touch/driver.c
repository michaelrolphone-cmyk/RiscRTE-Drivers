#include "RiscTouchV1.h"
#include "RiscI2cBusV1.h"
#include "RiscPlatformClockV1.h"
#include <stddef.h>
#include <stdint.h>
#include "GardenPlatformV1.h"
#include "RiscTouchI2cV2.h"
#include "RiscProviderSyncV1.h"
#include "RiscPowerRailV1.h"
#include "ProviderLog.h"

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

/* Runtime owns this instance-scoped reentrancy guard. Ordinary device ELFs
 * must not import privileged FreeRTOS symbols or CAS their PSRAM-backed BSS.
 * Calls and lifecycle run on the Runtime owner task, not a capture task. */
static const risc_provider_sync_api_v1 *sync_api;
static uint64_t state_lock;
static bool unlock_failed;
static const risc_i2c_bus_api_v1 *bus;
static const risc_platform_clock_api_v1 *clock_api;
static const garden_gpio_v1 *gpio;
static const risc_power_rail_api_v1 *rail;
static const risc_hw_i2c_touch_v2 *hardware;
static uint64_t bus_claim, reset_claim;
static bool ready;
/* A failed command may have reached the controller. Keep custody and require
 * a checked reset/reprobe before any further samples or mode commands. */
static bool controlled, asleep, wake_required;
static bool timing_failed;
static bool state_valid;
static bool unlock_state(void) {
    if (!sync_api || !state_lock || !sync_api->unlock(sync_api->context, state_lock)) {
        unlock_failed = true;
        ready = state_valid = false;
        return false;
    }
    unlock_failed = false;
    return true;
}
static bool lock_state(void) {
    if (!sync_api || !state_lock) return false;
    if (unlock_failed && !unlock_state()) return false;
    return sync_api->try_lock(sync_api->context, state_lock);
}
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

static bool reset_delay(uint32_t milliseconds);
static bool select_reg(uint16_t reg) {
    const uint8_t address[2]={(uint8_t)(reg>>8u),(uint8_t)reg};
    if(!bus->transact(bus->context,bus_claim,address,2,NULL,0,CST9217_TIMEOUT_MS))return false;
    return reset_delay(2);
}
static bool read_reg(uint16_t reg,uint8_t *out,size_t length) {
    return select_reg(reg) && bus->transact(bus->context,bus_claim,NULL,0,out,length,CST9217_TIMEOUT_MS);
}
static bool claim_bus(void) {
    uint64_t token=0;
    bool claimed=bus->claim_device(bus->context,CST9217_ADDRESS,&token);
    bus_claim=token; /* Keep even a false/nonzero result until checked release. */
    return claimed && token;
}
static bool probe(void) {
    const uint8_t mode[2]={0xd1,0x01};
    if(!select_reg(0xd101u) ||
       !bus->transact(bus->context,bus_claim,mode,2,NULL,0,CST9217_TIMEOUT_MS))return false;
    if(!reset_delay(10))return false;
    uint8_t check[4],resolution[4],project[4];
    if(!read_reg(0xd1fcu,check,4) || !read_reg(CST9217_RESOLUTION,resolution,4) ||
       !read_reg(0xd204u,project,4))return false;
    surface_width=(uint16_t)(resolution[0]|((uint16_t)resolution[1]<<8));
    surface_height=(uint16_t)(resolution[2]|((uint16_t)resolution[3]<<8));
    return surface_width == hardware->base.width && surface_height == hardware->base.height;
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

static bool wake_device(void);
static bool sleep_failure(const char *stage) {
    provider_log(stage);
    return false;
}
static bool enter_command_mode(void) {
    /* Source-backed bounded retry: Waveshare SensorLib setMode makes three
     * D11E/D11E/readback attempts, settling 200 ms after failed I/O. Also wait
     * between stale acknowledgments rather than assuming immediate silicon. */
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        uint8_t mode[4] = {0};
        timing_failed = false;
        const char *failure = NULL;
        if (!select_reg(0xd11eu)) failure = "CST_SLEEP stage=command-first error=transfer";
        else if (!select_reg(0xd11eu)) failure = "CST_SLEEP stage=command-second error=transfer";
        else if (!read_reg(0x0002u, mode, 4)) failure = "CST_SLEEP stage=command-status error=transfer";
        else if (mode[1] != 0x1eu) failure = "CST_SLEEP stage=command-status error=mode";
        if (!failure) return true;
        if (timing_failed) return sleep_failure("CST_SLEEP stage=command-settle error=clock");
        provider_log(failure);
        if (attempt == 2) break;
        provider_log("CST_SLEEP stage=command-retry wait_ms=200");
        if (!reset_delay(200)) return sleep_failure("CST_SLEEP stage=command-retry error=clock");
    }
    return sleep_failure("CST_SLEEP stage=command-mode error=retry-exhausted");
}
static bool sleep_device(void) {
    if (asleep) return true;
    if (wake_required && !wake_device()) return sleep_failure("CST_SLEEP stage=reset-reprobe error=resume");
    wake_required = true;
    invalidate_subscribers();
    if (!enter_command_mode()) return false;
    uint8_t mode[2] = {0};
    if (!select_reg(0xd101u)) return sleep_failure("CST_SLEEP stage=debug-info error=transfer");
    if (!read_reg(0x0002u, mode, 2)) return sleep_failure("CST_SLEEP stage=debug-status error=transfer");
    if (mode[1] != 0x01u) return sleep_failure("CST_SLEEP stage=debug-status error=mode");
    if (!reset_delay(10)) return sleep_failure("CST_SLEEP stage=debug-settle error=clock");
    const uint8_t sleep_command[2] = {0xd1, 0x05};
    /* An unacknowledged D105 may have acted. Do not blindly retry it or erase
     * the last subscription: the existing checked reset/reprobe owns retry. */
    if (!bus->transact(bus->context, bus_claim, sleep_command, 2, NULL, 0,
                       CST9217_TIMEOUT_MS)) return sleep_failure("CST_SLEEP stage=sleep-command error=transfer retained=1");
    asleep = true;
    provider_log("CST_SLEEP stage=sleep-command result=acknowledged");
    return true;
}

static uint64_t subscribe_locked(void *context) {
    (void)context;
    if (!ready || !bus || !bus_claim || subscription_serial == UINT64_MAX) return 0;
    for (uint8_t i = 0; i < RISC_TOUCH_MAX_SUBSCRIBERS; ++i) {
        if (!subscribers[i].token) {
            if (wake_required && !wake_device()) return 0;
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
            bool last = true;
            for (uint8_t j = 0; j < RISC_TOUCH_MAX_SUBSCRIBERS; ++j)
                if (j != i && subscribers[j].token) last = false;
            /* Do not consume the last token until sleep is acknowledged. A
             * false transfer can have put the chip to sleep: retry resets. */
            if (last && !sleep_device()) return false;
            subscribers[i] = (touch_subscriber){0};
            return true;
        }
    }
    return false;
}

static bool poll_locked(void *context, size_t max_reports) {
    (void)context;
    if (!ready || wake_required || !bus || !clock_api || !bus_claim || !max_reports || max_reports > 16u)
        return false;
    /* At most two 20 ms transfers and one 2 ms settling yield. */
    return service_one();
}

static int32_t next_event_locked(void *context, uint64_t token,
                          risc_touch_event_v1 *out) {
    (void)context;
    /* A report GAP is recoverable after a later successful poll. Missing
     * provider ownership/readiness is not: callers must retain the invocation
     * rather than treating a disabled provider as an endless queue gap. */
    if (!ready || wake_required || !bus || !bus_claim || !out) return -2;
    if (!token) return -1;
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
    if (!ready || wake_required || !state_valid || !bus || !bus_claim || !out) return false;
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

/* Exact typed admission for the qualified source configuration. The loader
 * scopes bus/GPIO tables to this hardware.device before calling start. There
 * is no public table introspection API: do not invent or probe a global bus. */
static const void *dependency(const risc_provider_dependency_v1 *deps, size_t count,
                              const char *id, size_t minimum) {
    const void *found = NULL;
    for (size_t i = 0; i < count; ++i) {
        if (!equal(deps[i].capability_id, id)) continue;
        if (found || !deps[i].api || deps[i].api_version != 1) return NULL;
        const uint32_t *header = deps[i].api;
        if (header[0] != 1 || header[1] < minimum) return NULL;
        found = deps[i].api;
    }
    return found;
}
static bool admitted(const risc_hardware_device_v1 *device) {
    if (!device || !device->instance_id || !equal(device->compatible, "hynitron,cst9217") ||
        !equal(device->revision, "unspecified") || !equal(device->config_type, "touch.i2c") ||
        device->config_version != 2 || device->config_size != sizeof(risc_hw_i2c_touch_v2) ||
        !device->config) return false;
    const risc_hw_i2c_touch_v2 *v = device->config;
    const risc_hw_i2c_touch_v1 *c = &v->base;
    const risc_hw_bus_v1 *b = &c->bus;
    return c->struct_size == sizeof(*v) && b->struct_size == sizeof(*b) &&
        b->kind == RISC_HW_BUS_I2C && b->instance_id == 101 && b->controller == 0 &&
        b->frequency_hz == 400000 && !b->mode && !b->reserved[0] &&
        !b->reserved[1] && !b->reserved[2] && b->sda == 15 && b->scl == 14 &&
        b->sclk == -1 && b->mosi == -1 && b->miso == -1 &&
        c->width == 466 && c->height == 466 && c->address == CST9217_ADDRESS &&
        c->reset == 2 && !c->reset_active_high && c->reset_assert_ms == 10 &&
        c->reset_recovery_ms == 50 && c->irq == 11 && !c->irq_active_high &&
        !c->irq_pull_up && v->power == -1 && !v->power_active_high && !v->irq_output &&
        !v->alternate_address && !v->reserved[0] && !v->reserved[1] && !v->reserved[2];
}
static bool reset_delay(uint32_t milliseconds) {
    const uint64_t before = monotonic_ms();
    if (before == UINT64_MAX) {timing_failed=true;return false;}
    uint64_t now = before;
    /* RTOS delays round to tick boundaries, not elapsed-time guarantees.
     * Complete an early wake against the monotonic deadline, without an
     * unbounded loop or treating a stalled/backward clock as settled. */
    for (unsigned waits = 0; waits < 4; ++waits) {
        if (now - before >= milliseconds) return true;
        clock_api->sleep_ms(clock_api->context, milliseconds - (uint32_t)(now - before));
        const uint64_t after = monotonic_ms();
        if (after == UINT64_MAX || after < now) {timing_failed=true;return false;}
        now = after;
    }
    if (now - before >= milliseconds) return true;
    timing_failed=true;return false;
}
static bool reset_level(bool expected) {
    bool actual = !expected;
    return gpio->read(gpio->context, reset_claim, &actual) && actual == expected;
}
static bool wake_device(void) {
    /* GPIO2 is still exclusively owned, distinct from panel reset GPIO1. */
    state_valid = false;
    wake_required = true;
    asleep = false;
    if (!gpio->write(gpio->context, reset_claim, false) || !reset_level(false) ||
        !reset_delay(hardware->base.reset_assert_ms) ||
        !gpio->write(gpio->context, reset_claim, true) || !reset_level(true) ||
        !reset_delay(hardware->base.reset_recovery_ms) || !probe()) return false;
    const uint64_t now = monotonic_ms();
    if (now == UINT64_MAX) return false;
    contact_count = 0;
    touch_buttons = 0;
    snapshot_timestamp_ms = now;
    for (uint8_t i = 0; i < RISC_TOUCH_MAX_CONTACTS; ++i)
        contacts[i] = (risc_touch_contact_v1){0};
    wake_required = false;
    state_valid = true;
    return true;
}
static bool prepare_reset(void) {
    /* Observe the separately scoped supply. Never touch PMIC address 0x34 from
     * the touch bus, gate CPU-shared DCDC1, or acquire native display GPIO1. */
    risc_power_rail_info_v1 info = {.struct_size = sizeof(info)};
    if (!rail->get_info(rail->context, &info) || !info.configuration_verified ||
        !info.enabled || info.configured_millivolts != 3300) return false;
    bool claimed = gpio->claim(gpio->context, (uint8_t)hardware->base.reset,
                               true, false, false, &reset_claim);
    /* False/nonzero is cleanup custody, never permission to use the claim. */
    return claimed && reset_claim && reset_level(false) &&
        reset_delay(hardware->base.reset_assert_ms) &&
        gpio->write(gpio->context, reset_claim, true) && reset_level(true) &&
        reset_delay(hardware->base.reset_recovery_ms);
}
static bool quiesce(void);
static bool start(const risc_provider_dependency_v1 *deps, size_t count) {
    if (sync_api || state_lock || bus || clock_api || gpio || rail || hardware ||
        bus_claim || reset_claim || !deps || count != 6) return false;
    const risc_hardware_device_v1 *selected = dependency(deps, count, "hardware.device", sizeof(*selected));
    const risc_i2c_bus_api_v1 *selected_bus = dependency(deps, count, "i2c.bus", sizeof(*selected_bus));
    const risc_platform_clock_api_v1 *selected_clock = dependency(deps, count, "platform.clock", sizeof(*selected_clock));
    const garden_gpio_v1 *selected_gpio = dependency(deps, count, "platform.gpio",
        offsetof(garden_gpio_v1, release) + sizeof(selected_gpio->release));
    const risc_power_rail_api_v1 *selected_rail = dependency(deps, count, "power.rail", sizeof(*selected_rail));
    const risc_provider_sync_api_v1 *selected_sync = dependency(deps, count, "platform.sync", sizeof(*selected_sync));
    if (!admitted(selected) || !selected_bus || !selected_bus->claim_device ||
        !selected_bus->transact || !selected_bus->release_device ||
        !selected_clock || !selected_clock->monotonic_ms || !selected_clock->sleep_ms ||
        !selected_gpio || !selected_gpio->claim || !selected_gpio->write ||
        !selected_gpio->read || !selected_gpio->release ||
        !selected_rail || !selected_rail->get_info ||
        !selected_sync || !selected_sync->is_owner || !selected_sync->create ||
        !selected_sync->try_lock || !selected_sync->unlock || !selected_sync->destroy)
        return false;
    if (!selected_sync->is_owner(selected_sync->context)) return false;
    bus = selected_bus;
    clock_api = selected_clock;
    gpio = selected_gpio;
    rail = selected_rail;
    hardware = selected->config;
    sync_api = selected_sync;
    bool created = sync_api->create(sync_api->context, &state_lock);
    if (!created || !state_lock || !lock_state()) {
        (void)quiesce();
        return false;
    }
    uint64_t initial_timestamp = UINT64_MAX;
    bool success = monotonic_ms() != UINT64_MAX && prepare_reset() && claim_bus() && probe() &&
        (initial_timestamp = monotonic_ms()) != UINT64_MAX;
    if (success) {
        ready = state_valid = controlled = true;
        asleep = wake_required = false;
        sequence = 0;
        /* Keep subscription generations monotonic across start/stop. */
        contact_count = 0;
        touch_buttons = 0;
        snapshot_timestamp_ms = initial_timestamp;
        for (uint8_t i = 0; i < RISC_TOUCH_MAX_CONTACTS; ++i)
            contacts[i] = (risc_touch_contact_v1){0};
        for (uint8_t i = 0; i < RISC_TOUCH_MAX_SUBSCRIBERS; ++i)
            subscribers[i] = (touch_subscriber){0};
    }
    if (!unlock_state()) success = false;
    if (!success) (void)quiesce();
    return success;
}
static bool quiesce_locked(void) {
    for (uint8_t i = 0; i < RISC_TOUCH_MAX_SUBSCRIBERS; ++i)
        if (subscribers[i].token) return false;
    if (controlled && !asleep && !sleep_device()) return false;
    ready = state_valid = false;
    if (bus_claim) {
        if (!bus || !bus->release_device(bus->context, bus_claim)) return false;
        bus_claim = 0;
    }
    /* Never surrender reset while any CST transfer ownership remains. */
    if (reset_claim) {
        if (!gpio || !gpio->release(gpio->context, reset_claim)) return false;
        reset_claim = 0;
    }
    contact_count = 0;
    touch_buttons = 0;
    surface_width = surface_height = 0;
    controlled = asleep = wake_required = false;
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
    int index = -1;
    for (uint8_t i = 0; i < RISC_TOUCH_MAX_SUBSCRIBERS; ++i)
        if (token && subscribers[i].token == token) index = i;
    const bool result = unsubscribe_locked(context, token);
    if (!unlock_state()) {
        /* The sync contract retains the held lock on false. Preserve the
         * caller's cleanup token rather than admitting sleep through it. */
        if (result && index >= 0)
            subscribers[index] = (touch_subscriber){.token = token, .gap = true};
        return false;
    }
    return result;
}
static bool poll(void *context, size_t max_reports) {
    if (!lock_state()) return false;
    const bool result = poll_locked(context, max_reports);
    unlock_state();
    return result;
}
static int32_t next_event(void *context, uint64_t token, risc_touch_event_v1 *out) {
    /* A prior failed unlock retained the guard. Do not call any dependency
     * while reporting that terminal state; lifecycle cleanup owns its retry. */
    if (unlock_failed || !lock_state()) return -2;
    const int32_t result = next_event_locked(context, token, out);
    if (!unlock_state()) return -2;
    return result;
}
static bool snapshot(void *context, risc_touch_snapshot_v1 *out) {
    if (unlock_failed || !lock_state()) return false;
    const bool result = snapshot_locked(context, out);
    if (!unlock_state()) return false;
    return result;
}
static bool quiesce(void) {
    if (!sync_api) return !state_lock && !bus && !clock_api && !gpio && !rail &&
        !hardware && !bus_claim && !reset_claim;
    if (state_lock) {
        if (!lock_state()) return false;
        const bool result = quiesce_locked();
        const bool unlocked = unlock_state();
        if (!result || !unlocked) return false;
        if (!sync_api->destroy(sync_api->context, state_lock)) return false;
        state_lock = 0;
    } else if (!quiesce_locked()) return false;
    bus = NULL;
    clock_api = NULL;
    gpio = NULL;
    rail = NULL;
    hardware = NULL;
    sync_api = NULL;
    unlock_failed = false;
    return true;
}
static void stop(void) { (void)quiesce(); }

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

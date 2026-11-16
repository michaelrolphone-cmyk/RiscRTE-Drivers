/* Drivers-owned adaptation of T-Watch scoped I2C. See source-map.json.
 * Reuse the typed controller/bus contracts, not the historical firmware shim.
 * Runtime owns the nonblocking guard; provider data may reside in PSRAM. */
#include "RiscI2cBusV1.h"
#include "TWatchHardwareV1.h"
#include "TWatchPlatformV1.h"
#include "RiscProviderSyncV1.h"

static const twatch_i2c_controller_v1 *hw;
static const risc_provider_sync_api_v1 *sync_api;
static uint64_t controller, serial, state_lock;
static bool started, unlock_failed;
typedef struct { uint64_t token; uint8_t address; } device;
static device devices[8];

static bool equal(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static const void *dependency(const risc_provider_dependency_v1 *d, size_t n,
                              const char *id, size_t size) {
    const void *found = NULL;
    for (size_t i = 0; i < n; ++i) {
        if (!equal(d[i].capability_id, id)) continue;
        if (found || !d[i].api || d[i].api_version != 1) return NULL;
        const uint32_t *header = d[i].api;
        if (header[0] != 1 || header[1] < size) return NULL;
        found = d[i].api;
    }
    return found;
}
static bool admitted(const risc_hardware_device_v1 *selected) {
    if (!selected || !selected->instance_id ||
        !equal(selected->compatible, "espressif,esp32s3-i2c") ||
        !equal(selected->revision, "unspecified") ||
        !equal(selected->config_type, "controller.i2c") || selected->config_version != 1 ||
        !selected->config || selected->config_size < sizeof(tw_hw_i2c_controller_v1))
        return false;
    const tw_hw_i2c_controller_v1 *c = selected->config;
    const risc_hw_bus_v1 *b = &c->bus;
    /* Original controller admission bounds, without any board pin assignment. */
    return c->struct_size >= sizeof(*c) && c->struct_size <= selected->config_size &&
        b->struct_size == sizeof(*b) && b->kind == RISC_HW_BUS_I2C && b->instance_id &&
        b->controller <= 1 && !b->mode && !b->reserved[0] && !b->reserved[1] &&
        !b->reserved[2] && b->frequency_hz && b->frequency_hz <= 400000 &&
        b->sda >= 0 && (uint16_t)b->sda <= RISC_HW_MAX_GPIO &&
        b->scl >= 0 && (uint16_t)b->scl <= RISC_HW_MAX_GPIO && b->sda != b->scl &&
        b->sclk == RISC_HW_PIN_NONE && b->mosi == RISC_HW_PIN_NONE &&
        b->miso == RISC_HW_PIN_NONE;
}
static bool unlock(void) {
    if (!sync_api || !state_lock || !sync_api->unlock(sync_api->context, state_lock)) {
        unlock_failed = true;
        started = false;
        return false;
    }
    unlock_failed = false;
    return true;
}
static bool lock(void) {
    if (!sync_api || !state_lock) return false;
    /* Failed unlock retains ownership. Retry it before another acquisition. */
    if (unlock_failed && !unlock()) return false;
    return sync_api->try_lock(sync_api->context, state_lock);
}
static bool claim_device(void *context, uint8_t address, uint64_t *out) {
    (void)context;
    if (out) *out = 0;
    if (!out || address < 8 || address > 0x77 || !lock()) return false;
    size_t slot = 8;
    bool ok = started && serial != UINT64_MAX;
    for (size_t i = 0; i < 8; ++i) {
        if (devices[i].token && devices[i].address == address) ok = false;
        if (!devices[i].token) slot = i;
    }
    ok = ok && slot < 8;
    uint64_t token = 0;
    if (ok) {
        token = ++serial;
        devices[slot] = (device){token, address};
    }
    if (!unlock()) {
        /* A failed claim must not strand a child token the caller never got.
         * The failed unlock contract leaves this guard held, so undo safely. */
        if (ok) devices[slot] = (device){0};
        return false;
    }
    if (ok) *out = token;
    return ok;
}
static bool transact(void *context, uint64_t token, const uint8_t *tx, size_t tn,
                     uint8_t *rx, size_t rn, uint32_t ms) {
    (void)context;
    if (!token || (!tn && !rn) || tn > 512 || rn > 512 || (tn && !tx) ||
        (rn && !rx) || !ms || ms > 1000 || !lock()) return false;
    bool ok = false;
    if (started)
        for (size_t i = 0; i < 8; ++i)
            if (devices[i].token == token)
                ok = hw->transfer(hw->context, controller, devices[i].address,
                                  tx, tn, rx, rn, ms);
    const bool unlocked = unlock();
    return ok && unlocked;
}
static bool release_device(void *context, uint64_t token) {
    (void)context;
    if (!token || !lock()) return false;
    size_t slot = 8;
    device previous = {0};
    for (size_t i = 0; i < 8; ++i)
        if (devices[i].token == token) {
            slot = i;
            previous = devices[i];
            devices[i] = (device){0};
        }
    if (!unlock()) {
        /* False means custody remains retryable, including an unlock fault. */
        if (slot < 8) devices[slot] = previous;
        return false;
    }
    return slot < 8;
}
static bool quiesce_locked(void) {
    for (size_t i = 0; i < 8; ++i)
        if (devices[i].token) return false;
    started = false;
    if (controller) {
        if (!hw || !hw->close(hw->context, controller)) return false;
        controller = 0;
    }
    return true;
}
static bool quiesce(void) {
    if (!sync_api) return !state_lock && !hw && !controller;
    if (state_lock) {
        if (!lock()) return false;
        const bool cleaned = quiesce_locked();
        const bool unlocked = unlock();
        if (!cleaned || !unlocked) return false;
        if (!sync_api->destroy(sync_api->context, state_lock)) return false;
        state_lock = 0;
    } else if (!quiesce_locked()) return false;
    hw = NULL;
    sync_api = NULL;
    unlock_failed = false;
    return true;
}
static bool start(const risc_provider_dependency_v1 *d, size_t n) {
    if (started || controller || hw || sync_api || state_lock || !d || n != 3)
        return false;
    const risc_hardware_device_v1 *selected = dependency(d, n, "hardware.device", sizeof(*selected));
    const twatch_i2c_controller_v1 *selected_hw = dependency(d, n, "platform.i2c.controller", sizeof(*selected_hw));
    const risc_provider_sync_api_v1 *selected_sync = dependency(d, n, "platform.sync", sizeof(*selected_sync));
    if (!admitted(selected) || !selected_hw || !selected_hw->open || !selected_hw->transfer ||
        !selected_hw->close || !selected_sync || !selected_sync->is_owner ||
        !selected_sync->create || !selected_sync->try_lock || !selected_sync->unlock ||
        !selected_sync->destroy || !selected_sync->is_owner(selected_sync->context)) return false;
    const tw_hw_i2c_controller_v1 *config = selected->config;
    hw = selected_hw;
    sync_api = selected_sync;
    const bool created = sync_api->create(sync_api->context, &state_lock);
    if (!created || !state_lock || !lock()) {
        (void)quiesce();
        return false;
    }
    /* A false/nonzero open still has cleanup custody, never transfer authority. */
    bool ok = hw->open(hw->context, config->bus.controller, config->bus.sda,
                       config->bus.scl, config->bus.frequency_hz, &controller) && controller;
    started = ok;
    if (!unlock()) ok = false;
    if (!ok) (void)quiesce();
    return ok;
}
static void stop(void) { (void)quiesce(); }
static const risc_i2c_bus_api_v1 api = {
    RISC_I2C_BUS_API_V1, sizeof(api), NULL, claim_device, transact, release_device
};
static const risc_driver_v2 descriptor = {
    RISC_PROVIDER_DRIVER_ABI_V2, sizeof(descriptor), "twatch-i2c", "i2c.bus", 1,
    &api, start, stop, quiesce
};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &descriptor : NULL;
}

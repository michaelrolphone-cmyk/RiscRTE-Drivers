/* Transitional i2c.bus ELF. Upstream drivers bind only RiscI2cBusV1 and
 * cannot observe whether this provider delegates or owns I2C0 itself.
 * The firmware retains exclusive controller/pin/Wire ownership for now.
 * Only this privileged, verified ELF can import the private compatibility API.
 */
#include "RiscI2cBusV1.h"
#include "RiscFirmwareI2cCompatV1.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <stddef.h>
#include <stdint.h>

#define MAX_CLAIMS 12u

/*
 * 0.1.2 was hardware-stable but rejected a second caller while one transaction
 * was active. 0.1.3 replaced that busy flag with relocatable __atomic state;
 * the resulting ELF regressed provider activation/runtime behavior on the
 * target. 0.1.4 attempted to repair the same design by moving atomic state.
 *
 * 0.1.5 removes that atomic design entirely. One ordinary FreeRTOS mutex owns
 * all provider-local state and serializes complete synchronous transactions.
 * Callers therefore queue instead of receiving a synthetic busy failure, while
 * claim/release/quiesce cannot race an in-flight transaction. The firmware
 * transport still owns the board-level recursive I2C/Wire mutex, so physical
 * bus ownership remains in firmware until the later full I2C ELF cutover.
 */
typedef struct {
    uint64_t token;
    uint8_t address;
} device_claim;

static device_claim claims[MAX_CLAIMS];
static uint64_t next_token;
static SemaphoreHandle_t state_lock;
static bool started;

static SemaphoreHandle_t take_state(TickType_t wait_ticks) {
    SemaphoreHandle_t lock = state_lock;
    if (!lock || xSemaphoreTake(lock, wait_ticks) != pdTRUE) return NULL;
    return lock;
}

static void give_state(SemaphoreHandle_t lock) {
    if (lock) (void)xSemaphoreGive(lock);
}

static bool claim_device(void *context, uint8_t address, uint64_t *out) {
    (void)context;
    if (out) *out = 0;
    if (!out || address < 0x08u || address > 0x77u) return false;

    SemaphoreHandle_t lock = take_state(portMAX_DELAY);
    if (!lock) return false;

    bool ok = false;
    device_claim *empty = NULL;
    if (!started || next_token == UINT64_MAX) goto done;
    for (size_t i = 0; i < MAX_CLAIMS; ++i) {
        if (claims[i].token && claims[i].address == address) goto done;
        if (!claims[i].token && !empty) empty = &claims[i];
    }
    if (!empty) goto done;

    empty->address = address;
    empty->token = ++next_token;
    *out = empty->token;
    ok = true;

done:
    give_state(lock);
    return ok;
}

static bool transact(void *context, uint64_t token,
                     const uint8_t *write_bytes, size_t write_length,
                     uint8_t *read_bytes, size_t read_length,
                     uint32_t timeout_ms) {
    (void)context;
    if (!token || (!write_length && !read_length) ||
        write_length > RISC_FW_I2C_COMPAT_V1_MAX_BYTES ||
        read_length > RISC_FW_I2C_COMPAT_V1_MAX_BYTES ||
        (write_length && !write_bytes) || (read_length && !read_bytes) ||
        !timeout_ms || timeout_ms > RISC_FW_I2C_COMPAT_V1_MAX_TIMEOUT_MS)
        return false;

    const TickType_t begun = xTaskGetTickCount();
    TickType_t wait_ticks = pdMS_TO_TICKS(timeout_ms);
    if (!wait_ticks) wait_ticks = 1;
    SemaphoreHandle_t lock = take_state(wait_ticks);
    if (!lock) return false;

    uint8_t address = 0;
    if (started) {
        for (size_t i = 0; i < MAX_CLAIMS; ++i) {
            if (claims[i].token == token) {
                address = claims[i].address;
                break;
            }
        }
    }

    bool ok = false;
    const uint32_t waited_ms =
        (uint32_t)((TickType_t)(xTaskGetTickCount() - begun)) * portTICK_PERIOD_MS;
    if (address && waited_ms < timeout_ms) {
        ok = risc_fw_i2c_transact_v1(address, write_bytes, write_length,
                                     read_bytes, read_length, timeout_ms - waited_ms);
    }
    give_state(lock);

    /* A NACK or timed-out transaction is a failure; no fabricated read or
     * success is returned to board.power.vbus or another dependent provider. */
    return ok;
}

static bool release_device(void *context, uint64_t token) {
    (void)context;
    if (!token) return false;

    /*
     * Waiting for this mutex is the drain guarantee required by RiscI2cBusV1:
     * once acquired, no transaction can still be using this claim.
     */
    SemaphoreHandle_t lock = take_state(portMAX_DELAY);
    if (!lock) return false;

    bool ok = false;
    if (started) {
        for (size_t i = 0; i < MAX_CLAIMS; ++i) {
            if (claims[i].token == token) {
                claims[i].token = 0;
                claims[i].address = 0;
                ok = true;
                break;
            }
        }
    }

    give_state(lock);
    return ok;
}

static bool quiesce(void) {
    SemaphoreHandle_t lock = take_state(portMAX_DELAY);
    if (!lock) return !started;

    bool clear = true;
    for (size_t i = 0; i < MAX_CLAIMS; ++i) {
        if (claims[i].token) {
            clear = false;
            break;
        }
    }
    if (clear) started = false;

    give_state(lock);
    return clear;
}

static bool start(const risc_provider_dependency_v1 *dependencies,
                  size_t dependency_count) {
    (void)dependencies;
    if (dependency_count || started || state_lock) return false;
    for (size_t i = 0; i < MAX_CLAIMS; ++i)
        if (claims[i].token) return false;

    SemaphoreHandle_t lock = xSemaphoreCreateMutex();
    if (!lock) return false;
    state_lock = lock;
    started = true;
    return true;
}

static void stop(void) {
    SemaphoreHandle_t lock = state_lock;
    if (!lock) {
        started = false;
        return;
    }
    if (!quiesce()) return;
    state_lock = NULL;
    vSemaphoreDelete(lock);
}

static const risc_i2c_bus_api_v1 bus_api = {
    RISC_I2C_BUS_API_V1, sizeof(risc_i2c_bus_api_v1), NULL,
    claim_device, transact, release_device
};

static const risc_driver_v2 driver = {
    RISC_PROVIDER_DRIVER_ABI_V2, sizeof(risc_driver_v2),
    "i2c-esp32s3-v2", "i2c.bus", RISC_I2C_BUS_API_V1,
    &bus_api, start, stop, quiesce
};

__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &driver : NULL;
}

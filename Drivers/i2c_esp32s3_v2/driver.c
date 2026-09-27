/* Transitional i2c.bus ELF. Upstream drivers bind only RiscI2cBusV1 and
 * cannot observe whether this provider delegates or owns I2C0 itself.
 * The firmware retains exclusive controller/pin/Wire ownership for now.
 * Only this privileged, verified ELF can import the private compatibility API.
 */
#include "RiscI2cBusV1.h"
#include "RiscFirmwareI2cCompatV1.h"
#include <stddef.h>
#include <stdint.h>

#define MAX_CLAIMS 12u

typedef struct {
    uint64_t token;
    uint8_t address;
} device_claim;
static device_claim claims[MAX_CLAIMS];
static uint64_t next_token;
static bool started;
static uint32_t in_flight;

static bool is_started(void) {
    return __atomic_load_n(&started, __ATOMIC_ACQUIRE);
}

static uint32_t active_transactions(void) {
    return __atomic_load_n(&in_flight, __ATOMIC_ACQUIRE);
}

static bool claim_device(void *context, uint8_t address, uint64_t *out) {
    (void)context;
    if (out) *out = 0;
    if (!out || !is_started() || address < 0x08u || address > 0x77u ||
        next_token == UINT64_MAX) return false;
    device_claim *empty = NULL;
    for (size_t i = 0; i < MAX_CLAIMS; ++i) {
        if (claims[i].token && claims[i].address == address) return false;
        if (!claims[i].token && !empty) empty = &claims[i];
    }
    if (!empty) return false;
    empty->address = address;
    empty->token = ++next_token;
    *out = empty->token;
    return true;
}

static bool transact(void *context, uint64_t token,
                     const uint8_t *write_bytes, size_t write_length,
                     uint8_t *read_bytes, size_t read_length,
                     uint32_t timeout_ms) {
    (void)context;
    if (!is_started() || !token || (!write_length && !read_length) ||
        write_length > RISC_FW_I2C_COMPAT_V1_MAX_BYTES ||
        read_length > RISC_FW_I2C_COMPAT_V1_MAX_BYTES ||
        (write_length && !write_bytes) || (read_length && !read_bytes) ||
        !timeout_ms || timeout_ms > RISC_FW_I2C_COMPAT_V1_MAX_TIMEOUT_MS)
        return false;

    /*
     * Transactions may arrive from independent provider consumers on separate
     * tasks (for example the 5 ms touch capture worker and power telemetry).
     * The private firmware backend owns the recursive board I2C mutex and
     * serializes the physical Wire transaction. Do not reject normal
     * contention here: an immediate "busy" failure turns a scheduling overlap
     * into a false device error and causes touch gesture resynchronization.
     *
     * Count in-flight calls before reading the claim table. release/quiesce
     * refuse to mutate lifetime state until all calls have drained.
     */
    __atomic_add_fetch(&in_flight, 1u, __ATOMIC_ACQ_REL);
    if (!is_started()) {
        __atomic_sub_fetch(&in_flight, 1u, __ATOMIC_ACQ_REL);
        return false;
    }

    uint8_t address = 0;
    for (size_t i = 0; i < MAX_CLAIMS; ++i) {
        if (claims[i].token == token) {
            address = claims[i].address;
            break;
        }
    }
    if (!address) {
        __atomic_sub_fetch(&in_flight, 1u, __ATOMIC_ACQ_REL);
        return false;
    }

    const bool ok = risc_fw_i2c_transact_v1(address, write_bytes,
                                            write_length, read_bytes,
                                            read_length, timeout_ms);
    __atomic_sub_fetch(&in_flight, 1u, __ATOMIC_ACQ_REL);
    /* A NACK or timed-out transaction is a failure; no fabricated read or
     * success is returned to board.power.vbus or another dependent provider. */
    return ok;
}

static bool release_device(void *context, uint64_t token) {
    (void)context;
    if (!is_started() || active_transactions() || !token) return false;
    for (size_t i = 0; i < MAX_CLAIMS; ++i) {
        if (claims[i].token == token) {
            claims[i].token = 0;
            claims[i].address = 0;
            return true;
        }
    }
    return false;
}

static bool quiesce(void) {
    if (active_transactions()) return false;
    for (size_t i = 0; i < MAX_CLAIMS; ++i)
        if (claims[i].token) return false;
    /* No physical ownership to release. The firmware continues to serve
     * touch/battery/expander transactions after this ELF is unmapped. */
    __atomic_store_n(&started, false, __ATOMIC_RELEASE);
    return true;
}

static bool start(const risc_provider_dependency_v1 *dependencies,
                  size_t dependency_count) {
    (void)dependencies;
    if (dependency_count || is_started() || active_transactions()) return false;
    for (size_t i = 0; i < MAX_CLAIMS; ++i)
        if (claims[i].token) return false;
    /* Private symbol resolution already proves the firmware supports the
     * compatibility transport. Do not call Wire.begin() or install IDF I2C. */
    __atomic_store_n(&started, true, __ATOMIC_RELEASE);
    return true;
}
static void stop(void) { (void)quiesce(); }

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

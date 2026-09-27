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

/*
 * Driver-local lifecycle/transaction state.
 *
 * 0.1.3 used separate zero-initialized atomics in .bss. On hardware that
 * image layout could place the atomic word at an unaligned runtime address,
 * causing Xtensa LoadStoreAlignment as soon as an upstream provider made the
 * first I2C transaction.
 *
 * Keep every mutable word in .data, which is the first runtime data section
 * and therefore starts at the allocator's natural alignment. One 32-bit word
 * coordinates transactions and rare lifecycle/claim-table mutations:
 *
 *   STOPPED                    no API use allowed
 *   STARTED | n                n concurrent transact() calls
 *   STARTED | MUTATING         claim/release table mutation in progress
 *
 * Physical Wire transactions are still serialized by the firmware's
 * BoardT5S3::ScopedI2CLock inside risc_fw_i2c_transact_v1().
 */
#define STATE_STOPPED       0x40000000u
#define STATE_STARTED       0x80000000u
#define STATE_MUTATING      0x20000000u
#define STATE_COUNT_MASK    0x0000ffffu

typedef struct {
    uint64_t token;
    uint8_t address;
} device_claim;

/* Nonzero initializers force these objects into .data rather than .bss. */
static device_claim claims[MAX_CLAIMS]
    __attribute__((section(".data"))) = {{UINT64_MAX, 0u}};
static uint64_t next_token
    __attribute__((section(".data"))) = UINT64_MAX;
static uint32_t state
    __attribute__((section(".data"), aligned(4))) = STATE_STOPPED;

static uint32_t load_state(void) {
    return __atomic_load_n(&state, __ATOMIC_ACQUIRE);
}

static bool begin_mutation(void) {
    uint32_t expected = STATE_STARTED;
    return __atomic_compare_exchange_n(&state, &expected,
                                       STATE_STARTED | STATE_MUTATING,
                                       false, __ATOMIC_ACQ_REL,
                                       __ATOMIC_ACQUIRE);
}

static void end_mutation(void) {
    __atomic_store_n(&state, STATE_STARTED, __ATOMIC_RELEASE);
}

static bool begin_transaction(void) {
    uint32_t current = load_state();
    for (;;) {
        if ((current & (STATE_STARTED | STATE_MUTATING)) != STATE_STARTED)
            return false;
        const uint32_t count = current & STATE_COUNT_MASK;
        if (count == STATE_COUNT_MASK) return false;
        const uint32_t desired = current + 1u;
        if (__atomic_compare_exchange_n(&state, &current, desired, false,
                                        __ATOMIC_ACQ_REL,
                                        __ATOMIC_ACQUIRE))
            return true;
    }
}

static void end_transaction(void) {
    (void)__atomic_sub_fetch(&state, 1u, __ATOMIC_ACQ_REL);
}

static bool claim_device(void *context, uint8_t address, uint64_t *out) {
    (void)context;
    if (out) *out = 0;
    if (!out || address < 0x08u || address > 0x77u ||
        !begin_mutation()) return false;

    bool ok = false;
    device_claim *empty = NULL;
    for (size_t i = 0; i < MAX_CLAIMS; ++i) {
        if (claims[i].token && claims[i].address == address) goto done;
        if (!claims[i].token && !empty) empty = &claims[i];
    }
    if (!empty || next_token == UINT64_MAX) goto done;

    empty->address = address;
    empty->token = ++next_token;
    *out = empty->token;
    ok = true;

done:
    end_mutation();
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
        !timeout_ms || timeout_ms > RISC_FW_I2C_COMPAT_V1_MAX_TIMEOUT_MS ||
        !begin_transaction())
        return false;

    uint8_t address = 0;
    for (size_t i = 0; i < MAX_CLAIMS; ++i) {
        if (claims[i].token == token) {
            address = claims[i].address;
            break;
        }
    }

    bool ok = false;
    if (address) {
        ok = risc_fw_i2c_transact_v1(address, write_bytes, write_length,
                                     read_bytes, read_length, timeout_ms);
    }
    end_transaction();

    /* A NACK or timed-out transaction is a failure; no fabricated read or
     * success is returned to board.power.vbus or another dependent provider. */
    return ok;
}

static bool release_device(void *context, uint64_t token) {
    (void)context;
    if (!token || !begin_mutation()) return false;

    bool ok = false;
    for (size_t i = 0; i < MAX_CLAIMS; ++i) {
        if (claims[i].token == token) {
            claims[i].token = 0;
            claims[i].address = 0;
            ok = true;
            break;
        }
    }

    end_mutation();
    return ok;
}

static bool quiesce(void) {
    if (!begin_mutation()) return false;

    for (size_t i = 0; i < MAX_CLAIMS; ++i) {
        if (claims[i].token) {
            end_mutation();
            return false;
        }
    }

    uint32_t expected = STATE_STARTED | STATE_MUTATING;
    return __atomic_compare_exchange_n(&state, &expected, STATE_STOPPED,
                                       false, __ATOMIC_ACQ_REL,
                                       __ATOMIC_ACQUIRE);
}

static bool start(const risc_provider_dependency_v1 *dependencies,
                  size_t dependency_count) {
    (void)dependencies;
    if (dependency_count) return false;

    uint32_t expected = STATE_STOPPED;
    if (!__atomic_compare_exchange_n(&state, &expected, STATE_MUTATING,
                                     false, __ATOMIC_ACQ_REL,
                                     __ATOMIC_ACQUIRE))
        return false;

    for (size_t i = 0; i < MAX_CLAIMS; ++i) {
        claims[i].token = 0;
        claims[i].address = 0;
    }
    /* UINT64_MAX is only the link-time .data placement sentinel. After the
     * first start, preserve the monotonically increasing token generation
     * across stop/start so stale handles can never become valid again. */
    if (next_token == UINT64_MAX) next_token = 0;
    __atomic_store_n(&state, STATE_STARTED, __ATOMIC_RELEASE);
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

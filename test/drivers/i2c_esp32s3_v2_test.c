#define _POSIX_C_SOURCE 200809L
/* Host fixture for the ACTUAL transitional I2C provider. The firmware's
 * private transport is mocked here; this does not prove Wire bus timing,
 * physical I2C ownership cutover, or hardware operation on a board. */
#include "RiscI2cBusV1.h"
#include "RiscFirmwareI2cCompatV1.h"

#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static const risc_driver_v2 *provider;
static const risc_i2c_bus_api_v1 *bus;

static pthread_mutex_t backend_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t backend_cv = PTHREAD_COND_INITIALIZER;
static unsigned transactions;
static unsigned backend_active;
static unsigned max_backend_active;
static unsigned backend_entries;
static unsigned failures;
static bool block_next_backend;
static bool backend_blocked;
static bool allow_backend_return;
static uint8_t last_address;
static uint32_t last_timeout;
static size_t last_write_length, last_read_length;

static pthread_mutex_t call_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t call_cv = PTHREAD_COND_INITIALIZER;
static unsigned calls_started;
static bool release_started;
static bool release_done;

static void short_delay(void) {
    const struct timespec delay = {.tv_sec = 0, .tv_nsec = 50000000L};
    (void)nanosleep(&delay, NULL);
}

static void arm_backend_block(void) {
    pthread_mutex_lock(&backend_lock);
    block_next_backend = true;
    backend_blocked = false;
    allow_backend_return = false;
    pthread_mutex_unlock(&backend_lock);
}

static void wait_backend_blocked(void) {
    pthread_mutex_lock(&backend_lock);
    while (!backend_blocked)
        pthread_cond_wait(&backend_cv, &backend_lock);
    pthread_mutex_unlock(&backend_lock);
}

static void release_backend_block(void) {
    pthread_mutex_lock(&backend_lock);
    allow_backend_return = true;
    pthread_cond_broadcast(&backend_cv);
    pthread_mutex_unlock(&backend_lock);
}

/* Match the exact private C ABI imported by the provider. The real firmware
 * backend independently serializes physical Wire access with the board I2C
 * mutex. This mock intentionally allows overlap so the test can prove the ELF
 * itself queues callers before they reach the firmware bridge. */
bool risc_fw_i2c_transact_v1(uint8_t address,
                             const uint8_t *write_bytes, size_t write_length,
                             uint8_t *read_bytes, size_t read_length,
                             uint32_t timeout_ms) {
    assert(address == 0x6bu);
    assert(write_length || read_length);
    assert(write_length <= RISC_FW_I2C_COMPAT_V1_MAX_BYTES);
    assert(read_length <= RISC_FW_I2C_COMPAT_V1_MAX_BYTES);
    assert(!write_length || write_bytes);
    assert(!read_length || read_bytes);
    assert(timeout_ms > 0 && timeout_ms <= RISC_FW_I2C_COMPAT_V1_MAX_TIMEOUT_MS);

    pthread_mutex_lock(&backend_lock);
    ++transactions;
    ++backend_entries;
    ++backend_active;
    if (backend_active > max_backend_active) max_backend_active = backend_active;
    last_address = address;
    last_timeout = timeout_ms;
    last_write_length = write_length;
    last_read_length = read_length;

    if (block_next_backend) {
        block_next_backend = false;
        backend_blocked = true;
        pthread_cond_broadcast(&backend_cv);
        while (!allow_backend_return)
            pthread_cond_wait(&backend_cv, &backend_lock);
    }

    bool ok = true;
    if (failures) {
        --failures;
        ok = false;
    }
    pthread_mutex_unlock(&backend_lock);

    if (ok)
        for (size_t i = 0; i < read_length; ++i) read_bytes[i] = 0x42u;

    pthread_mutex_lock(&backend_lock);
    assert(backend_active > 0);
    --backend_active;
    pthread_cond_broadcast(&backend_cv);
    pthread_mutex_unlock(&backend_lock);
    return ok;
}

typedef struct {
    uint64_t claim;
    bool result;
    uint8_t answer;
} transaction_call;

static void *transaction_worker(void *opaque) {
    transaction_call *call = (transaction_call *)opaque;
    uint8_t reg = 3u;
    pthread_mutex_lock(&call_lock);
    ++calls_started;
    pthread_cond_broadcast(&call_cv);
    pthread_mutex_unlock(&call_lock);
    call->result = bus->transact(NULL, call->claim, &reg, 1,
                                 &call->answer, 1, 1000);
    return NULL;
}

typedef struct {
    uint64_t claim;
    bool result;
} release_call;

static void *release_worker(void *opaque) {
    release_call *call = (release_call *)opaque;
    pthread_mutex_lock(&call_lock);
    release_started = true;
    pthread_cond_broadcast(&call_cv);
    pthread_mutex_unlock(&call_lock);
    call->result = bus->release_device(NULL, call->claim);
    pthread_mutex_lock(&call_lock);
    release_done = true;
    pthread_cond_broadcast(&call_cv);
    pthread_mutex_unlock(&call_lock);
    return NULL;
}

int main(void) {
    provider = t5_driver_get(RISC_PROVIDER_DRIVER_ABI_V2);
    assert(provider && !t5_driver_get(1));
    assert(strcmp(provider->driver_id, "i2c-esp32s3-v2") == 0);
    assert(strcmp(provider->capability_id, "i2c.bus") == 0);
    assert(provider->quiesce && provider->capability);
    bus = provider->capability;
    assert(bus->api_version == RISC_I2C_BUS_API_V1);
    assert(bus->struct_size == sizeof(*bus));
    assert(!provider->start(NULL, 1));
    assert(provider->start(NULL, 0));
    assert(!provider->start(NULL, 0));

    uint64_t claim = UINT64_MAX, other = UINT64_MAX;
    assert(!bus->claim_device(NULL, 7u, &claim) && claim == 0);
    assert(!bus->claim_device(NULL, 0x78u, &claim) && claim == 0);
    assert(bus->claim_device(NULL, 0x6bu, &claim) && claim != 0);
    assert(!bus->claim_device(NULL, 0x6bu, &other) && other == 0);
    assert(bus->claim_device(NULL, 0x55u, &other) && other != claim);
    assert(!provider->quiesce());

    uint8_t reg = 3u, answer = 0u, command[2] = {3u, 0x20u};
    assert(!bus->transact(NULL, other + 12u, &reg, 1, &answer, 1, 100));
    assert(!bus->transact(NULL, claim, &reg, 1, &answer, 1, 0));
    assert(!bus->transact(NULL, claim, &reg, 129, &answer, 1, 100));
    assert(!bus->transact(NULL, claim, &reg, 1, &answer, 129, 100));
    assert(!bus->transact(NULL, claim, NULL, 1, &answer, 1, 100));
    assert(!bus->transact(NULL, claim, NULL, 0, NULL, 0, 100));
    assert(!bus->transact(NULL, claim, &reg, 1, &answer, 1, 3001));
    assert(transactions == 0);

    assert(bus->transact(NULL, claim, &reg, 1, &answer, 1, 100));
    assert(answer == 0x42u && last_address == 0x6bu &&
           last_timeout > 0 && last_timeout <= 100 && last_write_length == 1 &&
           last_read_length == 1);
    assert(bus->transact(NULL, claim, command, 2, NULL, 0, 100));
    assert(last_write_length == 2 && last_read_length == 0);
    assert(bus->transact(NULL, claim, NULL, 0, &answer, 1, 100));
    assert(last_write_length == 0 && last_read_length == 1 && answer == 0x42u);
    failures = 1;
    assert(!bus->transact(NULL, claim, &reg, 1, &answer, 1, 100));
    assert(bus->transact(NULL, claim, &reg, 1, &answer, 1, 100));

    /* Two independent tasks must queue at the provider rather than overlap or
     * receive the 0.1.2 synthetic busy failure. */
    pthread_t first_thread, second_thread;
    transaction_call first = {.claim = claim}, second = {.claim = claim};
    calls_started = 0;
    max_backend_active = 0;
    const unsigned before_queue = backend_entries;
    arm_backend_block();
    assert(pthread_create(&first_thread, NULL, transaction_worker, &first) == 0);
    wait_backend_blocked();
    assert(pthread_create(&second_thread, NULL, transaction_worker, &second) == 0);
    pthread_mutex_lock(&call_lock);
    while (calls_started < 2)
        pthread_cond_wait(&call_cv, &call_lock);
    pthread_mutex_unlock(&call_lock);
    short_delay();

    pthread_mutex_lock(&backend_lock);
    assert(backend_entries == before_queue + 1);
    assert(max_backend_active == 1);
    pthread_mutex_unlock(&backend_lock);

    // A short-deadline caller must time out without entering the backend.
    const unsigned before_timeout = backend_entries;
    assert(!bus->transact(NULL, claim, &reg, 1, &answer, 1, 5));
    assert(backend_entries == before_timeout);
    release_backend_block();
    assert(pthread_join(first_thread, NULL) == 0);
    assert(pthread_join(second_thread, NULL) == 0);
    assert(first.result && second.result);
    assert(first.answer == 0x42u && second.answer == 0x42u);
    // The queued caller gets the remaining budget, never a fresh 1000 ms.
    assert(last_timeout > 0 && last_timeout < 1000);
    pthread_mutex_lock(&backend_lock);
    assert(backend_entries == before_queue + 2);
    assert(max_backend_active == 1);
    pthread_mutex_unlock(&backend_lock);

    /* release_device() must wait for an operation on the claim to drain. */
    pthread_t transaction_thread, releasing_thread;
    transaction_call draining = {.claim = claim};
    release_call releasing = {.claim = claim};
    release_started = false;
    release_done = false;
    arm_backend_block();
    assert(pthread_create(&transaction_thread, NULL, transaction_worker, &draining) == 0);
    wait_backend_blocked();
    assert(pthread_create(&releasing_thread, NULL, release_worker, &releasing) == 0);
    pthread_mutex_lock(&call_lock);
    while (!release_started)
        pthread_cond_wait(&call_cv, &call_lock);
    pthread_mutex_unlock(&call_lock);
    short_delay();
    pthread_mutex_lock(&call_lock);
    assert(!release_done);
    pthread_mutex_unlock(&call_lock);

    release_backend_block();
    assert(pthread_join(transaction_thread, NULL) == 0);
    assert(pthread_join(releasing_thread, NULL) == 0);
    assert(draining.result && releasing.result);
    assert(!bus->transact(NULL, claim, &reg, 1, &answer, 1, 100));

    assert(!bus->release_device(NULL, claim + 99u));
    assert(bus->release_device(NULL, other));
    assert(!bus->release_device(NULL, claim));
    assert(provider->quiesce());
    provider->stop();

    /* Token generation remains monotonic across a clean stop/start. */
    assert(provider->start(NULL, 0));
    uint64_t fresh = 0;
    assert(bus->claim_device(NULL, 0x6bu, &fresh) && fresh > claim);
    assert(!bus->transact(NULL, claim, &reg, 1, &answer, 1, 100));
    assert(bus->release_device(NULL, fresh));
    assert(provider->quiesce());
    provider->stop();

    puts("I2C firmware-compatibility provider: mutex queueing, drain, claims, bounds, failure and restart: PASS");
    return 0;
}

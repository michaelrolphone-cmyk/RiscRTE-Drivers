/* Host fixture for the ACTUAL transitional I2C provider. The firmware's
 * private transport is mocked here; this does not prove Wire bus timing,
 * physical I2C ownership cutover, or hardware operation on a board. */
#include "RiscI2cBusV1.h"
#include "RiscFirmwareI2cCompatV1.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const risc_driver_v2 *provider;
static const risc_i2c_bus_api_v1 *bus;
static unsigned transactions, failures;
static uint8_t last_address;
static uint32_t last_timeout;
static size_t last_write_length, last_read_length;
static bool reenter, nested;
static uint64_t primary_claim;

/* Match the exact private C ABI imported by the provider. The real firmware
 * backend serializes physical Wire access with the board I2C mutex, so the
 * provider must allow another caller to queue instead of returning a false
 * "busy" device error. */
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
    ++transactions;
    last_address = address;
    last_timeout = timeout_ms;
    last_write_length = write_length;
    last_read_length = read_length;
    if (reenter && !nested) {
        nested = true;
        uint8_t nested_answer = 0u;
        assert(!provider->quiesce());
        assert(!bus->release_device(NULL, primary_claim));
        assert(bus->transact(NULL, primary_claim, write_bytes, write_length,
                             &nested_answer, 1, timeout_ms));
        assert(nested_answer == 0x42u);
        nested = false;
    }
    if (failures) { --failures; return false; }
    for (size_t i = 0; i < read_length; ++i) read_bytes[i] = 0x42u;
    return true;
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
    primary_claim = claim;
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

    reenter = true;
    assert(bus->transact(NULL, claim, &reg, 1, &answer, 1, 100));
    reenter = false;
    assert(transactions == 2 && answer == 0x42u &&
           last_address == 0x6bu && last_timeout == 100 &&
           last_write_length == 1 && last_read_length == 1);
    assert(bus->transact(NULL, claim, command, 2, NULL, 0, 100));
    assert(last_write_length == 2 && last_read_length == 0);
    assert(bus->transact(NULL, claim, NULL, 0, &answer, 1, 100));
    assert(last_write_length == 0 && last_read_length == 1 && answer == 0x42u);
    failures = 1;
    assert(!bus->transact(NULL, claim, &reg, 1, &answer, 1, 100));
    assert(bus->transact(NULL, claim, &reg, 1, &answer, 1, 100));
    assert(transactions == 6);

    assert(!bus->release_device(NULL, claim + 99u));
    assert(bus->release_device(NULL, other));
    assert(bus->release_device(NULL, claim));
    assert(!bus->release_device(NULL, claim));
    assert(provider->quiesce());
    assert(!bus->transact(NULL, claim, &reg, 1, &answer, 1, 100));
    provider->stop();
    assert(provider->start(NULL, 0));
    uint64_t fresh = 0;
    assert(bus->claim_device(NULL, 0x6bu, &fresh) && fresh > claim);
    assert(!bus->transact(NULL, claim, &reg, 1, &answer, 1, 100));
    assert(bus->release_device(NULL, fresh));
    assert(provider->quiesce());
    provider->stop();
    puts("I2C firmware-compatibility provider: queued contention, claims, bounds, failure and quiescence: PASS");
    return 0;
}

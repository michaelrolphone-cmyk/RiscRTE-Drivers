#include "RiscUsbVbusV1.h"
#include "RiscBq25896ProfileV1.h"
extern const risc_driver_v2 *t5_profile_get(uint32_t abi);
#include "RiscI2cBusV1.h"
#include "RiscPlatformClockV1.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t registers[0x20];
    uint64_t claim;
    unsigned claims, writes, releases, reads_after_batfet, fault_reads;
    uint8_t fail_read;
    int fail_write, fail_once, batfet_attempted;
} fake_chip;
static fake_chip chip;
static bool claim_device(void *ctx, uint8_t address, uint64_t *out) {
    fake_chip *c = ctx;
    if (address != 0x6bu || !out || c->claim) return false;
    c->claim = ++c->claims;
    *out = c->claim;
    return true;
}
static bool transact(void *ctx, uint64_t token, const uint8_t *wr, size_t wn,
                     uint8_t *rd, size_t rn, uint32_t timeout) {
    fake_chip *c = ctx;
    if (!token || c->claim != token || !wr || wn == 0u ||
        wr[0] >= sizeof(c->registers) || timeout != 100u) return false;
    if (wn == 1u && rn == 1u && rd) {
        if (c->batfet_attempted) ++c->reads_after_batfet;
        if (wr[0] == c->fail_read) return false;
        if (wr[0] == 0x0cu) ++c->fault_reads;
        *rd = c->registers[wr[0]];
        return true;
    }
    if (wn == 2u && rn == 0u && !rd) {
        ++c->writes;
        c->registers[wr[0]] = wr[1];
        if (wr[0] == 0x09u && (wr[1] & 0x20u)) c->batfet_attempted = 1;
        if (c->fail_once && c->fail_write == wr[0]) {
            c->fail_once = 0;
            return false; /* NACK AFTER the PMIC accepted the write. */
        }
        return true;
    }
    return false;
}
static bool release_device(void *ctx, uint64_t token) {
    fake_chip *c = ctx;
    if (!token || token != c->claim) return false;
    c->claim = 0;
    ++c->releases;
    return true;
}
static uint64_t monotonic_ms(void *ctx) { (void)ctx; return 5u; }
static void sleep_ms(void *ctx, uint32_t ms) { (void)ctx; (void)ms; }
int main(int argc, char **argv) {
    const int fail_batfet = argc > 1 && strcmp(argv[1], "batfet-nack") == 0;
    risc_i2c_bus_api_v1 bus = {RISC_I2C_BUS_API_V1,
        sizeof(risc_i2c_bus_api_v1), &chip,
        claim_device, transact, release_device};
    risc_platform_clock_api_v1 clock = {RISC_PLATFORM_CLOCK_API_V1,
        sizeof(risc_platform_clock_api_v1), NULL, monotonic_ms, sleep_ms};
    const risc_provider_dependency_v1 deps[] = {
        {"i2c.bus", RISC_I2C_BUS_API_V1, &bus},
        {"platform.clock", RISC_PLATFORM_CLOCK_API_V1, &clock},
        {RISC_BQ25896_PROFILE_CAPABILITY, 1, t5_profile_get(2)->capability}
    };
    const risc_driver_v2 *driver = t5_driver_get(RISC_PROVIDER_DRIVER_ABI_V2);
    assert(driver && driver->capability);
    const risc_usb_vbus_charger_api_v1 *power =
        (const risc_usb_vbus_charger_api_v1 *)driver->capability;
    assert(power->base.struct_size >= sizeof(*power) &&
           power->request_shutdown && power->configure_charger);
    chip.fail_read = 0xffu;
    chip.registers[0x03] = 0x10u;
    chip.registers[0x14] = 0x01u; /* Known BQ25896 PN, revision 1. */
    assert(driver->start(deps, 3u) && chip.claims == 1u);

    chip.registers[0x0b] = 0x04u; /* PG external power veto. */
    assert(!power->request_shutdown(NULL) && !chip.writes);
    chip.registers[0x0b] = 0u;
    chip.registers[0x11] = 0x80u; /* REG11 input-good veto independently. */
    assert(!power->request_shutdown(NULL) && !chip.writes);
    chip.registers[0x11] = 0u;
    chip.registers[0x03] |= 0x20u; /* Even if status is stale, never shut down OTG. */
    assert(!power->request_shutdown(NULL) && !chip.writes);
    chip.registers[0x03] &= (uint8_t)~0x20u;
    chip.fail_read = 0x09u;
    assert(!power->request_shutdown(NULL) && !chip.writes);
    chip.fail_read = 0xffu;

    if (!fail_batfet) {
        chip.fail_write = 0x03;
        chip.fail_once = 1;
        assert(!power->request_shutdown(NULL));
        assert(chip.claim && !driver->quiesce());
        uint64_t token = 9;
        assert(!power->base.acquire_host(NULL, 500u, &token) && token == 0);
        assert(power->configure_charger(NULL)); /* Explicit charge-state recovery. */
        assert(chip.claims == 1u && !chip.batfet_attempted);
    } else {
        chip.fail_write = 0x09;
        chip.fail_once = 1;
    }

    const unsigned before = chip.writes;
    assert(power->request_shutdown(NULL) == !fail_batfet);
    assert(chip.batfet_attempted && chip.claims == 1u && chip.claim);
    assert(chip.registers[0x03] == (uint8_t)(chip.registers[0x03] & (uint8_t)~0x10u));
    assert(chip.registers[0x09] & 0x20u);
    assert(chip.writes >= before + 2u);
    assert(chip.reads_after_batfet == 0u && chip.fault_reads == 0u);
    assert(!power->request_shutdown(NULL) && !power->configure_charger(NULL));
    risc_bq25896_charger_snapshot_v1 snapshot = {0};
    assert(!power->read_charger(NULL, &snapshot));
    assert(!driver->quiesce() && !chip.releases);
    uint64_t host_token = 1;
    assert(!power->base.acquire_host(NULL, 500u, &host_token) && host_token == 0u);
    assert(!chip.reads_after_batfet);
    /* No stop/unmap: BATFET_DIS may have already removed I2C power. */
    puts(fail_batfet ? "BQ shutdown BATFET NACK: ownership pinned PASS" :
                        "BQ shutdown command ACK: external/OTG veto, charge recovery, one-way latch PASS");
    return 0;
}

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
    uint8_t regs[0x20];
    uint64_t claim, next_claim, now;
    unsigned writes, releases, bad_claims, fault_reads;
    unsigned fail_power_write, fail_adc_write, fail_release, fail_probe;
    unsigned adc_ready_ms, fault_on_boost;
    unsigned late_fault_ms, late_fault_injected;
    uint8_t fault_latched;
    int external, no_boost, clock_failed, reject_500ma;
} simulated_board;
static bool claim_device(void *ctx, uint8_t address, uint64_t *out) {
    simulated_board *b = ctx;
    if (address != 0x6b || b->claim || !out) return false;
    b->claim = ++b->next_claim;
    *out = b->claim;
    return true;
}
static bool transact(void *ctx, uint64_t id, const uint8_t *wr, size_t nwr,
                     uint8_t *rd, size_t nrd, uint32_t timeout) {
    simulated_board *b = ctx;
    if (id != b->claim || !id) { ++b->bad_claims; return false; }
    if (!wr || !nwr || wr[0] >= sizeof(b->regs) || timeout != 100) return false;
    if (nwr == 1 && nrd == 1 && rd) {
        if (b->fail_probe) return false;
        uint8_t reg = wr[0];
        if (reg == 0x0b) {
            if (b->regs[3] & 0x20) *rd = b->no_boost ? 0 : 0xe0;
            else *rd = b->external ? 0x24 : 0;
        } else if (reg == 0x11) {
            if (b->regs[3] & 0x20)
                /* TI BQ25896: VBUS_GD=0 in OTG; ADC data is not immediate. */
                *rd = (b->no_boost || !(b->regs[2] & 0x40) ||
                       b->now < b->adc_ready_ms) ? 0 : 25;
            else *rd = b->external ? 0x80 : 0;
        } else if (reg == 0x0c) {
            /* First REG0C read returns history; next read returns live fault. */
            if (b->late_fault_ms && b->now >= b->late_fault_ms &&
                !b->late_fault_injected) {
                b->fault_latched = 0x40;
                b->late_fault_injected = 1;
            }
            ++b->fault_reads;
            *rd = b->fault_latched ? b->fault_latched : b->regs[0x0c];
            b->fault_latched = 0;
        } else *rd = b->regs[reg];
        return true;
    }
    if (nwr == 2 && nrd == 0 && !rd) {
        ++b->writes;
        if ((wr[0] == 3 && b->fail_power_write) ||
            (wr[0] == 2 && b->fail_adc_write)) {
            if (wr[0] == 3) --b->fail_power_write;
            else --b->fail_adc_write;
            /* NACK after data reached the PMIC: rollback is mandatory. */
            b->regs[wr[0]] = wr[1];
            return false;
        }
        b->regs[wr[0]] = wr[1];
        if (wr[0] == 3 && (wr[1] & 0x20) &&
            (b->fault_on_boost ||
             (b->reject_500ma && (b->regs[0x0a] & 0x07) == 0))) {
            b->fault_latched = 0x40;
            b->regs[0x0c] = b->fault_on_boost == 2 ? 0x40 : 0;
        }
        return true;
    }
    return false;
}
static bool release_device(void *ctx, uint64_t id) {
    simulated_board *b = ctx;
    if (id != b->claim || !id) return false;
    if (b->fail_release) { --b->fail_release; return false; }
    b->claim = 0;
    ++b->releases;
    return true;
}
static uint64_t monotonic_ms(void *ctx) {
    simulated_board *b = ctx;
    return b->clock_failed ? UINT64_MAX : b->now;
}
static void sleep_ms(void *ctx, uint32_t ms) { ((simulated_board *)ctx)->now += ms; }
static simulated_board board;
static risc_i2c_bus_api_v1 i2c = {
    RISC_I2C_BUS_API_V1, sizeof(risc_i2c_bus_api_v1),
    &board, claim_device, transact, release_device
};
static risc_platform_clock_api_v1 clock_api = {
    RISC_PLATFORM_CLOCK_API_V1, sizeof(risc_platform_clock_api_v1),
    &board, monotonic_ms, sleep_ms
};
static risc_bq25896_profile_api_v1 profile;
static const risc_bq25896_profile_api_v1 *t5_profile;
static const risc_provider_dependency_v1 deps[] = {
    {"i2c.bus", RISC_I2C_BUS_API_V1, &i2c},
    {"platform.clock", RISC_PLATFORM_CLOCK_API_V1, &clock_api},
    {RISC_BQ25896_PROFILE_CAPABILITY, RISC_BQ25896_PROFILE_API_V1, &profile}
};
static const risc_driver_v2 *driver;
static const risc_usb_vbus_api_v1 *power;
static void reset_board(void) {
    assert(board.claim == 0);
    memset(&board, 0, sizeof(board));
    board.regs[2] = 0x15; /* ADC initially disabled; retain unrelated settings. */
    board.regs[3] = 0x10;
    board.regs[0x0a] = 0x32;
    profile = *t5_profile;
    assert(driver->start(deps, 3));
    assert(board.claim != 0);
}
static void shutdown_board(void) {
    assert(driver->quiesce());
    assert(board.claim == 0);
    driver->stop();
}
static void assert_restored(void) {
    assert(board.regs[2] == 0x15);
    assert(board.regs[3] == 0x10);
    assert(board.regs[0x0a] == 0x32);
}
int main(void) {
    const risc_driver_v2 *profile_driver = t5_profile_get(RISC_PROVIDER_DRIVER_ABI_V2);
    assert(profile_driver && profile_driver->start(NULL, 0));
    t5_profile = profile_driver->capability;
    driver = t5_driver_get(RISC_PROVIDER_DRIVER_ABI_V2);
    assert(driver && !t5_driver_get(1));
    assert(strcmp(driver->driver_id, "board-power-t5s3-v2") == 0);
    assert(strcmp(driver->capability_id, "board.power.vbus") == 0);
    power = (const risc_usb_vbus_api_v1 *)driver->capability;
    assert(power && power->api_version == 1 && power->acquire_host &&
           power->release_host && power->quiesce && driver->quiesce);
    assert(!driver->start(deps, 1));
    assert(!driver->start(deps, 2)); /* No implicit T5S3 profile. */

    /* The actual chip-owner extension distinguishes incoming power from
     * our own source, survives repeated park/reacquire without releasing
     * I2C, and never treats an I2C error as permission to start the host. */
    const risc_usb_vbus_monitor_api_v1 *monitor = driver->capability;
    assert(power->struct_size >= sizeof(*monitor) && monitor->input_status);
    assert(monitor->flags & RISC_USB_POWER_IDLE_PROBE_REQUIRED);
    reset_board();
    assert(monitor->input_status(NULL) == RISC_USB_POWER_SETTLING);
    board.now += 500;
    assert(monitor->input_status(NULL) == RISC_USB_POWER_ABSENT);
    board.external = 1;
    assert(monitor->input_status(NULL) == RISC_USB_POWER_EXTERNAL);
    assert(board.writes == 0);
    board.external = 0;
    board.fail_probe = 1;
    assert(monitor->input_status(NULL) == RISC_USB_POWER_UNKNOWN);
    board.fail_probe = 0;
    board.regs[3] = 0x20; /* unowned OTG is not a clean battery-only state */
    assert(monitor->input_status(NULL) == RISC_USB_POWER_UNKNOWN);
    board.regs[3] = 0x10;
    for (unsigned i = 0; i < 3; ++i) {
        uint64_t source = 0;
        assert(power->acquire_host(NULL, 500, &source));
        assert(monitor->input_status(NULL) == RISC_USB_POWER_SOURCE);
        board.external = 1;
        assert(power->release_host(NULL, source));
        assert(board.claim && !board.releases);
        assert_restored(); /* OTG off, charge enabled */
        assert(monitor->input_status(NULL) == RISC_USB_POWER_SETTLING);
        board.now += 500;
        assert(monitor->input_status(NULL) == RISC_USB_POWER_EXTERNAL);
        board.external = 0;
    }
    shutdown_board();
    assert(monitor->input_status(NULL) == RISC_USB_POWER_UNKNOWN);

    reset_board();
    uint64_t token = UINT64_MAX;
    assert(!power->acquire_host(NULL, 501, &token) && token == 0);
    assert(!power->acquire_host(NULL, 0, &token) && token == 0);
    assert(board.writes == 0);
    assert(power->acquire_host(NULL, 500, &token) && token != 0);
    assert(board.now >= 80);             /* v1.2.16 settles for 80 ms. */
    assert(board.fault_reads >= 4);      /* preflight and OTG each read twice */
    assert(board.regs[3] == 0x20);       /* charger off, OTG on */
    assert(board.regs[0x0a] == 0x92);    /* v1.2.16: 5.126 V, 1.2 A peak limit */
    assert(board.regs[2] == 0x55);      /* ADC turned on without losing settings */
    assert(!power->quiesce(NULL) && !driver->quiesce());
    assert(!power->acquire_host(NULL, 500, &(uint64_t){0}));
    assert(!power->release_host(NULL, token + 1));
    board.fail_power_write = 1;
    assert(!power->release_host(NULL, token));
    assert(!driver->quiesce() && board.claim);
    assert(power->release_host(NULL, token)); /* retry, not soft reset */
    assert_restored();
    assert(!power->release_host(NULL, token));
    board.fail_release = 1;
    assert(!driver->quiesce() && board.claim);
    shutdown_board();
    assert(board.releases == 1 && !board.bad_claims);

    /* Simulate hardware known to reject 500mA at OTG start; the historical
     * 1.2A boost threshold must avoid that fault without masking REG0C. */
    reset_board();
    board.reject_500ma = 1;
    assert(power->acquire_host(NULL, 500, &token));
    assert(board.regs[0x0a] == 0x92);
    assert(power->release_host(NULL, token));
    assert_restored();
    shutdown_board();

    reset_board();
    board.clock_failed = 1;
    token = 9;
    assert(!power->acquire_host(NULL, 500, &token) && token == 0);
    assert(board.writes == 0);  /* Unavailable clock must not energize VBUS. */
    board.clock_failed = 0;
    shutdown_board();

    reset_board();
    assert(power->acquire_host(NULL, 500, &token));
    board.clock_failed = 1;
    assert(!power->release_host(NULL, token));
    assert(!driver->quiesce() && board.claim);
    assert(!power->acquire_host(NULL, 500, &(uint64_t){0}));
    board.clock_failed = 0;
    assert(power->release_host(NULL, token));
    assert_restored();
    shutdown_board();

    reset_board();
    board.external = 1;
    token = 9;
    assert(!power->acquire_host(NULL, 500, &token) && token == 0);
    assert(board.writes == 0);
    assert(board.fault_reads == 0); /* conflict check precedes fault clearing */
    shutdown_board();

    reset_board();
    board.fail_power_write = 1;
    token = 8;
    assert(!power->acquire_host(NULL, 500, &token) && token == 0);
    assert_restored();
    shutdown_board();

    reset_board();
    board.fail_adc_write = 1;
    assert(!power->acquire_host(NULL, 500, &token) && token == 0);
    assert_restored();
    shutdown_board();

    reset_board();
    board.no_boost = 1;
    token = 8;
    assert(!power->acquire_host(NULL, 500, &token) && token == 0);
    assert(board.now >= 1500);
    assert_restored();
    shutdown_board();

    reset_board();
    board.adc_ready_ms = 1000; /* Real ADC can take up to 1s to complete. */
    assert(power->acquire_host(NULL, 500, &token));
    assert(board.now >= 1000 && board.now < 1500);
    assert(power->release_host(NULL, token));
    assert_restored();
    shutdown_board();

    reset_board();
    board.adc_ready_ms = 2000; /* Invalid ADC still fails closed on timeout. */
    assert(!power->acquire_host(NULL, 500, &token));
    assert(board.now >= 1500);
    assert_restored();
    shutdown_board();

    reset_board();
    board.fault_latched = 0x40; /* Old, resolved fault must not block OTG. */
    assert(power->acquire_host(NULL, 500, &token));
    assert(board.fault_reads >= 4);
    assert(power->release_host(NULL, token));
    assert_restored();
    shutdown_board();

    reset_board();
    board.regs[0x0c] = 0x40; /* Actual current fault: no OTG writes allowed. */
    assert(!power->acquire_host(NULL, 500, &token));
    assert(board.writes == 0);
    assert_restored();
    shutdown_board();

    reset_board();
    board.fault_on_boost = 1; /* One resolved inrush: qualify stable ADC. */
    board.adc_ready_ms = 1000;
    assert(power->acquire_host(NULL, 500, &token));
    assert(board.now >= 1000 && board.now < 1500);
    assert(board.fault_reads >= 4);
    assert(power->release_host(NULL, token));
    assert_restored();
    shutdown_board();

    reset_board();
    board.fault_on_boost = 1; /* A SECOND transient in same startup is unsafe. */
    board.late_fault_ms = 200;
    assert(!power->acquire_host(NULL, 500, &token));
    assert(board.late_fault_injected);
    assert(board.now < 1500);
    assert_restored();
    shutdown_board();

    reset_board();
    board.fault_on_boost = 1; /* Cleared fault without OTG regulation is unsafe. */
    board.no_boost = 1;
    assert(!power->acquire_host(NULL, 500, &token));
    assert_restored();
    shutdown_board();

    reset_board();
    board.fault_on_boost = 1; /* Cleared fault with no valid ADC still times out. */
    board.adc_ready_ms = 2000;
    assert(!power->acquire_host(NULL, 500, &token));
    assert(board.now >= 1500);
    assert_restored();
    shutdown_board();

    reset_board();
    board.fault_on_boost = 2; /* New persistent fault: fail, restore. */
    assert(!power->acquire_host(NULL, 500, &token));
    assert_restored();
    shutdown_board();

    /* Reuse the SAME compiled driver with a different installed profile.
     * Synthetic electrical values are not a claim about another real board. */
    memset(&board, 0, sizeof(board));
    board.regs[3] = 0x10;
    profile = *t5_profile;
    profile.max_host_milliamps = 100;
    profile.boost_millivolts = 4998;
    profile.boost_limit_milliamps = 750;
    profile.boost_settle_ms = 120;
    profile.input_settle_ms = 900;
    profile.transient_window_ms = 0;
    profile.transient_stable_ms = 0;
    assert(driver->start(deps, 3));
    /* Copy once at admission; changing provider memory is not live policy. */
    profile.boost_limit_milliamps = 2150;
    board.now = 899;
    assert(monitor->input_status(NULL) == RISC_USB_POWER_SETTLING);
    ++board.now;
    assert(monitor->input_status(NULL) == RISC_USB_POWER_ABSENT);
    assert(!power->acquire_host(NULL, 101, &token) && !board.writes);
    const uint64_t begun = board.now;
    assert(power->acquire_host(NULL, 100, &token));
    assert(board.regs[0x0a] == 0x71 && board.now >= begun + 120);
    assert(power->release_host(NULL, token));
    board.fault_on_boost = 1;
    assert(!power->acquire_host(NULL, 100, &token)); /* Recovery not enabled. */
    assert(board.regs[3] == 0x10 && board.regs[0x0a] == 0);
    shutdown_board();

    /* Malformed/incompatible electrical data must fail before claiming I2C. */
    for (unsigned invalid = 0; invalid < 10; ++invalid) {
        profile = *t5_profile;
        switch (invalid) {
        case 0: profile.struct_size = 8; break;
        case 1: profile.api_version = 2; break;
        case 2: profile.max_host_milliamps = 1201; break;
        case 3: profile.boost_millivolts = 5000; break; /* not an exact step */
        case 4: profile.boost_limit_milliamps = 1000; break;
        case 5: profile.boost_settle_ms = 0; break;
        case 6: profile.input_settle_ms = 219; break;
        case 7: profile.input_settle_ms = UINT32_MAX; break;
        case 8: profile.transient_window_ms = 0; break;
        case 9: profile.transient_stable_ms = 1; break;
        }
        memset(&board, 0, sizeof(board));
        assert(!driver->start(deps, 3));
        assert(!board.next_claim && !board.writes);
        assert(driver->quiesce()); driver->stop();
    }
    profile = *t5_profile;
    board.regs[0x14] = 0x18; /* Different chip at the same address. */
    assert(!driver->start(deps, 3));
    assert(board.claim && !board.writes);
    shutdown_board();
    assert(profile_driver->quiesce()); profile_driver->stop();

    puts("BQ25896 reuse/profile validation plus T5S3 VBUS: v1.2.16 1.2A boost, qualified transient recovery, repeat/live faults, ADC, conflicts, rollback, timeout, retry: PASS");
    return 0;
}

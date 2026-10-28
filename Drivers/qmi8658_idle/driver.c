/* QMI8658 no-motion profile owner. QST QMI8658C Rev A, Table22/31,
 * Fig16 and sections7.3/13.4. Only CTRL7[1:0] and CTRL1[0] are owned.
 * No reset, rails, ODR, gyro configuration, FIFO, IRQ or CTRL9 writes. */
#include "DriverSupport.h"
#include "RiscPowerSleepGuardV1.h"
#include "ProviderLog.h"
static const risc_i2c_bus_api_v1 *bus;
static const risc_platform_clock_api_v1 *clock_api;
static const tw_hw_i2c_device_v1 *config;
static uint64_t session;
static bool session_valid, active, dirty, busy;
static uint8_t original_one, original_seven;
/* Slowest documented valid ODR is 3Hz; t6 <= 2/ODR. The conservative
 * ceil(2000/3) bound avoids assuming inherited 125Hz or a 31ms delay. */
#define QMI_STOP_MS 667u
static bool fail(const char *reason) { provider_log(reason); return false; }
static bool take(void) {
    if (session || !bus || !config) return false;
    bool ok = bus->claim_device(bus->context, config->address, &session);
    session_valid = ok && session;
    return session_valid;
}
static bool release(void) {
    if (!session) return true;
    if (!bus || !bus->release_device(bus->context, session)) return false;
    session = 0; session_valid = false; return true;
}
static bool read_one(uint8_t reg, uint8_t *out) {
    return session_valid && bus->transact(bus->context, session, &reg, 1, out, 1, 20);
}
static bool update(uint8_t reg, uint8_t mask, uint8_t value) {
    uint8_t before = 0, after = 0;
    if (!read_one(reg, &before)) return false;
    uint8_t wanted = (uint8_t)((before & (uint8_t)~mask) | (value & mask));
    uint8_t tx[2] = {reg, wanted};
    if (wanted != before && !bus->transact(bus->context, session, tx, 2, NULL, 0, 20)) return false;
    return read_one(reg, &after) && (after & mask) == (wanted & mask);
}
static bool wait_ms(uint32_t ms) {
    uint64_t before = clock_api->monotonic_ms(clock_api->context);
    if (before == UINT64_MAX) return false;
    clock_api->sleep_ms(clock_api->context, ms);
    uint64_t after = clock_api->monotonic_ms(clock_api->context);
    return after != UINT64_MAX && after >= before && after - before >= ms;
}
static bool sensors_off(void) {
    uint8_t enabled = 0;
    return read_one(8, &enabled) && update(8, 3, 0) &&
        (!(enabled & 3) || wait_ms(QMI_STOP_MS));
}
static bool restore(void) {
    /* Retrying cleanup may find that an earlier enable write acted despite
     * returning false. Disable and settle again before oscillator changes. */
    if (!sensors_off() || !update(2, 1, original_one)) return false;
    /* t0 max15ms on leaving power-down; t7 <100us at400kHz rounded to1ms.
     * This wait is also repeated after an uncertain earlier oscillator write. */
    if (!wait_ms((original_one & 1) ? 1u : 15u)) return false;
    return update(8, 3, original_seven);
}
static bool ready(void *context) {
    (void)context;
    return active && !session && !busy;
}
static bool quiesce(void) {
    if (busy) return false;
    busy = true; active = false;
    if (session && !session_valid && !release()) goto retained;
    if (dirty) {
        if (!session && !take()) goto retained;
        if (!restore()) goto retained;
    }
    if (!release()) goto retained;
    dirty = false;
    bus = NULL; clock_api = NULL; config = NULL;
    busy = false; return true;
retained:
    busy = false;
    return fail("QMI_IDLE reason=cleanup_retained");
}
static void stop(void) { (void)quiesce(); }
static bool start(const risc_provider_dependency_v1 *deps, size_t count) {
    if (busy || bus || clock_api || config || session || active || dirty) return false;
    const tw_hw_i2c_device_v1 *c = tw_config(deps, count, "qst,qmi8658-idle", "peripheral.i2c", sizeof(*c));
    const risc_i2c_bus_api_v1 *b = tw_dep(deps, count, "i2c.bus", sizeof(*b));
    const risc_platform_clock_api_v1 *k = tw_dep(deps, count, "platform.clock", sizeof(*k));
    if (count != 3 || !tw_i2c_config_valid(c) || c->struct_size != sizeof(*c) ||
        c->bus.frequency_hz != 400000 || (c->address != 0x6a && c->address != 0x6b) ||
        c->chip_id != 5 || c->irq != -1 || c->irq_active_high || c->irq_pull_up ||
        !tw_i2c_valid(b) || !tw_clock_valid(k)) return fail("QMI_IDLE reason=admission");
    bus = b; clock_api = k; config = c; busy = true;
    if (!take()) { busy = false; return fail("QMI_IDLE reason=claim"); }
    const char *reason = "QMI_IDLE reason=read";
    uint8_t id = 0, one = 0, accel = 0, gyro = 0, seven = 0, features = 0, command = 0, fifo = 0;
    if (!read_one(0, &id) || !read_one(2, &one) || !read_one(3, &accel) ||
        !read_one(4, &gyro) || !read_one(8, &seven) || !read_one(9, &features) ||
        !read_one(10, &command) || !read_one(20, &fifo)) goto failed;
    reason = "QMI_IDLE reason=identity"; if (id != 5) goto failed;
    original_one = one; original_seven = seven;
    /* Table31 preserves arbitrary configuration in exact power-down. Accept
     * this already established state without normalizing unrelated modes. */
    if ((one & 1) && !(seven & 3)) goto verified_idle;
    reason = "QMI_IDLE reason=unsupported_mode";
    /* Preserve unowned controls, never normalize self-test, locking, FIFO,
     * activity/command or contradictory power states. Low-power accel may
     * carry hidden WoM configuration requiring a separate CTRL9 exit. */
    if ((one & 2) || (seven & 0xcc) || (features & 0x3f) || command ||
        (fifo & 0xf3) || (accel & 0x80) || (gyro & 0x80) ||
        ((one & 1) && (seven & 3))) goto failed;
    reason = "QMI_IDLE reason=unsupported_odr";
    if ((seven & 1) && ((accel & 0x70) > 0x30 || (accel & 0x0f) > 8 ||
        (!(seven & 2) && (accel & 0x0f) < 3))) goto failed;
    if ((seven & 2) && ((gyro & 0x70) == 0x70 || (gyro & 0x0f) > 8)) goto failed;
    dirty = true; /* Complete original state precedes the first possible write. */
    reason = "QMI_IDLE reason=disable_or_timing";
    if (!sensors_off() || !update(2, 1, 1) || !wait_ms(1)) goto failed;
    /* Validate both owned fields after the oscillator transition. */
verified_idle:
    reason = "QMI_IDLE reason=idle_readback";
    if (!read_one(8, &seven) || (seven & 3) || !read_one(2, &one) || !(one & 1)) goto failed;
    if (!release()) { busy = false; return fail("QMI_IDLE reason=release_retained"); }
    active = true; busy = false;
    provider_log("QMI_IDLE ready=1 state=power_down hardware_current=unmeasured");
    return true;
failed:
    /* Keep an authorized claim on uncertain writes until checked graph
     * cleanup restores it. Pre-write refusal only has session custody. */
    if (!dirty && !release()) reason = "QMI_IDLE reason=release_retained";
    busy = false; return fail(reason);
}
static const risc_power_sleep_guard_v1 api = {1, sizeof(api), NULL, ready};
static const risc_driver_v2 driver = {2, sizeof(driver), "qmi8658-idle",
    RISC_POWER_SLEEP_GUARD_CAPABILITY, 1, &api, start, stop, quiesce};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) { return abi == 2 ? &driver : NULL; }

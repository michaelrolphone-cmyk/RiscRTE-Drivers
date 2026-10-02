/* CW2017 at 0x63. Reads SoC and VCELL only. Does not write the 80-byte BATINFO
 * profile: a wrong table would recalibrate the gauge. GPIO21 is charge status. */
#include "RiscBatteryGaugeV1.h"
#include "RiscGpioBankV1.h"
#include "RiscI2cBusV1.h"
#include "x4pro_pins.h"
#include "x4pro_proto.h"
#include <stddef.h>

static const risc_gpio_bank_api_v1 *gpio_api;
static const risc_i2c_bus_api_v1 *bus;
static uint64_t charge, bus_claim;
static bool started;
static bool equal(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static bool read_reg(uint8_t reg, uint8_t *out, size_t length) {
    return bus->transact(bus->context, bus_claim, &reg, 1, out, length, 30);
}
static bool read_sample(void *context, risc_battery_sample_v1 *out) {
    (void)context;
    if (!started || !out) return false;
    uint8_t soc = 0, cell[2] = {0}, version = 0;
    *out = (risc_battery_sample_v1){0, 255, 0};
    if (!read_reg(0x00, &version, 1) || !read_reg(0x04, &soc, 1) || !read_reg(0x02, cell, 2))
        return false;
    out->millivolts = x4pro_cw2017_millivolts(cell[0], cell[1]);
    out->percent = soc <= 100u ? soc : 255u;
    if (version == 0) out->flags |= RISC_BATTERY_PROFILE_MISSING;
    bool charging = false;
    if (gpio_api->read(gpio_api->context, charge, &charging) && charging)
        out->flags |= RISC_BATTERY_CHARGING;
    return true;
}
static bool quiesce(void) {
    bool ok = true;
    if (bus_claim && bus && !bus->release_device(bus->context, bus_claim)) ok = false;
    if (charge && gpio_api && !gpio_api->release(gpio_api->context, charge)) ok = false;
    bus_claim = charge = 0;
    bus = NULL;
    gpio_api = NULL;
    started = false;
    return ok;
}
static bool start(const risc_provider_dependency_v1 *deps, size_t count) {
    if (started || !deps || count != 2u) return false;
    for (size_t i = 0; i < count; ++i) {
        if (equal(deps[i].capability_id, RISC_GPIO_BANK_CAPABILITY)) gpio_api = deps[i].api;
        else if (equal(deps[i].capability_id, "i2c.bus")) bus = deps[i].api;
    }
    if (!gpio_api || !bus || !bus->claim_device) return false;
    if (!gpio_api->claim(gpio_api->context, X4PRO_PIN_CHG_STAT, RISC_GPIO_INPUT, &charge) ||
        !bus->claim_device(bus->context, X4PRO_I2C_CW2017, &bus_claim)) {
        (void)quiesce();
        return false;
    }
    started = true;
    return true;
}
static void stop(void) { (void)quiesce(); }
static const risc_battery_gauge_api_v1 api = {
    RISC_BATTERY_GAUGE_API_V1, sizeof(api), NULL, read_sample
};
static const risc_driver_v2 driver = {
    RISC_PROVIDER_DRIVER_ABI_V2, sizeof(driver),
    "x4pro-battery", RISC_BATTERY_GAUGE_CAPABILITY, RISC_BATTERY_GAUGE_API_V1,
    &api, start, stop, quiesce
};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &driver : NULL;
}

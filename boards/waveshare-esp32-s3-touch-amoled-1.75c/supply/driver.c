/* Passive AXP2101 DCDC1 observer for the CPU-shared VCC3V3 supply.
 * X-Powers AXP2101 datasheet sections 6.13.2.69/71: enable 0x80 bit0;
 * 0x82 bits4:0 encode 1500 + 100*code mV, codes0..19 only.
 * No call writes PMIC register data, changes a rail, or powers off the CPU.
 */
#include "DriverSupport.h"
#include "RiscPowerRailV1.h"
#include "RiscI2cFunctionV1.h"

static const risc_i2c_bus_api_v1 *bus;
static const risc_hw_i2c_function_v1 *function;
static uint64_t session;
static bool active, closing;

static bool release_session(void) {
    if (!session) return true;
    if (!bus || !bus->release_device(bus->context, session)) return false;
    session = 0;
    return true;
}

static bool read8(uint8_t reg, uint8_t *value) {
    return bus->transact(bus->context, session, &reg, 1, value, 1, 20);
}

static bool get_info(void *context, risc_power_rail_info_v1 *out) {
    (void)context;
    if (!out || out->struct_size < sizeof(*out)) return false;
    *out = (risc_power_rail_info_v1){.struct_size = sizeof(*out)};
    if (!active || closing || session) return false;
    bool claimed = bus->claim_device(bus->context, function->address, &session);
    if (!claimed || !session) {
        /* A false/nonzero claim is cleanup custody, never a live I/O lease. */
        closing = true;
        return false;
    }
    uint8_t id = 0, enabled = 0, voltage = 0;
    bool ok = read8(0x03, &id) && id == 0x4a &&
              read8(0x80, &enabled) && read8(0x82, &voltage);
    const uint8_t code = voltage & 0x1f;
    ok = ok && code <= 19;
    bool released = release_session();
    if (!ok || !released) {
        closing = true;
        return false;
    }
    out->enabled = (enabled & 1) != 0;
    out->configured_millivolts = 1500u + 100u * code;
    out->configuration_verified = true; /* Readback, not measured load voltage. */
    return true;
}

static bool set_enabled(void *context, bool enabled) {
    (void)context; (void)enabled;
    return false; /* This selected observer has no supply-changing authority. */
}

static bool quiesce(void) {
    closing = true;
    return release_session();
}

static void stop(void) {
    if (!quiesce()) return;
    active = closing = false;
    bus = NULL;
    function = NULL;
}

static bool start(const risc_provider_dependency_v1 *deps, size_t count) {
    if (bus || session || active || closing) return false;
    const risc_hw_i2c_function_v1 *selected = tw_config(
        deps, count, "x-powers,axp2101-dcdc1", "peripheral.i2c-function", sizeof(*selected));
    if (!selected || !selected->parent_instance_id || selected->function_id != 1 ||
        selected->address != 0x34 || selected->chip_id != 0x4a || selected->reserved ||
        !tw_bus_limit(&selected->bus, RISC_HW_BUS_I2C, 400000)) return false;
    const risc_i2c_bus_api_v1 *selected_bus = tw_dep(deps, count, "i2c.bus", sizeof(*selected_bus));
    if (!tw_i2c_valid(selected_bus)) return false;
    function = selected;
    bus = selected_bus;
    active = true;
    risc_power_rail_info_v1 info = {.struct_size = sizeof(info)};
    return get_info(NULL, &info);
}

static const risc_power_rail_api_v1 api = {
    1, sizeof(api), NULL, get_info, set_enabled
};
static const risc_driver_v2 descriptor = {
    2, sizeof(descriptor), "axp2101-dcdc1-observer", RISC_POWER_RAIL_CAPABILITY, 1,
    &api, start, stop, quiesce
};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) { return abi == 2 ? &descriptor : NULL; }

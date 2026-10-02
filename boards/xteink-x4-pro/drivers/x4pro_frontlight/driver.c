/* Cool GPIO8 / warm GPIO9, active high. Proportional 25 kHz LEDC is the stock
 * path; this ELF drives the pads directly so it does not depend on an unclaimed
 * LEDC clock. Any non-zero channel is on. */
#include "RiscFrontlightV1.h"
#include "RiscGpioBankV1.h"
#include "x4pro_pins.h"
#include <stddef.h>

static const risc_gpio_bank_api_v1 *gpio_api;
static uint64_t cool, warm;
static bool started;
static bool equal(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static bool set_levels(void *context, uint16_t cool_level, uint16_t warm_level, uint16_t maximum) {
    (void)context;
    if (!started || !maximum || cool_level > maximum || warm_level > maximum) return false;
    return gpio_api->write(gpio_api->context, cool, cool_level != 0) &&
           gpio_api->write(gpio_api->context, warm, warm_level != 0);
}
static bool off(void *context) { return set_levels(context, 0, 0, 1); }
static bool quiesce(void) {
    bool ok = true;
    if (cool && gpio_api && !gpio_api->release(gpio_api->context, cool)) ok = false;
    if (warm && gpio_api && !gpio_api->release(gpio_api->context, warm)) ok = false;
    cool = warm = 0;
    gpio_api = NULL;
    started = false;
    return ok;
}
static bool start(const risc_provider_dependency_v1 *deps, size_t count) {
    if (started || !deps || count != 1u || !equal(deps[0].capability_id, RISC_GPIO_BANK_CAPABILITY))
        return false;
    gpio_api = deps[0].api;
    if (!gpio_api || !gpio_api->claim) return false;
    if (!gpio_api->claim(gpio_api->context, X4PRO_PIN_LIGHT_COOL, RISC_GPIO_OUTPUT, &cool) ||
        !gpio_api->claim(gpio_api->context, X4PRO_PIN_LIGHT_WARM, RISC_GPIO_OUTPUT, &warm) ||
        !off(NULL)) {
        (void)quiesce();
        return false;
    }
    started = true;
    return true;
}
static void stop(void) { (void)quiesce(); }
static const risc_frontlight_api_v1 api = {
    RISC_FRONTLIGHT_API_V1, sizeof(api), NULL, set_levels, off
};
static const risc_driver_v2 driver = {
    RISC_PROVIDER_DRIVER_ABI_V2, sizeof(driver),
    "x4pro-frontlight", RISC_FRONTLIGHT_CAPABILITY, RISC_FRONTLIGHT_API_V1,
    &api, start, stop, quiesce
};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &driver : NULL;
}

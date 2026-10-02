/* Digital buttons confirmed on the X4 Pro: GPIO0 left, GPIO7 right, GPIO3 power.
 * There is no live ADC ladder on this board. */
#include "RiscGpioBankV1.h"
#include "RiscInputNavigationV1.h"
#include "x4pro_pins.h"
#include <stddef.h>

static const risc_gpio_bank_api_v1 *gpio_api;
static uint64_t left, right, power;
static uint32_t previous;
static bool started, armed;

static bool equal(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static bool pressed(uint64_t claim) {
    bool level = true;
    if (!gpio_api->read(gpio_api->context, claim, &level)) return false;
    return !level;
}
static bool poll(void *context, risc_input_navigation_frame_v1 *out) {
    (void)context;
    if (!started || !out) return false;
    uint32_t now = 0;
    if (pressed(left)) now |= RISC_NAV_LEFT | RISC_NAV_UP | RISC_NAV_PAGE_BACK;
    if (pressed(right)) now |= RISC_NAV_RIGHT | RISC_NAV_DOWN | RISC_NAV_PAGE_FORWARD;
    if (pressed(power)) now |= RISC_NAV_HOME;
    if (!armed) {
        if (!now) armed = true;
        now = 0;
    }
    out->buttons = now;
    out->pressed = now & ~previous;
    out->released = previous & ~now;
    previous = now;
    return true;
}
static bool foreground(void *context, const risc_input_foreground_v1 *claims, size_t count) {
    (void)context; (void)claims;
    return count == 0;
}
static bool reset(void *context) {
    (void)context;
    previous = 0;
    armed = false;
    return true;
}
static bool quiesce(void) {
    bool ok = true;
    const uint64_t pins[] = {left, right, power};
    for (size_t i = 0; i < 3; ++i)
        if (pins[i] && gpio_api && !gpio_api->release(gpio_api->context, pins[i])) ok = false;
    left = right = power = 0;
    gpio_api = NULL;
    started = false;
    return ok;
}
static bool start(const risc_provider_dependency_v1 *deps, size_t count) {
    if (started || !deps || count != 1u || !equal(deps[0].capability_id, RISC_GPIO_BANK_CAPABILITY))
        return false;
    gpio_api = deps[0].api;
    if (!gpio_api || !gpio_api->claim || !gpio_api->read) return false;
    const uint32_t flags = RISC_GPIO_INPUT | RISC_GPIO_PULLUP;
    if (!gpio_api->claim(gpio_api->context, X4PRO_PIN_BTN_LEFT, flags, &left) ||
        !gpio_api->claim(gpio_api->context, X4PRO_PIN_BTN_RIGHT, flags, &right) ||
        !gpio_api->claim(gpio_api->context, X4PRO_PIN_BTN_POWER, flags, &power)) {
        (void)quiesce();
        return false;
    }
    started = true;
    armed = false;
    return true;
}
static void stop(void) { (void)quiesce(); }
static const risc_input_navigation_api_v1 api = {
    RISC_INPUT_NAVIGATION_API_V1, sizeof(api), NULL, poll, foreground, reset
};
static const risc_driver_v2 driver = {
    RISC_PROVIDER_DRIVER_ABI_V2, sizeof(driver),
    "x4pro-buttons", "input.navigation", RISC_INPUT_NAVIGATION_API_V1,
    &api, start, stop, quiesce
};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &driver : NULL;
}

#include "RiscUsbControllerV1.h"
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

/* Exercise the production usb.host ELF, not an independent policy copy. */
static const uint8_t config_bytes[] = {
    9, 2, 32, 0, 1, 1, 0, 0x80, 50,
    9, 4, 0, 0, 2, 0xff, 0, 0, 0,
    7, 5, 0x81, 2, 64, 0, 0,
    7, 5, 0x02, 2, 64, 0, 0
};
static bool emitted, fail_events, controller_idle = true, release_ok = true;
static int forwarded, claims, releases, physical_quiesces;

static int32_t next_event(void *ctx, risc_usb_controller_event_v1 *out) {
    (void)ctx;
    if (fail_events) return -1;
    if (emitted) return 0;
    emitted = true;
    *out = (risc_usb_controller_event_v1){1, 55};
    return 1;
}
static bool configuration(void *ctx, uint64_t physical, uint8_t *bytes,
                          size_t *size, uint16_t *vid, uint16_t *pid) {
    (void)ctx;
    assert(physical == 55);
    if (!bytes || !size || *size < sizeof(config_bytes)) return false;
    memcpy(bytes, config_bytes, sizeof(config_bytes));
    *size = sizeof(config_bytes);
    *vid = 0x10c4;
    *pid = 0xea60;
    return true;
}
static bool claim(void *ctx, uint64_t physical, uint8_t iface,
                  uint8_t alt, uint64_t *out) {
    (void)ctx;
    assert(physical == 55 && iface == 0 && alt == 0 && out);
    ++claims;
    *out = 700;
    return true;
}
static bool release_claim(void *ctx, uint64_t physical_claim) {
    (void)ctx;
    assert(physical_claim == 700);
    ++releases;
    return release_ok;
}
static int32_t control(void *ctx, uint64_t physical, uint8_t type,
                       uint8_t request, uint16_t value, uint16_t index,
                       uint8_t *data, uint16_t length, uint32_t timeout) {
    (void)ctx; (void)value; (void)data;
    assert(physical == 55 && index == 0 && !length && timeout == 1000);
    assert((type == 0x40 && request == 0x5f) ||
           (type == 0x21 && request == 0x22));
    ++forwarded;
    return 0;
}
static int32_t bulk_read(void *ctx, uint64_t token, uint8_t endpoint,
                         uint8_t *dst, size_t capacity, uint32_t timeout) {
    (void)ctx; (void)token; (void)endpoint; (void)dst;
    (void)capacity; (void)timeout;
    return -1;
}
static int32_t bulk_write(void *ctx, uint64_t token, uint8_t endpoint,
                          const uint8_t *src, size_t length, uint32_t timeout) {
    (void)ctx; (void)token; (void)endpoint; (void)src;
    (void)length; (void)timeout;
    return -1;
}
static bool quiesce_controller(void *ctx) {
    (void)ctx;
    ++physical_quiesces;
    return controller_idle;
}

int main(int argc, char **argv) {
    assert(argc == 2);
    void *elf = dlopen(argv[1], RTLD_NOW);
    assert(elf);
    risc_driver_get_v2_fn get = (risc_driver_get_v2_fn)dlsym(elf, "t5_driver_get");
    assert(get);
    const risc_driver_v2 *driver = get(RISC_PROVIDER_DRIVER_ABI_V2);
    assert(driver && strcmp(driver->driver_id, "usb-host-v2") == 0);
    const risc_usb_host_api_v1 *host = (const risc_usb_host_api_v1 *)driver->capability;
    assert(host && host->struct_size >= sizeof(risc_usb_host_discovery_v1));
    const risc_usb_host_discovery_v1 *discovery =
        (const risc_usb_host_discovery_v1 *)host;
    assert(discovery->release_checked && discovery->poll && discovery->devices &&
           discovery->control_claim);
    risc_usb_controller_api_v1 controller = {
        RISC_USB_CONTROLLER_API_V1, sizeof(controller), NULL,
        next_event, configuration, claim, release_claim, control,
        bulk_read, bulk_write, quiesce_controller
    };
    risc_provider_dependency_v1 dep = {"usb.controller", 1, &controller};
    assert(driver->start(&dep, 1));
    size_t processed = 0;
    assert(discovery->poll(host->context, 8, &processed) && processed == 1);
    uint64_t tokens[RISC_USB_HOST_MAX_DEVICES] = {0};
    size_t count = RISC_USB_HOST_MAX_DEVICES;
    assert(discovery->devices(host->context, tokens, &count) && count == 1);
    const uint64_t device = tokens[0];
    assert(device && device != 55);

    /* The legacy device-token prefix preserves deployed protocol behavior;
     * this fixture verifies the new exact-claim suffix separately. */
    assert(discovery->control_claim(host->context, device, 0x21, 0x22,
                                    0, 0, NULL, 0, 1000) == -1);
    assert(!forwarded);
    uint64_t claim_token = 0;
    assert(host->claim(host->context, device, 0, 0, &claim_token) && claim_token);
    assert(discovery->control_claim(host->context, claim_token, 0x40, 0x5f,
                                    0, 0, NULL, 0, 1000) == 0);
    assert(discovery->control_claim(host->context, claim_token, 0x21, 0x22,
                                    0, 0, NULL, 0, 1000) == 0);
    assert(discovery->control_claim(host->context, claim_token, 0x21, 0x22,
                                    0, 1, NULL, 0, 1000) == -1);
    assert(discovery->control_claim(host->context, claim_token, 0x22, 0x22,
                                    0, 0, NULL, 0, 1000) == -1);
    assert(forwarded == 2 && claims == 1);

    /* An event overflow/fault revokes new operations but NOT known claims.
     * Physical quiescence is independently required before resetting state. */
    fail_events = true;
    assert(!discovery->poll(host->context, 8, &processed));
    assert(discovery->control_claim(host->context, claim_token, 0x40, 0x5f,
                                    0, 0, NULL, 0, 1000) == -1);
    count = RISC_USB_HOST_MAX_DEVICES;
    assert(!discovery->devices(host->context, tokens, &count));
    assert(!driver->quiesce()); /* The consumer still owns its claim. */
    driver->stop();            /* Must not silently clear that claim. */
    release_ok = false;
    assert(!discovery->release_checked(host->context, claim_token));
    assert(!driver->quiesce());
    release_ok = true;
    assert(discovery->release_checked(host->context, claim_token));
    controller_idle = false;
    assert(!driver->quiesce());
    driver->stop(); /* Retain the fault until callbacks/DMA actually quiesce. */
    controller_idle = true;
    assert(driver->quiesce() && physical_quiesces >= 2);
    driver->stop();
    fail_events = false;
    assert(driver->start(&dep, 1));
    count = RISC_USB_HOST_MAX_DEVICES;
    assert(discovery->devices(host->context, tokens, &count) && count == 0);
    assert(discovery->control_claim(host->context, claim_token, 0x40, 0x5f,
                                    0, 0, NULL, 0, 1000) == -1);
    assert(driver->quiesce());
    driver->stop();
    assert(dlclose(elf) == 0);
    puts("USB host claim-scoped controls and verified fault cleanup: PASS");
    return 0;
}

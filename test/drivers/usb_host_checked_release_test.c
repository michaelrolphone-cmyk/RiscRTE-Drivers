#include "RiscUsbControllerV1.h"
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

static const uint8_t config[] = {
    9, 2, 32, 0, 1, 1, 0, 0x80, 50,
    9, 4, 0, 0, 2, 0xff, 0, 0, 0,
    7, 5, 0x81, 2, 64, 0, 0,
    7, 5, 0x02, 2, 64, 0, 0
};
static risc_usb_controller_event_v1 event;
static bool pending_event, reject_release;
static unsigned physical_claims, physical_releases;
static uint64_t next_claim = 100;
static int32_t next_event(void *ctx, risc_usb_controller_event_v1 *out) {
    (void)ctx;
    if (!pending_event) return 0;
    *out = event;
    pending_event = false;
    return 1;
}
static bool configuration(void *ctx, uint64_t device, uint8_t *bytes,
                          size_t *size, uint16_t *vid, uint16_t *pid) {
    (void)ctx;
    if (device != 51 || !bytes || !size || *size < sizeof(config)) return false;
    memcpy(bytes, config, sizeof(config));
    *size = sizeof(config);
    *vid = 0x1234;
    *pid = 0x9876;
    return true;
}
static bool claim(void *ctx, uint64_t device, uint8_t iface,
                  uint8_t alt, uint64_t *out) {
    (void)ctx;
    if (device != 51 || iface != 0 || alt != 0 || !out) return false;
    ++physical_claims;
    *out = ++next_claim;
    return true;
}
static bool release(void *ctx, uint64_t claim) {
    (void)ctx;
    assert(claim > 100);
    ++physical_releases;
    return !reject_release;
}
static int32_t control(void *ctx, uint64_t device, uint8_t type,
                       uint8_t req, uint16_t value, uint16_t index,
                       uint8_t *data, uint16_t len, uint32_t timeout) {
    (void)ctx; (void)device; (void)type; (void)req; (void)value;
    (void)index; (void)data; (void)len; (void)timeout;
    return 0;
}
static int32_t read_data(void *ctx, uint64_t claim, uint8_t endpoint,
                         uint8_t *dst, size_t cap, uint32_t timeout) {
    (void)ctx; (void)claim; (void)endpoint; (void)dst; (void)cap; (void)timeout;
    return 0;
}
static int32_t write_data(void *ctx, uint64_t claim, uint8_t endpoint,
                          const uint8_t *src, size_t len, uint32_t timeout) {
    (void)ctx; (void)claim; (void)endpoint; (void)src; (void)timeout;
    return (int32_t)len;
}
static bool quiesce_controller(void *ctx) {(void)ctx; return true;}
static void queue(uint32_t kind) {
    assert(!pending_event);
    event = (risc_usb_controller_event_v1){kind, 51};
    pending_event = true;
}
int main(int argc, char **argv) {
    assert(argc == 2);
    void *elf = dlopen(argv[1], RTLD_NOW);
    assert(elf);
    risc_driver_get_v2_fn get = (risc_driver_get_v2_fn)dlsym(elf, "t5_driver_get");
    assert(get);
    const risc_driver_v2 *driver = get(RISC_PROVIDER_DRIVER_ABI_V2);
    assert(driver);
    const risc_usb_host_discovery_v1 *host =
        (const risc_usb_host_discovery_v1 *)driver->capability;
    assert(host && host->host.struct_size >= sizeof(*host) &&
           host->poll && host->devices && host->release_checked);
    risc_usb_controller_api_v1 controller = {
        RISC_USB_CONTROLLER_API_V1, sizeof(controller), 0,
        next_event, configuration, claim, release, control,
        read_data, write_data, quiesce_controller
    };
    risc_provider_dependency_v1 dep = {"usb.controller", 1, &controller};
    assert(driver->start(&dep, 1));
    queue(1);
    size_t processed = 0;
    assert(host->poll(host->host.context, 8, &processed) && processed == 1);
    uint64_t devices[8] = {0}; size_t count = 8;
    assert(host->devices(host->host.context, devices, &count) && count == 1);
    uint64_t token = 0, stale = 0;
    assert(host->host.claim(host->host.context, devices[0], 0, 0, &token) && token);
    assert(!driver->quiesce());
    reject_release = true;
    assert(!host->release_checked(host->host.context, token));
    assert(!host->host.claim(host->host.context, devices[0], 0, 0, &stale));
    uint8_t byte = 0;
    assert(host->host.bulk_read(host->host.context, token, 0x81, &byte, 1, 5) < 0);
    assert(!driver->quiesce());
    reject_release = false;
    assert(host->release_checked(host->host.context, token));
    assert(!host->release_checked(host->host.context, token));
    assert(physical_claims == 1 && physical_releases == 3);
    assert(host->host.claim(host->host.context, devices[0], 0, 0, &stale) && stale != token);
    reject_release = true;
    assert(!host->release_checked(host->host.context, stale));
    queue(2);
    assert(host->poll(host->host.context, 8, &processed) && processed == 1);
    assert(!host->host.claim(host->host.context, devices[0], 0, 0, &token));
    reject_release = false;
    /* Graph cleanup may physically finish, but cannot swallow the checked
     * caller's terminal receipt while it still retains queues/dependencies. */
    assert(!driver->quiesce());
    unsigned releases_before_ack = physical_releases;
    assert(!driver->quiesce());
    assert(physical_releases == releases_before_ack);
    assert(host->release_checked(host->host.context, stale));
    assert(physical_releases == releases_before_ack);
    assert(driver->quiesce());
    driver->stop();
    assert(dlclose(elf) == 0);
    puts("USB host checked release, quarantine, retry and detach: PASS");
    return 0;
}

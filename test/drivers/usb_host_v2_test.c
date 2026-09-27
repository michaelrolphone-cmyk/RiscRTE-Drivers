#include "RiscUsbInterruptV1.h"
#include "RiscUsbDiscoveryDiagnosticsV1.h"
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

static const uint8_t config[] = {
    9,2,38,0,2,1,0,0x80,50,
    9,4,0,0,2,0xff,0,0,0,
    7,5,0x81,2,64,0,0,
    7,5,0x02,2,64,0,0,
    6,4,1,0,0,0xff
};
static risc_usb_controller_event_v1 events[8];
static size_t event_head, event_tail;
static int physical_claims, physical_releases, bulk_reads, bulk_writes, controls;
static bool reject_release, idle = true;
static uint64_t next_physical = 600;
static void queue(uint32_t kind, uint64_t token) {
    assert(event_tail < 8);
    events[event_tail++] = (risc_usb_controller_event_v1){kind, token};
}
static int32_t next_event(void *ctx, risc_usb_controller_event_v1 *out) {
    (void)ctx;
    if (event_head == event_tail) return 0;
    *out = events[event_head++]; return 1;
}
static bool configuration(void *ctx, uint64_t physical, uint8_t *out,
                          size_t *len, uint16_t *vid, uint16_t *pid) {
    (void)ctx;
    if (!physical || !out || !len || *len < sizeof(config)) return false;
    memcpy(out, config, sizeof(config));
    *len = sizeof(config); *vid = 0x1234; *pid = 0x9876;
    return true;
}
static bool claim(void *ctx, uint64_t physical, uint8_t iface,
                  uint8_t alt, uint64_t *token) {
    (void)ctx;
    assert(physical && iface == 0 && alt == 0 && token);
    ++physical_claims; *token = ++next_physical; return true;
}
static bool release_claim(void *ctx, uint64_t token) {
    (void)ctx;
    assert(token > 600);
    ++physical_releases;
    return !reject_release;
}
static int32_t control(void *ctx, uint64_t physical, uint8_t type,
                       uint8_t request, uint16_t value, uint16_t index,
                       uint8_t *data, uint16_t len, uint32_t timeout) {
    (void)ctx; (void)value; (void)data;
    assert(physical && type == 0x21 && request == 0x22 &&
           index == 0 && len == 0 && timeout == 1000);
    ++controls; return 0;
}
static int32_t bulk_read(void *ctx, uint64_t claim_token, uint8_t endpoint,
                         uint8_t *data, size_t len, uint32_t timeout) {
    (void)ctx;
    assert(claim_token > 600 && endpoint == 0x81 && data && len && timeout);
    data[0] = 'R'; ++bulk_reads; return 1;
}
static int32_t bulk_write(void *ctx, uint64_t claim_token, uint8_t endpoint,
                          const uint8_t *data, size_t len, uint32_t timeout) {
    (void)ctx;
    assert(claim_token > 600 && endpoint == 0x02 && data && len && timeout);
    ++bulk_writes; return (int32_t)len;
}
static int32_t interrupt_read(void *ctx, uint64_t claim_token, uint8_t endpoint,
                              uint8_t *data, size_t len, uint32_t timeout) {
    (void)ctx; (void)claim_token; (void)endpoint; (void)data;
    (void)len; (void)timeout;
    return -1; /* no interrupt endpoint in this bulk-only descriptor */
}
static bool quiesce_controller(void *ctx) { (void)ctx; return idle; }
static bool controller_diagnostic(void *ctx, char *out, size_t capacity) {
    assert(ctx == &events);
    snprintf(out, capacity, "ATTACHED; ENUM FAIL: Root port reset failed");
    return true;
}

int main(int argc, char **argv) {
    assert(argc == 2);
    void *elf = dlopen(argv[1], RTLD_NOW);
    assert(elf);
    risc_driver_get_v2_fn get = (risc_driver_get_v2_fn)dlsym(elf, "t5_driver_get");
    assert(get && !get(1));
    const risc_driver_v2 *driver = get(2);
    assert(driver && strcmp(driver->driver_id, "usb-host-v2") == 0 &&
           strcmp(driver->capability_id, "usb.host") == 0 && driver->quiesce);
    const risc_usb_host_api_v1 *host = (const risc_usb_host_api_v1 *)driver->capability;
    assert(host && host->api_version == 1 &&
           host->struct_size >= sizeof(risc_usb_host_interrupt_v1));
    const risc_usb_host_interrupt_v1 *extended =
        (const risc_usb_host_interrupt_v1 *)host;
    assert(extended->interrupt_read);
    const risc_usb_host_discovery_v1 *discovery =
        (const risc_usb_host_discovery_v1 *)host;
    assert(!driver->start(NULL, 0));
    risc_usb_controller_interrupt_v1 controller = {
        {RISC_USB_CONTROLLER_API_V1, sizeof(controller), NULL,
         next_event, configuration, claim, release_claim, control,
         bulk_read, bulk_write, quiesce_controller},
        interrupt_read
    };
    risc_provider_dependency_v1 dependency = {"usb.controller", 1, &controller.controller};
    assert(driver->start(&dependency, 1));
    assert(!driver->start(&dependency, 1));
    assert(host->struct_size >= sizeof(risc_usb_host_diagnostics_v1));
    const risc_usb_host_diagnostics_v1 *diagnostics = (const risc_usb_host_diagnostics_v1 *)host;
    char reason[80] = {0};
    // Old controller allocation really ends at the interrupt extension.
    assert(!diagnostics->diagnostic(host->context, reason, sizeof(reason)));
    size_t n = 8;
    uint64_t devices[8] = {0};
    assert(discovery->devices(host->context, devices, &n) && n == 0);
    queue(1, 51);
    size_t processed = 0;
    assert(discovery->poll(host->context, 8, &processed) && processed == 1);
    n = 0;
    assert(!discovery->devices(host->context, devices, &n) && n == 1);
    n = 8;
    assert(discovery->devices(host->context, devices, &n) && n == 1);
    uint64_t first = devices[0], claim_token = 0;
    assert(first && first != 51);
    uint8_t desc[64]; size_t length = sizeof(desc);
    uint16_t vid = 0, pid = 0;
    assert(host->configuration(host->context, first, desc, &length, &vid, &pid));
    assert(length == sizeof(config) && vid == 0x1234 && pid == 0x9876);
    assert(!host->claim(host->context, first, 1, 0, &claim_token));
    assert(host->claim(host->context, first, 0, 0, &claim_token) && claim_token);
    uint64_t duplicate = 0;
    assert(!host->claim(host->context, first, 0, 0, &duplicate));
    assert(!driver->quiesce());
    assert(host->control(host->context, first, 0x21, 0x22, 3, 1, NULL, 0, 1000) == -1);
    assert(host->control(host->context, first, 0x21, 0x22, 3, 0, NULL, 0, 1000) == 0);
    uint8_t data[2] = {0};
    assert(host->bulk_read(host->context, claim_token, 0x81, data, 2, 100) == 1);
    assert(data[0] == 'R');
    assert(host->bulk_write(host->context, claim_token, 0x02, data, 2, 100) == 2);
    assert(host->bulk_read(host->context, claim_token, 0x82, data, 2, 100) == -1);
    assert(host->bulk_write(host->context, claim_token, 0x81, data, 2, 100) == -1);
    assert(extended->interrupt_read(host->context, claim_token, 0x81, data, 2, 10) == -1);
    assert(physical_claims == 1 && controls == 1 && bulk_reads == 1 && bulk_writes == 1);
    queue(2, 51);
    assert(discovery->poll(host->context, 8, &processed) && processed == 1);
    assert(host->bulk_read(host->context, claim_token, 0x81, data, 2, 100) == -1);
    length = sizeof(desc);
    assert(!host->configuration(host->context, first, desc, &length, &vid, &pid));
    assert(!driver->quiesce());
    reject_release = true;
    host->release(host->context, claim_token);
    assert(physical_releases == 1 && !driver->quiesce());
    reject_release = false;
    idle = false;
    assert(!driver->quiesce() && physical_releases == 3);
    idle = true;
    assert(driver->quiesce());
    driver->stop();
    assert(driver->start(&dependency, 1));
    queue(1, 52);
    assert(discovery->poll(host->context, 8, &processed) && processed == 1);
    n = 8;
    assert(discovery->devices(host->context, devices, &n) && n == 1 &&
           devices[0] != first);
    assert(!host->claim(host->context, first, 0, 0, &claim_token));
    assert(driver->quiesce());
    driver->stop();
    assert(!diagnostics->diagnostic(host->context, reason, sizeof(reason)));
    risc_usb_controller_diagnostics_v1 diagnostic_controller = {controller, controller_diagnostic};
    diagnostic_controller.base.controller.struct_size = sizeof(diagnostic_controller);
    diagnostic_controller.base.controller.context = &events;
    dependency.api = &diagnostic_controller.base.controller;
    assert(driver->start(&dependency, 1));
    assert(diagnostics->diagnostic(host->context, reason, sizeof(reason)));
    assert(!strcmp(reason, "ATTACHED; ENUM FAIL: Root port reset failed"));
    assert(!diagnostics->diagnostic(host->context, NULL, sizeof(reason)));
    assert(!diagnostics->diagnostic(host->context, reason, 0));
    diagnostic_controller.diagnostic = NULL;
    assert(!diagnostics->diagnostic(host->context, reason, sizeof(reason)));
    assert(driver->quiesce());
    driver->stop();
    assert(dlclose(elf) == 0);
    puts("USB host ELF discovery/generation, bulk and interrupt claim gates: PASS");
    return 0;
}

#include "RiscUsbControllerV1.h"
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

/* Execute the production host ELF against a simulated controller. A failed
 * descriptor request must not be interpreted as physical detach; a failed
 * event stream MUST NOT be interpreted as an empty, healthy inventory. */
static const uint8_t config[] = {
    9, 2, 32, 0, 1, 1, 0, 0x80, 50,
    9, 4, 0, 0, 2, 0xff, 0, 0, 0,
    7, 5, 0x81, 2, 64, 0, 0,
    7, 5, 0x02, 2, 64, 0, 0
};
static risc_usb_controller_event_v1 pending;
static bool event_ready, fail_descriptor, idle = true;
static unsigned polls, configurations;
static void queue(uint32_t kind, uint64_t physical) {
    assert(!event_ready);
    pending = (risc_usb_controller_event_v1){kind, physical};
    event_ready = true;
}
static int32_t next_event(void *context, risc_usb_controller_event_v1 *event) {
    (void)context;
    ++polls;
    if (!event_ready) return 0;
    *event = pending;
    event_ready = false;
    return 1;
}
static bool configuration(void *context, uint64_t device, uint8_t *out,
                          size_t *length, uint16_t *vid, uint16_t *pid) {
    (void)context;
    assert(device && out && length && vid && pid);
    ++configurations;
    if (fail_descriptor || *length < sizeof(config)) return false;
    memcpy(out, config, sizeof(config));
    *length = sizeof(config);
    *vid = 0x1a86;
    *pid = 0x7523;
    return true;
}
static bool claim(void *context, uint64_t device, uint8_t iface, uint8_t alt,
                  uint64_t *token) {
    (void)context; (void)device; (void)iface; (void)alt; (void)token;
    return false;
}
static bool release(void *context, uint64_t token) {
    (void)context; (void)token;
    return false;
}
static int32_t control(void *context, uint64_t device, uint8_t type,
                       uint8_t request, uint16_t value, uint16_t index,
                       uint8_t *payload, uint16_t length, uint32_t timeout) {
    (void)context; (void)device; (void)type; (void)request; (void)value;
    (void)index; (void)payload; (void)length; (void)timeout;
    return -1;
}
static int32_t read_data(void *context, uint64_t token, uint8_t endpoint,
                         uint8_t *bytes, size_t length, uint32_t timeout) {
    (void)context; (void)token; (void)endpoint; (void)bytes;
    (void)length; (void)timeout;
    return -1;
}
static int32_t write_data(void *context, uint64_t token, uint8_t endpoint,
                          const uint8_t *bytes, size_t length, uint32_t timeout) {
    (void)context; (void)token; (void)endpoint; (void)bytes;
    (void)length; (void)timeout;
    return -1;
}
static bool quiesce(void *context) { (void)context; return idle; }

int main(int argc, char **argv) {
    assert(argc == 2);
    void *elf = dlopen(argv[1], RTLD_NOW);
    assert(elf);
    risc_driver_get_v2_fn get = (risc_driver_get_v2_fn)dlsym(elf, "t5_driver_get");
    assert(get);
    const risc_driver_v2 *driver = get(RISC_PROVIDER_DRIVER_ABI_V2);
    assert(driver && strcmp(driver->driver_id, "usb-host-v2") == 0);
    const risc_usb_host_snapshot_v1 *api =
        (const risc_usb_host_snapshot_v1 *)driver->capability;
    assert(api && api->discovery.host.api_version == RISC_USB_HOST_API_V1 &&
           api->discovery.host.struct_size >= sizeof(*api) && api->snapshot &&
           api->discovery.poll && api->discovery.devices);
    risc_usb_controller_api_v1 controller = {
        RISC_USB_CONTROLLER_API_V1, sizeof(controller), NULL,
        next_event, configuration, claim, release, control, read_data,
        write_data, quiesce
    };
    risc_provider_dependency_v1 dep = {"usb.controller", 1, &controller};
    assert(driver->start(&dep, 1));
    risc_usb_device_identity_v1 result[RISC_USB_HOST_MAX_DEVICES] = {{0}};
    size_t count = RISC_USB_HOST_MAX_DEVICES;
    assert(api->snapshot(api->discovery.host.context, result, &count) && count == 0);
    assert(!api->snapshot(api->discovery.host.context, result, NULL));

    queue(1, 51);
    count = 0;
    result[0].token = UINT64_MAX;
    assert(!api->snapshot(api->discovery.host.context, result, &count) && count == 1);
    assert(result[0].token == UINT64_MAX); /* No partial records. */
    count = RISC_USB_HOST_MAX_DEVICES;
    assert(api->snapshot(api->discovery.host.context, result, &count) && count == 1);
    const uint64_t first = result[0].token;
    assert(first && first != 51 && result[0].identified &&
           result[0].vid == 0x1a86 && result[0].pid == 0x7523);
    const unsigned before = configurations;
    fail_descriptor = true;
    count = RISC_USB_HOST_MAX_DEVICES;
    assert(api->snapshot(api->discovery.host.context, result, &count) && count == 1 &&
           result[0].token == first && !result[0].identified &&
           result[0].vid == 0 && result[0].pid == 0 && configurations == before + 1);
    fail_descriptor = false;
    count = RISC_USB_HOST_MAX_DEVICES;
    assert(api->snapshot(api->discovery.host.context, result, &count) && count == 1 &&
           result[0].token == first && result[0].identified);

    queue(2, 51);
    count = RISC_USB_HOST_MAX_DEVICES;
    assert(api->snapshot(api->discovery.host.context, result, &count) && count == 0);
    queue(1, 51); /* Reusing an address gets a NEW generation token. */
    count = RISC_USB_HOST_MAX_DEVICES;
    assert(api->snapshot(api->discovery.host.context, result, &count) && count == 1 &&
           result[0].token != first && result[0].identified);
    const uint64_t second = result[0].token;
    queue(3, 51); /* Lost/invalid event cannot manufacture an empty snapshot. */
    count = RISC_USB_HOST_MAX_DEVICES;
    assert(!api->snapshot(api->discovery.host.context, result, &count));
    assert(!api->snapshot(api->discovery.host.context, result, &count));
    idle = false;
    assert(!driver->quiesce());
    driver->stop();
    assert(!driver->start(&dep, 1)); /* No early fault reset. */
    idle = true;
    assert(driver->quiesce());
    driver->stop();
    assert(driver->start(&dep, 1));
    count = RISC_USB_HOST_MAX_DEVICES;
    assert(api->snapshot(api->discovery.host.context, result, &count) && count == 0);
    queue(1, 51);
    count = RISC_USB_HOST_MAX_DEVICES;
    assert(api->snapshot(api->discovery.host.context, result, &count) && count == 1 &&
           result[0].token != first && result[0].token != second);
    assert(polls > 0);
    assert(driver->quiesce());
    driver->stop();
    assert(dlclose(elf) == 0);
    puts("USB host ELF snapshot: atomic capacity, transient descriptor, detach, stale generation and fault recovery PASS");
    return 0;
}

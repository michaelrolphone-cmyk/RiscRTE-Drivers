#include "RiscUsbControllerV1.h"
#include "RiscSerialPortV1.h"
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

static const uint8_t config_single[] = {
    9, 2, 32, 0, 1, 1, 0, 0x80, 50,
    9, 4, 0, 0, 2, 0xff, 0, 0, 0,
    7, 5, 0x81, 2, 64, 0, 0,
    7, 5, 0x02, 2, 64, 0, 0
};
static uint16_t vendor = 0x10c4;
static int claims, releases, controls, reads, writes, fail_request = -1;
static bool fail_release, poll_ok = true, inventory_ok = true;
static bool present = true, pending_detach;
static unsigned polls, inventories;
static uint8_t requests[32], speeds[4];
static uint16_t values[32];

static bool configuration(void *ctx, uint64_t dev, uint8_t *buf,
                          size_t *size, uint16_t *vid, uint16_t *pid) {
    (void)ctx;
    if (!dev || !present || !buf || !size || *size < sizeof(config_single))
        return false;
    memcpy(buf, config_single, sizeof(config_single));
    *size = sizeof(config_single);
    *vid = vendor;
    *pid = 0xea60;
    return true;
}
static bool claim(void *ctx, uint64_t dev, uint8_t iface,
                  uint8_t alt, uint64_t *out) {
    (void)ctx;
    assert(dev == 7 && present && iface == 0 && alt == 0 && out);
    *out = 17;
    ++claims;
    return true;
}
static bool checked_release(void *ctx, uint64_t token) {
    (void)ctx;
    assert(token == 17);
    ++releases;
    return !fail_release;
}
static void release_claim(void *ctx, uint64_t token) {
    (void)checked_release(ctx, token);
}
static bool poll(void *ctx, size_t max_events, size_t *processed) {
    (void)ctx;
    ++polls;
    if (!poll_ok || !processed || max_events == 0) return false;
    *processed = pending_detach ? 1u : 0u;
    if (pending_detach) { present = false; pending_detach = false; }
    return true;
}
static bool devices(void *ctx, uint64_t *out, size_t *count) {
    (void)ctx;
    ++inventories;
    if (!inventory_ok || !count || *count < (present ? 1u : 0u) ||
        (present && !out)) return false;
    if (present) out[0] = 7;
    *count = present ? 1u : 0u;
    return true;
}
static int32_t control(void *ctx, uint64_t claim_token, uint8_t type, uint8_t req,
                       uint16_t value, uint16_t iface, uint8_t *data,
                       uint16_t length, uint32_t timeout) {
    (void)ctx;
    assert(claim_token == 17 && present && type == 0x41 && iface == 0 && timeout == 1000);
    assert(controls < 32);
    requests[controls] = req;
    values[controls] = value;
    ++controls;
    if (fail_request == req) return -1;
    if (req == 0x1e) {
        assert(data && length == 4);
        memcpy(speeds, data, 4);
    } else assert(!data && length == 0);
    return length;
}
static int32_t read_data(void *ctx, uint64_t token, uint8_t ep,
                         uint8_t *dst, size_t cap, uint32_t timeout) {
    (void)ctx;
    assert(token == 17 && ep == 0x81 && cap >= 2 && timeout == 30);
    ++reads;
    dst[0] = 'O'; dst[1] = 'K';
    return 2;
}
static int32_t write_data(void *ctx, uint64_t token, uint8_t ep,
                          const uint8_t *src, size_t length, uint32_t timeout) {
    (void)ctx;
    assert(token == 17 && ep == 0x02 && length == 2 && timeout == 40 &&
           src[0] == 'H' && src[1] == 'I');
    ++writes;
    return 2;
}
int main(int argc, char **argv) {
    assert(argc == 2);
    void *lib = dlopen(argv[1], RTLD_NOW);
    assert(lib);
    risc_driver_get_v2_fn get = (risc_driver_get_v2_fn)dlsym(lib, "t5_driver_get");
    assert(get && !get(1));
    const risc_driver_v2 *driver = get(RISC_PROVIDER_DRIVER_ABI_V2);
    assert(driver && driver->struct_size == sizeof(risc_driver_poll_v2) &&
           strcmp(driver->driver_id, "usb-cp210x-v2") == 0 &&
           strcmp(driver->capability_id, "serial.port") == 0 && driver->quiesce);
    const risc_driver_poll_v2 *poll_driver = (const risc_driver_poll_v2 *)driver;
    assert(poll_driver->streams.last_error);
    char diagnostic[96] = {0};
    assert(!poll_driver->streams.last_error(diagnostic, sizeof(diagnostic)));
    const risc_serial_port_api_v1 *serial = (const risc_serial_port_api_v1 *)driver->capability;
    assert(serial && serial->api_version == 1 && serial->struct_size >= sizeof(*serial));
    const risc_serial_port_discovery_v1 *class_probe =
        (const risc_serial_port_discovery_v1 *)serial;
    assert(serial->struct_size >= sizeof(*class_probe) && class_probe->probe);
    assert(!driver->start(NULL, 0));
    assert(poll_driver->streams.last_error(diagnostic, sizeof(diagnostic)) &&
           strstr(diagnostic, "missing dependency table"));
    char short_diagnostic[8] = {'X','X','X','X','X','X','X','X'};
    assert(poll_driver->streams.last_error(short_diagnostic, sizeof(short_diagnostic)) &&
           short_diagnostic[7] == 0);
    risc_usb_host_api_v1 legacy = {
        RISC_USB_HOST_API_V1, sizeof(legacy), NULL, configuration, claim,
        release_claim, control, read_data, write_data
    };
    risc_provider_dependency_v1 dep = {"usb.host", 1, &legacy};
    assert(!driver->start(&dep, 1));
    assert(poll_driver->streams.last_error(diagnostic, sizeof(diagnostic)) &&
           strstr(diagnostic, "discovery API too small"));
    risc_usb_host_discovery_v1 host = {
        {RISC_USB_HOST_API_V1, sizeof(host), NULL, configuration, claim,
         release_claim, control, read_data, write_data},
        NULL, devices,  0, 0, checked_release, control
    };
    dep.api = &host.host;
    assert(!driver->start(&dep, 1)); /* No reliable detach evidence. */
    assert(poll_driver->streams.last_error(diagnostic, sizeof(diagnostic)) &&
           strstr(diagnostic, "discovery callbacks incomplete"));
    host.poll = poll;
    host.devices = NULL;
    assert(!driver->start(&dep, 1));
    host.devices = devices;
    assert(driver->start(&dep, 1) &&
           !poll_driver->streams.last_error(diagnostic, sizeof(diagnostic)));
    assert(!driver->start(&dep, 1) && driver->quiesce());
    assert(poll_driver->streams.last_error(diagnostic, sizeof(diagnostic)) &&
           strstr(diagnostic, "already active"));

    vendor = 0xffff;
    assert(class_probe->probe(7) == 0 && claims == 0 && controls == 0);
    assert(!serial->open(7) && claims == 0);
    vendor = 0x10c4;
    assert(class_probe->probe(7) == 1 && claims == 0 && controls == 0);
    present = false;
    assert(class_probe->probe(7) < 0 && claims == 0 && controls == 0);
    present = true;
    fail_request = 0;
    assert(!serial->open(7) && claims == 1 && !driver->quiesce() && releases == 0);
    driver->stop();
    assert(!driver->quiesce());
    fail_request = -1;
    driver->stop();
    assert(releases == 1);
    assert(driver->start(&dep, 1));

    uint64_t first = serial->open(7);
    assert(first && claims == 2 && controls >= 3 && !driver->quiesce());
    assert(!serial->configure(first, 0, 8, 0, 1) &&
           !serial->configure(first, 115200, 9, 0, 1));
    assert(serial->configure(first, 115200, 8, 2, 1));
    assert(speeds[0] == 0 && speeds[1] == 0xc2 && speeds[2] == 1 && speeds[3] == 0);
    assert(requests[controls - 2] == 0x1e && requests[controls - 1] == 0x03 &&
           values[controls - 1] == 0x0820);
    assert(serial->control_lines(first, true, false) &&
           requests[controls - 1] == 0x07 && values[controls - 1] == 0x0301);
    uint8_t payload[2] = {0};
    assert(serial->read(first, payload, 2, 30) == 2 &&
           payload[0] == 'O' && payload[1] == 'K');
    assert(serial->write(first, (const uint8_t *)"HI", 2, 40) == 2 &&
           reads == 1 && writes == 1);
    fail_request = 0;
    assert(!serial->close(first) && !driver->quiesce() && releases == 1);
    fail_request = -1;
    fail_release = true;
    assert(!serial->close(first) && !driver->quiesce() && releases == 2);
    int commands = controls;
    assert(serial->read(first, payload, 2, 30) < 0 &&
           !serial->control_lines(first, true, true));
    fail_release = false;
    assert(serial->close(first) && driver->quiesce() &&
           releases == 3 && controls == commands);
    assert(!serial->close(first) && serial->read(first, payload, 2, 30) < 0);

    /* Unknown presence is NOT detach. Neither an unresponsive host nor a
     * still-attached device's UART NACK may silently release its claim. */
    uint64_t next = serial->open(7);
    assert(next && next != first && !serial->control_lines(first, true, true));
    const int before = controls, prior_releases = releases;
    poll_ok = false;
    assert(!serial->close(next) && controls == before && releases == prior_releases);
    poll_ok = true;
    inventory_ok = false;
    assert(!serial->close(next) && controls == before && releases == prior_releases);
    inventory_ok = true;
    fail_request = 0;
    assert(!serial->close(next) && controls == before + 1 &&
           releases == prior_releases);

    /* An actual provider-observed detach invalidates the UART control path.
     * Release the claim without issuing an impossible USB command; retain it
     * if the physical host release itself fails, then retry the SAME token. */
    pending_detach = true;
    fail_release = true;
    const int after_nack = controls;
    assert(!serial->close(next) && !driver->quiesce() &&
           controls == after_nack && releases == prior_releases + 1 &&
           !present && polls > 0 && inventories > 0);
    fail_release = false;
    assert(serial->close(next) && driver->quiesce() &&
           releases == prior_releases + 2 && controls == after_nack);
    assert(!serial->close(next) && serial->read(next, payload, 2, 30) < 0);

    /* An unreturned claim from a failed open is recoverable on disconnect. */
    present = true;
    fail_request = 0;
    pending_detach = true;
    const int before_orphan = releases;
    assert(!serial->open(7) && !present && releases == before_orphan + 1);
    assert(driver->quiesce());
    driver->stop();
    assert(dlclose(lib) == 0);
    puts("CP210x probe, claim-scoped control, release/detach recovery: PASS");
    return 0;
}

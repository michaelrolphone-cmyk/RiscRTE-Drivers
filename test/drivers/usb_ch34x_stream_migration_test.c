#include "RiscUsbControllerV1.h"
#include "RiscSerialPortV1.h"
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

static const uint8_t configuration_bytes[] = {
    9, 2, 32, 0, 1, 1, 0, 0x80, 50,
    9, 4, 0, 0, 2, 0xff, 0, 0, 0,
    7, 5, 0x81, 2, 64, 0, 0,
    7, 5, 0x02, 2, 64, 0, 0
};
static uint16_t vendor = 0x1a86, product = 0x7523;
static uint8_t version = 0x30;
static int claimed, releases, release_attempts, controls, fail_request = -1, reads, writes;
static bool fail_release, fail_configuration;
static uint8_t requests[32], types[32];
static uint16_t values[32], indices[32];

static bool configuration(void *ctx, uint64_t device, uint8_t *data,
                          size_t *length, uint16_t *vid, uint16_t *pid) {
    (void)ctx;
    assert(device == 7);
    if (fail_configuration || !data || !length ||
        *length < sizeof(configuration_bytes)) return false;
    memcpy(data, configuration_bytes, sizeof(configuration_bytes));
    *length = sizeof(configuration_bytes);
    *vid = vendor; *pid = product;
    return true;
}
static bool claim(void *ctx, uint64_t device, uint8_t iface, uint8_t alt, uint64_t *out) {
    (void)ctx;
    assert(device == 7 && iface == 0 && alt == 0 && out);
    ++claimed; *out = 17;
    return true;
}
static bool release_checked(void *ctx, uint64_t token) {
    (void)ctx;
    assert(token == 17);
    ++release_attempts;
    if (fail_release) return false;
    ++releases;
    return true;
}
static void release_claim(void *ctx, uint64_t token) {
    (void)release_checked(ctx, token);
}
static int32_t control(void *ctx, uint64_t claim_token, uint8_t type, uint8_t request,
                       uint16_t value, uint16_t index, uint8_t *data,
                       uint16_t length, uint32_t timeout) {
    (void)ctx;
    assert(claim_token == 17 && timeout == 1000 && controls < 32);
    types[controls] = type; requests[controls] = request;
    values[controls] = value; indices[controls] = index;
    ++controls;
    if (request == fail_request) return -1;
    if (request == 0x5f) {
        assert(type == 0xc0 && data && length == 2);
        data[0] = version; data[1] = 0;
    } else {
        assert(type == 0x40 && data == NULL && length == 0);
    }
    return length;
}
static int32_t read_data(void *ctx, uint64_t token, uint8_t endpoint, uint8_t *data,
                         size_t length, uint32_t timeout) {
    (void)ctx;
    assert(token == 17 && endpoint == 0x81 && length == 2 && timeout == 30);
    ++reads; data[0] = 'O'; data[1] = 'K'; return 2;
}
static int32_t write_data(void *ctx, uint64_t token, uint8_t endpoint,
                          const uint8_t *data, size_t length, uint32_t timeout) {
    (void)ctx;
    assert(token == 17 && endpoint == 0x02 && length == 2 && timeout == 40);
    assert(data[0] == 'H' && data[1] == 'I');
    ++writes; return 2;
}
static bool poll(void *ctx, size_t maximum, size_t *processed) {
    (void)ctx;
    if (!maximum || !processed) return false;
    *processed = 0;
    return true;
}
static bool devices(void *ctx, uint64_t *out, size_t *count) {
    (void)ctx;
    if (!count) return false;
    if (*count < 1 || !out) { *count = 1; return false; }
    out[0] = 7;
    *count = 1;
    return true;
}
int main(int argc, char **argv) {
    assert(argc == 2);
    void *lib = dlopen(argv[1], RTLD_NOW);
    assert(lib);
    risc_driver_get_v2_fn get = (risc_driver_get_v2_fn)dlsym(lib, "t5_driver_get");
    assert(get && !get(1));
    const risc_driver_v2 *driver = get(RISC_PROVIDER_DRIVER_ABI_V2);
    assert(driver && strcmp(driver->driver_id, "usb-ch34x-v2") == 0);
    assert(strcmp(driver->capability_id, "serial.port") == 0);
    const risc_driver_poll_v2 *poll_driver = (const risc_driver_poll_v2 *)driver;
    assert(driver->struct_size == sizeof(risc_driver_poll_v2) &&
           poll_driver->streams.last_error);
    char diagnostic[96] = {0};
    assert(!poll_driver->streams.last_error(diagnostic, sizeof(diagnostic)));
    const risc_usb_cdc_api_v1 *serial = (const risc_usb_cdc_api_v1 *)driver->capability;
    assert(serial && serial->api_version == 1 && serial->struct_size >= sizeof(*serial));
    const risc_serial_port_discovery_v1 *probe =
        (const risc_serial_port_discovery_v1 *)serial;
    assert(serial->struct_size >= sizeof(*probe) && probe->probe);
    const risc_serial_port_inventory_v1 *inventory =
        (const risc_serial_port_inventory_v1 *)serial;
    assert(serial->struct_size >= sizeof(*inventory) && inventory->snapshot);
    assert(!driver->start(NULL, 0));
    assert(poll_driver->streams.last_error(diagnostic, sizeof(diagnostic)) &&
           strstr(diagnostic, "missing dependency table"));
    char short_diagnostic[8] = {'X','X','X','X','X','X','X','X'};
    assert(poll_driver->streams.last_error(short_diagnostic, sizeof(short_diagnostic)) &&
           short_diagnostic[7] == 0);
    risc_usb_host_api_v1 legacy = {RISC_USB_HOST_API_V1, sizeof(legacy), NULL,
        configuration, claim, release_claim, control, read_data, write_data};
    risc_provider_dependency_v1 dep = {"usb.host", 1, &legacy};
    assert(!driver->start(&dep, 1)); /* No success signal for physical release. */
    assert(poll_driver->streams.last_error(diagnostic, sizeof(diagnostic)) &&
           strstr(diagnostic, "discovery API too small"));
    risc_usb_host_discovery_v1 host = {
        {RISC_USB_HOST_API_V1, sizeof(host), NULL,
         configuration, claim, release_claim, control, read_data, write_data},
        NULL, NULL,  0, 0, release_checked, control
    };
    dep.api = &host.host;
    assert(!driver->start(&dep, 1));
    assert(poll_driver->streams.last_error(diagnostic, sizeof(diagnostic)) &&
           strstr(diagnostic, "discovery callbacks incomplete"));
    host.poll = poll; host.devices = devices;
    assert(driver->start(&dep, 1) &&
           !poll_driver->streams.last_error(diagnostic, sizeof(diagnostic)));
    assert(!driver->start(&dep, 1) && driver->quiesce());
    assert(poll_driver->streams.last_error(diagnostic, sizeof(diagnostic)) &&
           strstr(diagnostic, "already active"));
    risc_serial_device_v1 observed[1] = {{0}};
    size_t observed_count = 1;
    assert(inventory->snapshot(observed, &observed_count) && observed_count == 1 &&
           observed[0].provider_device == 7 && observed[0].generation == 7 &&
           observed[0].transport == RISC_SERIAL_TRANSPORT_USB && !claimed && !controls);
    vendor = 0x10c4;
    observed_count = 1;
    assert(inventory->snapshot(observed, &observed_count) && !observed_count);
    assert(probe->probe(7) == 0 && claimed == 0 && controls == 0);
    assert(!serial->open(7) && claimed == 0);
    vendor = 0x1a86;
    assert(probe->probe(7) == 1 && claimed == 0 && controls == 0);
    fail_configuration = true;
    observed_count = 1;
    assert(!inventory->snapshot(observed, &observed_count));
    assert(probe->probe(7) < 0 && claimed == 0 && controls == 0);
    fail_configuration = false;
    fail_request = 0xa1;
    fail_release = true;
    assert(!serial->open(7) && claimed == 1 && releases == 0);
    assert(!driver->quiesce()); /* Failed-open orphan still holds claim. */
    fail_release = false;
    assert(driver->quiesce() && releases == 1 && release_attempts == 3);
    fail_request = -1;
    uint64_t session = serial->open(7);
    assert(session && !driver->quiesce());
    assert(types[controls - 2] == 0xc0 && requests[controls - 2] == 0x5f);
    assert(types[controls - 1] == 0x40 && requests[controls - 1] == 0xa1);
    assert(!serial->configure(session, 0, 8, 0, 1));
    assert(!serial->configure(session, 115200, 9, 0, 1));
    assert(serial->configure(session, 115200, 8, 0, 1));
    assert(requests[controls - 2] == 0x9a && values[controls - 2] == 0x1312);
    assert(requests[controls - 1] == 0x9a && values[controls - 1] == 0x2518);
    assert(indices[controls - 1] == 0xc3);
    assert(serial->control_lines(session, true, false));
    assert(requests[controls - 1] == 0xa4 && values[controls - 1] == 0xffdf);
    uint8_t read[2] = {0};
    assert(serial->read(session, read, 2, 30) == 2);
    assert(read[0] == 'O' && read[1] == 'K');
    assert(serial->write(session, (const uint8_t *)"HI", 2, 40) == 2);
    assert(reads == 1 && writes == 1);
    fail_release = true;
    assert(!serial->close(session) && !driver->quiesce());
    fail_release = false;
    assert(serial->close(session) && driver->quiesce());
    assert(!serial->close(session) && serial->read(session, read, 2, 30) < 0);
    version = 0x27;
    uint64_t old = serial->open(7);
    assert(old && old != session);
    assert(!serial->configure(old, 115200, 7, 0, 1));
    int before = controls;
    assert(serial->configure(old, 115200, 8, 0, 1));
    assert(controls == before + 1 && requests[controls - 1] == 0x9a);
    assert(serial->close(old) && driver->quiesce());
    vendor = 0x1234;
    assert(probe->probe(7) == 0 && !serial->open(7));
    driver->stop();
    assert(dlclose(lib) == 0);
    puts("CH34x ELF inventory, protocol, scoped control, orphan recovery: PASS");
    return 0;
}

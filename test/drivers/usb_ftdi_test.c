#include "RiscUsbProviderV1.h"
#include <assert.h>
#include <dlfcn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const uint8_t single_config[] = {
    9, 2, 32, 0, 1, 1, 0, 0x80, 50,
    9, 4, 0, 0, 2, 0xff, 0xff, 0xff, 0,
    7, 5, 0x81, 2, 64, 0, 0,
    7, 5, 0x02, 2, 64, 0, 0
};
static const uint8_t multi_config[] = {
    9, 2, 55, 0, 2, 1, 0, 0x80, 50,
    9, 4, 0, 0, 2, 0xff, 0xff, 0xff, 0,
    7, 5, 0x81, 2, 64, 0, 0,
    7, 5, 0x02, 2, 64, 0, 0,
    9, 4, 1, 0, 2, 0xff, 0xff, 0xff, 0,
    7, 5, 0x83, 2, 64, 0, 0,
    7, 5, 0x04, 2, 64, 0, 0
};

static uint16_t vendor = 0x0403, product = 0x6001, bcd_device = 0x0600;
static bool multiport;
static unsigned claims, releases, reads, writes, controls;
static uint8_t request_log[64], type_log[64];
static uint16_t value_log[64], index_log[64];
static int fail_request = -1;

static bool configuration(void *ctx, uint64_t device, uint8_t *data,
                          size_t *length, uint16_t *vid, uint16_t *pid) {
    (void)ctx;
    assert(device == 7 && data && length && vid && pid);
    const uint8_t *source = multiport ? multi_config : single_config;
    const size_t size = multiport ? sizeof(multi_config) : sizeof(single_config);
    if (*length < size) return false;
    memcpy(data, source, size);
    *length = size;
    *vid = vendor;
    *pid = product;
    return true;
}

static bool claim(void *ctx, uint64_t device, uint8_t iface,
                  uint8_t alt, uint64_t *out) {
    (void)ctx;
    assert(device == 7 && iface == 0 && alt == 0 && out);
    ++claims;
    *out = 17;
    return true;
}

static void release_claim(void *ctx, uint64_t token) {
    (void)ctx;
    assert(token == 17);
    ++releases;
}

static int32_t control(void *ctx, uint64_t device, uint8_t type, uint8_t request,
                       uint16_t value, uint16_t index, uint8_t *data,
                       uint16_t length, uint32_t timeout) {
    (void)ctx;
    assert(device == 7 && timeout == 1000);
    if (type == 0x80 && request == 6 && value == 0x0100 &&
        index == 0 && data && length == 18) {
        memset(data, 0, length);
        data[0] = 18; data[1] = 1;
        data[8] = (uint8_t)vendor; data[9] = (uint8_t)(vendor >> 8);
        data[10] = (uint8_t)product; data[11] = (uint8_t)(product >> 8);
        data[12] = (uint8_t)bcd_device; data[13] = (uint8_t)(bcd_device >> 8);
        return 18;
    }
    assert(type == 0x40 && !data && length == 0 && controls < 64);
    type_log[controls] = type;
    request_log[controls] = request;
    value_log[controls] = value;
    index_log[controls] = index;
    ++controls;
    if ((int)request == fail_request) return -1;
    return 0;
}

static int32_t read_data(void *ctx, uint64_t claim_id, uint8_t endpoint,
                         uint8_t *data, size_t length, uint32_t timeout) {
    (void)ctx;
    assert(claim_id == 17 && endpoint == 0x81 && data &&
           length == 64 && timeout == 30);
    ++reads;
    if (reads == 1) {
        data[0] = 0x11; data[1] = 0x60; /* modem + line status */
        data[2] = 'A'; data[3] = 'B'; data[4] = 'C'; data[5] = 'D';
        return 6;
    }
    data[0] = 0x11; data[1] = 0x60;
    return 2; /* status-only packet */
}

static int32_t write_data(void *ctx, uint64_t claim_id, uint8_t endpoint,
                          const uint8_t *data, size_t length, uint32_t timeout) {
    (void)ctx;
    assert(claim_id == 17 && endpoint == 0x02 && data &&
           length == 2 && timeout == 40);
    assert(data[0] == 'H' && data[1] == 'I');
    ++writes;
    return 2;
}

static void reset_observation(void) {
    claims = releases = reads = writes = controls = 0;
    fail_request = -1;
    memset(request_log, 0, sizeof(request_log));
    memset(type_log, 0, sizeof(type_log));
    memset(value_log, 0, sizeof(value_log));
    memset(index_log, 0, sizeof(index_log));
}

int main(int argc, char **argv) {
    assert(argc == 2);
    void *lib = dlopen(argv[1], RTLD_NOW);
    assert(lib);
    risc_driver_get_v2_fn get = (risc_driver_get_v2_fn)dlsym(lib, "t5_driver_get");
    assert(get && !get(1));
    const risc_driver_v2 *driver = get(RISC_PROVIDER_DRIVER_ABI_V2);
    assert(driver && driver->struct_size == sizeof(*driver));
    assert(strcmp(driver->driver_id, "usb-ftdi") == 0);
    assert(strcmp(driver->capability_id, "serial.port") == 0);
    assert(driver->quiesce);

    const risc_usb_cdc_api_v1 *serial =
        (const risc_usb_cdc_api_v1 *)driver->capability;
    assert(serial && serial->api_version == 1 &&
           serial->struct_size == sizeof(*serial));

    assert(!driver->start(NULL, 0));
    risc_usb_host_api_v1 host = {
        RISC_USB_HOST_API_V1, sizeof(host), NULL,
        configuration, claim, release_claim, control, read_data, write_data
    };
    risc_provider_dependency_v1 dep = {"usb.host", 1, &host};
    assert(driver->start(&dep, 1));
    assert(!driver->start(&dep, 1));
    assert(driver->quiesce());

    /* Non-FTDI and ambiguous multi-port devices are never claimed. */
    vendor = 0x10c4;
    assert(!serial->open(7) && claims == 0);
    vendor = 0x0403;
    product = 0x6014;
    bcd_device = 0x0900;
    multiport = true;
    assert(!serial->open(7) && claims == 0);
    multiport = false;

    /* Unsupported legacy device generation is refused before interface claim. */
    product = 0x6001;
    bcd_device = 0x0200;
    assert(!serial->open(7) && claims == 0);

    /* FT232R: channel zero, 48 MHz / 16 divisor family. */
    reset_observation();
    product = 0x6001;
    bcd_device = 0x0600;
    uint64_t session = serial->open(7);
    assert(session && claims == 1 && controls == 4);
    assert(request_log[0] == 0 && value_log[0] == 0 && index_log[0] == 0);
    assert(request_log[1] == 0 && value_log[1] == 1 && index_log[1] == 0);
    assert(request_log[2] == 0 && value_log[2] == 2 && index_log[2] == 0);
    assert(request_log[3] == 2 && value_log[3] == 0 && index_log[3] == 0);
    assert(!driver->quiesce());

    assert(!serial->configure(session, 0, 8, 0, 1));
    assert(!serial->configure(session, 115200, 9, 0, 1));
    assert(serial->configure(session, 115200, 8, 0, 1));
    assert(request_log[controls - 2] == 4);
    assert(value_log[controls - 2] == 8 && index_log[controls - 2] == 0);
    assert(request_log[controls - 1] == 3);
    assert(value_log[controls - 1] == 0x001a && index_log[controls - 1] == 0);

    assert(serial->control_lines(session, true, false));
    assert(request_log[controls - 1] == 1);
    assert(value_log[controls - 1] == 0x0301 && index_log[controls - 1] == 0);

    uint8_t bytes[2] = {0};
    assert(serial->read(session, bytes, sizeof(bytes), 30) == 2);
    assert(bytes[0] == 'A' && bytes[1] == 'B' && reads == 1);
    bytes[0] = bytes[1] = 0;
    assert(serial->read(session, bytes, sizeof(bytes), 30) == 2);
    assert(bytes[0] == 'C' && bytes[1] == 'D' && reads == 1);
    assert(serial->read(session, bytes, sizeof(bytes), 30) == 0 && reads == 2);

    assert(serial->write(session, (const uint8_t *)"HI", 2, 40) == 2);
    assert(writes == 1);
    assert(serial->close(session));
    assert(request_log[controls - 1] == 1 &&
           value_log[controls - 1] == 0x0300);
    assert(releases == 1 && driver->quiesce());
    assert(!serial->close(session) && serial->read(session, bytes, 2, 30) < 0);

    /* FT-X: interface A is channel 1 while retaining the BM divisor family. */
    reset_observation();
    product = 0x6015;
    bcd_device = 0x1000;
    uint64_t ftx = serial->open(7);
    assert(ftx && ftx != session);
    assert(index_log[0] == 1 && index_log[1] == 1 &&
           index_log[2] == 1 && index_log[3] == 1);
    assert(serial->configure(ftx, 115200, 8, 2, 2));
    assert(request_log[controls - 2] == 4);
    assert(value_log[controls - 2] == (uint16_t)(8 | (2u << 8) | (2u << 11)));
    assert(index_log[controls - 2] == 1);
    assert(request_log[controls - 1] == 3 &&
           value_log[controls - 1] == 0x001a &&
           index_log[controls - 1] == 1);
    assert(serial->close(ftx) && driver->quiesce());

    /* FT232H: 12 MBaud high-speed divisor and channel-A packing. */
    reset_observation();
    product = 0x6014;
    bcd_device = 0x0900;
    uint64_t high = serial->open(7);
    assert(high && high != ftx);
    assert(serial->configure(high, 12000000, 8, 0, 1));
    assert(request_log[controls - 1] == 3);
    assert(value_log[controls - 1] == 0x0000);
    assert(index_log[controls - 1] == 0x0201);
    assert(!serial->configure(high, 12000001, 8, 0, 1));

    fail_request = 1;
    assert(!serial->close(high) && !driver->quiesce() && releases == 0);
    fail_request = -1;
    assert(serial->close(high) && releases == 1 && driver->quiesce());

    driver->stop();
    assert(dlclose(lib) == 0);
    puts("FTDI ELF IDs, divisors, line control, RX status stripping, buffering and quiescence: PASS");
    return 0;
}

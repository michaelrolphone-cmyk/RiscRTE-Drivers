#include "RiscStlinkV1.h"
#include "RiscPlatformClockV1.h"
#include <assert.h>
#include <dlfcn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const uint8_t config_v2[] = {
    9, 2, 32, 0, 1, 1, 0, 0x80, 50,
    9, 4, 0, 0, 2, 0xff, 0xff, 0xff, 0,
    7, 5, 0x81, 2, 64, 0, 0,
    7, 5, 0x02, 2, 64, 0, 0
};
/* V2.1/V3 command RX is EP1 IN, command TX is EP1 OUT, and an
 * additional EP2 IN may carry SWO trace. */
static const uint8_t config_v21[] = {
    9, 2, 39, 0, 1, 1, 0, 0x80, 50,
    9, 4, 0, 0, 3, 0xff, 0xff, 0xff, 0,
    7, 5, 0x81, 2, 64, 0, 0,
    7, 5, 0x01, 2, 64, 0, 0,
    7, 5, 0x82, 2, 64, 0, 0
};

static uint16_t vendor = 0x0483, product = 0x3748;
static uint8_t current_mode = 2;
static bool attached = true;
static uint64_t now_ms;
static unsigned claims, releases, polls, writes, reads;
static uint8_t last_command[RISC_STLINK_COMMAND_BYTES];
static size_t last_command_length;

static bool configuration(void *ctx, uint64_t device, uint8_t *data,
                          size_t *length, uint16_t *vid, uint16_t *pid) {
    (void)ctx;
    assert(device == 7 && data && length && vid && pid);
    const uint8_t *source = product == 0x3748 ? config_v2 : config_v21;
    const size_t size = product == 0x3748 ? sizeof(config_v2) : sizeof(config_v21);
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
    (void)ctx; (void)device; (void)type; (void)request; (void)value;
    (void)index; (void)data; (void)length; (void)timeout;
    return -1;
}

static int32_t bulk_write(void *ctx, uint64_t claim_id, uint8_t endpoint,
                          const uint8_t *data, size_t length, uint32_t timeout) {
    (void)ctx;
    const uint8_t expected = product == 0x3748 ? 0x02 : 0x01;
    assert(claim_id == 17 && endpoint == expected && data && timeout <= 5000);
    ++writes;
    if (length == RISC_STLINK_COMMAND_BYTES) {
        memcpy(last_command, data, length);
        last_command_length = length;
        if (data[0] == 0xf2 && data[1] == 0x21) current_mode = 1;
        if (data[0] == 0xf4 && data[1] == 0x01) current_mode = 1;
        if (data[0] == 0xf4 && data[1] == 0x00) current_mode = 3;
    }
    return (int32_t)length;
}

static int32_t bulk_read(void *ctx, uint64_t claim_id, uint8_t endpoint,
                         uint8_t *data, size_t length, uint32_t timeout) {
    (void)ctx;
    assert(claim_id == 17 && endpoint == 0x81 && data && length && timeout <= 5000);
    ++reads;
    memset(data, 0, length);
    assert(last_command_length == RISC_STLINK_COMMAND_BYTES);
    if (last_command[0] == 0xf5) {
        data[0] = current_mode;
        return length >= 2 ? 2 : 1;
    }
    if (last_command[0] == 0xf2 &&
        (last_command[1] == 0x30 || last_command[1] == 0x20)) {
        assert(last_command[2] == 0xa3);
        current_mode = 2;
        data[0] = 0x80;
        return length >= 2 ? 2 : 1;
    }
    memset(data, 0xa5, length);
    return (int32_t)length;
}

static bool poll_host(void *ctx, size_t max_events, size_t *processed) {
    (void)ctx;
    assert(max_events > 0 && max_events <= 16 && processed);
    ++polls;
    *processed = attached ? 1 : 0;
    return true;
}

static uint64_t monotonic_ms(void *ctx) {
    (void)ctx;
    return now_ms;
}

static bool devices(void *ctx, uint64_t *out, size_t *count) {
    (void)ctx;
    assert(count);
    if (!attached) {
        *count = 0;
        return true;
    }
    if (*count < 1 || !out) {
        *count = 1;
        return false;
    }
    out[0] = 7;
    *count = 1;
    return true;
}

static void reset_io(void) {
    claims = releases = polls = writes = reads = 0;
    last_command_length = 0;
    memset(last_command, 0, sizeof(last_command));
}

int main(int argc, char **argv) {
    assert(argc == 2);
    void *lib = dlopen(argv[1], RTLD_NOW);
    assert(lib);
    risc_driver_get_v2_fn get = (risc_driver_get_v2_fn)dlsym(lib, "t5_driver_get");
    assert(get && !get(1));
    const risc_driver_v2 *driver = get(RISC_PROVIDER_DRIVER_ABI_V2);
    assert(driver && driver->struct_size == sizeof(*driver));
    assert(strcmp(driver->driver_id, "usb-stlink") == 0);
    assert(strcmp(driver->capability_id, "debug.vendor.stlink") == 0);
    assert(driver->capability_api == RISC_STLINK_API_V1);
    assert(driver->quiesce);

    const risc_stlink_api_v1 *stlink = (const risc_stlink_api_v1 *)driver->capability;
    assert(stlink && stlink->api_version == RISC_STLINK_API_V1 &&
           stlink->struct_size == sizeof(*stlink));

    assert(!driver->start(NULL, 0));
    risc_usb_host_discovery_v1 host = {
        .host = {
            .api_version = RISC_USB_HOST_API_V1,
            .struct_size = sizeof(risc_usb_host_discovery_v1),
            .context = NULL,
            .configuration = configuration,
            .claim = claim,
            .release = release_claim,
            .control = control,
            .bulk_read = bulk_read,
            .bulk_write = bulk_write,
        },
        .poll = poll_host,
        .devices = devices,
    };
    risc_platform_clock_api_v1 clock = {
        RISC_PLATFORM_CLOCK_API_V1, sizeof(risc_platform_clock_api_v1), NULL,
        monotonic_ms, NULL
    };
    risc_provider_dependency_v1 deps[2] = {
        {"usb.host", 1, &host},
        {"platform.clock", 1, &clock},
    };
    assert(driver->start(deps, 2));
    assert(!driver->start(deps, 2));

    /* ST-LINK/V1 uses SCSI transport and is intentionally not claimed. */
    product = 0x3744;
    now_ms = 10;
    assert(stlink->poll(NULL, 4));
    size_t count = 1;
    risc_stlink_probe_v1 probe = {0};
    assert(stlink->snapshot(NULL, &probe, &count) && count == 0);

    /* A new USB generation is required before an unsupported device can be
     * reconsidered with different descriptors/identity. */
    attached = false;
    now_ms += 10;
    assert(stlink->poll(NULL, 4));
    attached = true;

    /* ST-LINK/V2 is discovered as a dual SWD/SWIM probe. */
    product = 0x3748;
    now_ms += 10;
    assert(stlink->poll(NULL, 4));
    count = 0;
    assert(!stlink->snapshot(NULL, NULL, &count) && count == 1);
    count = 1;
    assert(stlink->snapshot(NULL, &probe, &count) && count == 1);
    assert(probe.device == 7 && probe.vid == 0x0483 && probe.pid == 0x3748);
    assert(probe.variant == RISC_STLINK_VARIANT_V2);
    assert(probe.rx_endpoint == 0x81 && probe.tx_endpoint == 0x02);
    assert(probe.supports_swd && probe.supports_swim);

    /* SWD entry exits an existing debug mode and re-enters explicitly as SWD. */
    reset_io();
    current_mode = 2;
    uint64_t swd = stlink->open(NULL, 7, RISC_STLINK_TRANSPORT_SWD);
    assert(swd && claims == 1 && !driver->quiesce());
    assert(current_mode == 2);
    assert(writes >= 3 && reads >= 2);

    uint8_t command[1] = {0xf7};
    uint8_t response[4] = {0};
    assert(stlink->command(NULL, swd, command, sizeof(command),
                           NULL, 0, response, sizeof(response), 1000) == 4);
    assert(response[0] == 0xa5 && response[3] == 0xa5);
    assert(stlink->command(NULL, swd, command, sizeof(command),
                           (const uint8_t *)"x", 1, response, 1, 1000) < 0);
    assert(stlink->close(NULL, swd));
    assert(releases == 1 && driver->quiesce());

    /* V2.1 can enter SWIM for STM8 and leaves SWIM before releasing. */
    attached = false;
    now_ms += 10;
    assert(stlink->poll(NULL, 4));
    attached = true;
    product = 0x374b;
    now_ms += 10;
    assert(stlink->poll(NULL, 4));
    count = 1;
    assert(stlink->snapshot(NULL, &probe, &count) && count == 1);
    assert(probe.variant == RISC_STLINK_VARIANT_V2_1);
    assert(probe.rx_endpoint == 0x81 && probe.tx_endpoint == 0x01);
    reset_io();
    current_mode = 2;
    uint64_t swim = stlink->open(NULL, 7, RISC_STLINK_TRANSPORT_SWIM);
    assert(swim && current_mode == 3);
    assert(stlink->set_transport(NULL, swim, RISC_STLINK_TRANSPORT_SWIM));
    assert(stlink->close(NULL, swim) && current_mode == 1);
    assert(releases == 1);

    /* V3 family is recognized by the same raw-bulk transport. */
    attached = false;
    now_ms += 10;
    assert(stlink->poll(NULL, 4));
    attached = true;
    product = 0x374e;
    now_ms += 10;
    assert(stlink->poll(NULL, 4));
    count = 1;
    assert(stlink->snapshot(NULL, &probe, &count) && count == 1);
    assert(probe.variant == RISC_STLINK_VARIANT_V3);
    assert(probe.rx_endpoint == 0x81 && probe.tx_endpoint == 0x01);

    /* DFU/bootloader mode is not silently exited because it re-enumerates. */
    reset_io();
    current_mode = 0;
    assert(!stlink->open(NULL, 7, RISC_STLINK_TRANSPORT_SWD));
    assert(claims == 1 && releases == 1 && driver->quiesce());

    driver->stop();
    assert(dlclose(lib) == 0);
    puts("ST-LINK V2/V2.1/V3 discovery, SWD, SWIM, framing and quiescence: PASS");
    return 0;
}

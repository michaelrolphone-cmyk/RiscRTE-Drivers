#include "RiscMspFetV1.h"
#include "RiscPlatformClockV1.h"
#include <assert.h>
#include <dlfcn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Two CDC ACM functions: interfaces 0/1 are the FET debug transport and
 * interfaces 2/3 are the independent Backchannel UART. */
static const uint8_t descriptor[] = {
    9,2,83,0,4,1,0,0x80,50,
    9,4,0,0,0,2,2,1,0,
    5,0x24,6,0,1,
    9,4,1,0,2,10,0,0,0,
    7,5,0x81,2,64,0,0,
    7,5,0x01,2,64,0,0,
    9,4,2,0,0,2,2,1,0,
    5,0x24,6,2,3,
    9,4,3,0,2,10,0,0,0,
    7,5,0x82,2,64,0,0,
    7,5,0x02,2,64,0,0
};

static uint16_t vendor = 0x2047, product = 0x0013;
static bool attached = true;
static uint64_t now_ms = 100;
static unsigned claims, releases, controls, writes, reads, resets;
static uint8_t pending;
static uint8_t pending_fid;
static uint8_t pending_ref;
static uint8_t last_mode = 0xff;
static uint8_t request_copy[32];
static size_t request_length;

static bool configuration(void *ctx, uint64_t device, uint8_t *data,
                          size_t *length, uint16_t *vid, uint16_t *pid) {
    (void)ctx;
    assert(device == 7 && data && length && vid && pid);
    if (*length < sizeof(descriptor)) return false;
    memcpy(data, descriptor, sizeof(descriptor));
    *length = sizeof(descriptor);
    *vid = vendor;
    *pid = product;
    return true;
}

static bool claim(void *ctx, uint64_t device, uint8_t iface,
                  uint8_t alt, uint64_t *out) {
    (void)ctx;
    assert(device == 7 && out && alt == 0);
    /* The driver must never claim the Backchannel UART interfaces 2/3. */
    assert(iface == 0 || iface == 1);
    *out = (uint64_t)(100 + iface);
    ++claims;
    return true;
}

static void release_claim(void *ctx, uint64_t token) {
    (void)ctx;
    assert(token == 100 || token == 101);
    ++releases;
}

static int32_t control(void *ctx, uint64_t device, uint8_t type, uint8_t request,
                       uint16_t value, uint16_t index, uint8_t *data,
                       uint16_t length, uint32_t timeout) {
    (void)ctx;
    assert(device == 7 && type == 0x21 && index == 0 && timeout == 1000);
    if (request == 0x20) {
        assert(value == 0 && data && length == 7);
        assert(data[0] == 0x00 && data[1] == 0x08 &&
               data[2] == 0x07 && data[3] == 0x00);
        assert(data[4] == 0 && data[5] == 0 && data[6] == 8);
        ++controls;
        return 7;
    }
    assert(request == 0x22 && value == 0 && !data && length == 0);
    ++controls;
    return 0;
}

static size_t make_frame(uint8_t type, uint8_t ref,
                         const uint8_t *payload, size_t payload_length,
                         uint8_t *out) {
    size_t n = 0;
    out[n++] = (uint8_t)(payload_length + 3u);
    out[n++] = type;
    out[n++] = ref;
    out[n++] = 0;
    if (payload_length) {
        memcpy(out + n, payload, payload_length);
        n += payload_length;
    }
    if (n & 1u) out[n++] = 0;

    uint8_t even = 0xff, odd = 0xff;
    for (size_t i = 0; i < n; i += 2u) {
        even ^= out[i];
        odd ^= out[i + 1u];
    }
    out[n++] = even;
    out[n++] = odd;
    return n;
}

static int32_t bulk_write(void *ctx, uint64_t claim_id, uint8_t endpoint,
                          const uint8_t *data, size_t length, uint32_t timeout) {
    (void)ctx;
    assert(claim_id == 101 && endpoint == 0x01 && data && length >= 4);
    assert(timeout > 0 && timeout <= 100);
    ++writes;

    const uint8_t type = data[1];
    if (type == 0x92) {
        ++resets;
        pending = 0;
        return (int32_t)length;
    }
    if (type == 0x91) {
        assert(data[2] == pending_ref);
        pending = 2; /* final ACK now available */
        return (int32_t)length;
    }

    assert(type == 0x81 && length >= 6);
    pending_ref = data[2];
    pending_fid = data[4];
    request_length = data[0] >= 5 ? (size_t)data[0] - 5u : 0u;
    assert(request_length <= sizeof(request_copy));
    if (request_length) memcpy(request_copy, data + 6, request_length);

    if (pending_fid == 0x06) {
        pending = 2; /* STOP_JTAG returns only acknowledgement here. */
    } else {
        pending = 1; /* DATA followed by host ACK and final probe ACK. */
    }
    return (int32_t)length;
}

static int32_t bulk_read(void *ctx, uint64_t claim_id, uint8_t endpoint,
                         uint8_t *data, size_t capacity, uint32_t timeout) {
    (void)ctx;
    assert(claim_id == 101 && endpoint == 0x81 && data && capacity >= 8);
    assert(timeout > 0 && timeout <= 100);
    ++reads;

    if (!pending) return 0;

    uint8_t payload[16] = {0};
    size_t payload_length = 0;
    uint8_t type = 0x91;

    if (pending == 1) {
        type = 0x93;
        if (pending_fid == 0x00) {
            const uint8_t version[] = {10, 0x82, 3, 0, 0xac, 0xaa, 0xac, 0xaa};
            memcpy(payload, version, sizeof(version));
            payload_length = sizeof(version);
        } else if (pending_fid == 0x04) {
            assert(request_length == 1);
            last_mode = request_copy[0];
            payload[0] = 1; /* one target found */
            payload_length = 1;
        } else {
            payload[0] = 0xde;
            payload[1] = 0xad;
            payload[2] = pending_fid;
            payload_length = 3;
        }
        pending = 0; /* wait for host ACK before final ACK */
    } else {
        pending = 0;
    }

    uint8_t frame[64] = {0};
    const size_t n = make_frame(type, pending_ref, payload, payload_length, frame);
    assert(n <= capacity);
    memcpy(data, frame, n);
    return (int32_t)n;
}

static bool poll_host(void *ctx, size_t max_events, size_t *processed) {
    (void)ctx;
    assert(max_events > 0 && max_events <= 16 && processed);
    *processed = attached ? 1 : 0;
    return true;
}

static bool devices(void *ctx, uint64_t *out, size_t *count) {
    (void)ctx;
    assert(count);
    if (!attached) {
        *count = 0;
        return true;
    }
    if (!out || *count < 1) {
        *count = 1;
        return false;
    }
    out[0] = 7;
    *count = 1;
    return true;
}

static uint64_t monotonic_ms(void *ctx) {
    (void)ctx;
    return now_ms;
}

static void reset_observation(void) {
    claims = releases = controls = writes = reads = resets = 0;
    pending = pending_fid = pending_ref = 0;
    request_length = 0;
    last_mode = 0xff;
    memset(request_copy, 0, sizeof(request_copy));
}

static void replug(const risc_msp_fet_api_v1 *msp, uint16_t next_pid) {
    attached = false;
    now_ms += 10;
    assert(msp->poll(NULL, 4));
    attached = true;
    product = next_pid;
    now_ms += 10;
    assert(msp->poll(NULL, 4));
}

int main(int argc, char **argv) {
    assert(argc == 2);
    void *lib = dlopen(argv[1], RTLD_NOW);
    assert(lib);
    risc_driver_get_v2_fn get =
        (risc_driver_get_v2_fn)dlsym(lib, "t5_driver_get");
    assert(get && !get(1));

    const risc_driver_v2 *driver = get(RISC_PROVIDER_DRIVER_ABI_V2);
    assert(driver && driver->struct_size == sizeof(*driver));
    assert(strcmp(driver->driver_id, "usb-msp") == 0);
    assert(strcmp(driver->capability_id, "debug.vendor.msp") == 0);
    assert(driver->capability_api == RISC_MSP_FET_API_V1);
    assert(driver->quiesce);

    const risc_msp_fet_api_v1 *msp =
        (const risc_msp_fet_api_v1 *)driver->capability;
    assert(msp && msp->api_version == RISC_MSP_FET_API_V1 &&
           msp->struct_size == sizeof(*msp));

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
        RISC_PLATFORM_CLOCK_API_V1, sizeof(risc_platform_clock_api_v1),
        NULL, monotonic_ms, NULL
    };
    risc_provider_dependency_v1 deps[2] = {
        {"usb.host", 1, &host},
        {"platform.clock", 1, &clock},
    };

    assert(!driver->start(NULL, 0));
    assert(driver->start(deps, 2));
    assert(!driver->start(deps, 2));

    /* Recovery identities must not be presented as debug probes. */
    product = 0x0203;
    assert(msp->poll(NULL, 4));
    size_t count = 1;
    risc_msp_fet_probe_v1 probe = {0};
    assert(msp->snapshot(NULL, &probe, &count) && count == 0);
    replug(msp, 0x0013);

    count = 0;
    assert(!msp->snapshot(NULL, NULL, &count) && count == 1);
    count = 1;
    assert(msp->snapshot(NULL, &probe, &count) && count == 1);
    assert(probe.device == 7 && probe.vid == 0x2047 && probe.pid == 0x0013);
    assert(probe.variant == RISC_MSP_FET_VARIANT_EZFET_LITE);
    assert(probe.control_interface == 0 && probe.data_interface == 1);
    assert(probe.rx_endpoint == 0x81 && probe.tx_endpoint == 0x01);
    assert(probe.supports_jtag && probe.supports_sbw &&
           probe.has_backchannel_uart);

    reset_observation();
    uint64_t session = msp->open(NULL, 7, RISC_MSP_FET_INTERFACE_SBW);
    assert(session && claims == 2 && controls == 2);
    assert(resets == 1 && last_mode == 1 && !driver->quiesce());

    const uint8_t request[] = {1, 2, 3};
    uint8_t response[8] = {0};
    const int32_t n = msp->execute(NULL, session, 0x15, request,
                                   sizeof(request), response,
                                   sizeof(response), 1000);
    assert(n == 3 && response[0] == 0xde && response[1] == 0xad &&
           response[2] == 0x15);
    assert(request_length == sizeof(request) &&
           memcmp(request_copy, request, sizeof(request)) == 0);

    assert(msp->set_interface(NULL, session, RISC_MSP_FET_INTERFACE_JTAG));
    assert(last_mode == 0);
    assert(msp->close(NULL, session));
    assert(releases == 2 && resets == 2 && driver->quiesce());
    assert(msp->execute(NULL, session, 0x15, NULL, 0,
                        response, sizeof(response), 1000) < 0);

    /* MSP-FET application identity is also supported. */
    replug(msp, 0x0014);
    count = 1;
    assert(msp->snapshot(NULL, &probe, &count) && count == 1);
    assert(probe.variant == RISC_MSP_FET_VARIANT_MSP_FET);

    reset_observation();
    uint64_t fet = msp->open(NULL, 7, RISC_MSP_FET_INTERFACE_JTAG);
    assert(fet && last_mode == 0);

    /* Detach revokes the generation and frees both physical claims. */
    attached = false;
    now_ms += 10;
    assert(msp->poll(NULL, 4));
    assert(releases == 2 && driver->quiesce());
    assert(msp->execute(NULL, fet, 0x15, NULL, 0,
                        response, sizeof(response), 1000) < 0);

    driver->stop();
    assert(dlclose(lib) == 0);
    puts("MSP-FET/eZ-FET discovery, HAL framing, JTAG, SBW, detach and quiescence: PASS");
    return 0;
}

#include "RiscProgramMspV1.h"
#include "RiscMspFetV1.h"
#include <assert.h>
#include <dlfcn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint8_t memory[256];
static uint8_t jtag_id = 0x99;
static uint8_t last_open_mode;
static uint8_t last_fid;
static unsigned opens, closes, polls, syncs, releases_target, configures;
static size_t probe_count = 1;

static bool poll_msp(void *ctx, size_t max_events) {
    (void)ctx;
    assert(max_events == 8);
    ++polls;
    return true;
}

static bool snapshot_msp(void *ctx, risc_msp_fet_probe_v1 *out, size_t *count) {
    (void)ctx;
    assert(count);
    if (*count < probe_count || (probe_count && !out)) {
        *count = probe_count;
        return false;
    }
    for (size_t i = 0; i < probe_count; ++i) {
        memset(&out[i], 0, sizeof(out[i]));
        out[i].device = 7 + i;
        out[i].variant = RISC_MSP_FET_VARIANT_MSP_FET;
        out[i].supports_jtag = out[i].supports_sbw = 1;
    }
    *count = probe_count;
    return true;
}

static uint64_t open_msp(void *ctx, uint64_t device, uint8_t mode) {
    (void)ctx;
    assert(device == 7 || device == 8);
    assert(mode == RISC_MSP_FET_INTERFACE_SBW || mode == RISC_MSP_FET_INTERFACE_JTAG);
    ++opens;
    last_open_mode = mode;
    return 0x1234;
}

static bool set_interface(void *ctx, uint64_t session, uint8_t mode) {
    (void)ctx; (void)session; (void)mode;
    return true;
}

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int32_t execute_msp(void *ctx, uint64_t session, uint8_t fid,
                           const uint8_t *request, size_t request_length,
                           uint8_t *response, size_t response_capacity,
                           uint32_t timeout_ms) {
    (void)ctx;
    assert(session == 0x1234 && timeout_ms > 0);
    last_fid = fid;
    if (fid == 0x00) {
        assert(request && request_length == 1 && response && response_capacity >= 8);
        memset(response, 0, 8);
        response[1] = (uint8_t)((3u << 6) | 8u); /* protocol 3.8 */
        return 8;
    }
    if (fid == 0x52) {
        assert(!request && request_length == 0);
        return 0;
    }
    if (fid == 0x0c) {
        assert(response && response_capacity >= 1);
        response[0] = jtag_id;
        return 1;
    }
    if (fid == 0x07) {
        assert(request && request_length == 8);
        ++configures;
        return 0;
    }
    if (fid == 0x4c) {
        assert(!request && request_length == 0);
        return 0;
    }
    if (fid == 0x3a) {
        assert(request && request_length == 21 && response && response_capacity >= 8);
        assert(request[0] == 0x5c && request[1] == 0x01 &&
               request[2] == 0x80 && request[3] == 0x5a);
        memset(response, 0, 8);
        response[0] = 0x80;
        response[2] = 0x00; response[3] = 0x44; /* PC = 0x4400 */
        ++syncs;
        return 8;
    }
    if (fid == 0x3c) {
        assert(request && request_length == 18);
        assert(request[0] == 0x5c && request[1] == 0x01 &&
               request[2] == 0x80 && request[3] == 0x5a);
        assert(request[4] == 0x00 && request[5] == 0x44);
        assert(request[10] == 7 && request[14] == 1);
        ++releases_target;
        return 0;
    }
    if (fid == 0x3d) {
        assert(request && request_length == 8 && response);
        const uint32_t address = get32(request);
        const uint32_t words = get32(request + 4);
        const size_t bytes = (size_t)words * 2u;
        assert(address >= 0x1000 && address + bytes <= 0x1100);
        assert(response_capacity >= bytes);
        memcpy(response, memory + (address - 0x1000), bytes);
        return (int32_t)bytes;
    }
    if (fid == 0x4e) {
        assert(request && request_length >= 10);
        const uint32_t address = get32(request);
        const uint32_t words = get32(request + 4);
        const size_t bytes = (size_t)words * 2u;
        assert(request_length == 8u + bytes);
        assert(address >= 0x1000 && address + bytes <= 0x1100);
        memcpy(memory + (address - 0x1000), request + 8, bytes);
        return 0;
    }
    assert(!"unexpected MSP FID");
    return -1;
}

static bool close_msp(void *ctx, uint64_t session) {
    (void)ctx;
    assert(session == 0x1234);
    ++closes;
    return true;
}

int main(int argc, char **argv) {
    assert(argc == 2);
    void *lib = dlopen(argv[1], RTLD_NOW);
    assert(lib);
    risc_driver_get_v2_fn get = (risc_driver_get_v2_fn)dlsym(lib, "t5_driver_get");
    assert(get && !get(1));
    const risc_driver_v2 *driver = get(RISC_PROVIDER_DRIVER_ABI_V2);
    assert(driver && strcmp(driver->driver_id, "program-msp") == 0);
    assert(strcmp(driver->capability_id, "program.msp") == 0);

    risc_msp_fet_api_v1 msp = {
        RISC_MSP_FET_API_V1, sizeof(risc_msp_fet_api_v1), NULL,
        poll_msp, snapshot_msp, open_msp, set_interface,
        execute_msp, close_msp
    };
    risc_provider_dependency_v1 dep = {"debug.vendor.msp", 1, &msp};
    assert(driver->start(&dep, 1));

    const risc_program_msp_api_v1 *api =
        (const risc_program_msp_api_v1 *)driver->capability;
    assert(api && api->api_version == 1 && api->struct_size == sizeof(*api));

    memset(memory, 0xaa, sizeof(memory));
    risc_program_msp_target_v1 target = {0};
    uint64_t session = api->open(api->context, 0, RISC_PROGRAM_MSP_INTERFACE_AUTO, &target);
    assert(session && opens == 1 && polls == 1);
    assert(last_open_mode == RISC_MSP_FET_INTERFACE_SBW);
    assert(target.probe_device == 7 && target.jtag_id == 0x99);
    assert(target.protocol_major == 3 && target.protocol_minor == 8);
    assert(configures >= 8 && syncs == 1 && last_fid == 0x3a);
    assert(!driver->quiesce());

    const uint8_t bytes[] = {0x11, 0x22, 0x33};
    assert(api->write(api->context, session, 0x1001, bytes, sizeof(bytes)) == 3);
    assert(memory[0] == 0xaa && memory[1] == 0x11 &&
           memory[2] == 0x22 && memory[3] == 0x33 && memory[4] == 0xaa);
    assert(api->verify(api->context, session, 0x1001, bytes, sizeof(bytes)) == 3);

    memory[2] ^= 1u;
    assert(api->verify(api->context, session, 0x1001, bytes, sizeof(bytes)) == -2);
    char error[160] = {0};
    assert(api->last_error(api->context, error, sizeof(error)));
    assert(strstr(error, "differs"));

    assert(api->close(api->context, session));
    assert(syncs == 2 && releases_target == 1);
    assert(closes == 1 && driver->quiesce());

    /* Ambiguous default probe selection fails before opening hardware. */
    probe_count = 2;
    memset(&target, 0, sizeof(target));
    assert(!api->open(api->context, 0, RISC_PROGRAM_MSP_INTERFACE_AUTO, &target));
    assert(opens == 1);

    /* Explicit probe selection succeeds but unsupported non-XV2 targets close. */
    probe_count = 1;
    jtag_id = 0x89;
    assert(!api->open(api->context, 7, RISC_PROGRAM_MSP_INTERFACE_JTAG, &target));
    assert(opens == 2 && closes == 2);

    driver->stop();
    assert(dlclose(lib) == 0);
    puts("MSP programmer target gating, FRAM RMW/write/verify and cleanup: PASS");
    return 0;
}

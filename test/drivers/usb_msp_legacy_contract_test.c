/* Prefix-sized/partially advertised host tables must never be over-read. */
#define main baseline_msp_fixture_main
#include "usb_msp_test.c"
#undef main
#include <stddef.h>
#include <stdlib.h>
static unsigned checked_calls, scoped_controls;
static bool permit_release;
static bool checked(void *ctx, uint64_t token) {
    ++checked_calls;
    if (!permit_release) return false;
    release_claim(ctx, token); return true;
}
static int32_t scoped_control(void *ctx, uint64_t token, uint8_t type,
    uint8_t request, uint16_t value, uint16_t index, uint8_t *data,
    uint16_t length, uint32_t timeout) {
    assert(token == 100); ++scoped_controls;
    return control(ctx, 7, type, request, value, index, data, length, timeout);
}
int main(int argc, char **argv) {
    assert(argc == 3);
    const bool has_release = strcmp(argv[2], "release-only") == 0 || strcmp(argv[2], "full") == 0 || strcmp(argv[2], "truncated-control") == 0;
    const bool full = strcmp(argv[2], "full") == 0;
    size_t size = sizeof(risc_usb_host_legacy_discovery_v1);
    if (strcmp(argv[2], "truncated-release") == 0)
        size = offsetof(risc_usb_host_discovery_v1, release_checked) + sizeof(void *) - 1;
    if (strcmp(argv[2], "null-release") == 0 || full) size = sizeof(risc_usb_host_discovery_v1);
    if (strcmp(argv[2], "release-only") == 0)
        size = offsetof(risc_usb_host_discovery_v1, release_checked) + sizeof(void *);
    if (strcmp(argv[2], "truncated-control") == 0)
        size = offsetof(risc_usb_host_discovery_v1, control_claim) + sizeof(void *) - 1;
    risc_usb_host_discovery_v1 source = {
        .host = {1, (uint32_t)size, NULL, configuration, claim, release_claim,
                 control, bulk_read, bulk_write},
        .poll = poll_host, .devices = devices,
        .release_checked = has_release ? checked : NULL,
        .control_claim = full ? scoped_control : NULL
    };
    /* ASan's heap redzone starts at the advertised size, not the new struct. */
    void *table = malloc(size); assert(table); memcpy(table, &source, size);
    void *lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL); assert(lib);
    risc_driver_get_v2_fn get = (risc_driver_get_v2_fn)dlsym(lib, "t5_driver_get"); assert(get);
    const risc_driver_v2 *driver = get(2);
    const risc_msp_fet_api_v1 *msp = driver->capability;
    risc_platform_clock_api_v1 clock = {1, sizeof(clock), NULL, monotonic_ms, NULL};
    risc_provider_dependency_v1 deps[] = {{"usb.host", 1, table}, {"platform.clock", 1, &clock}};
    assert(driver->start(deps, 2));
    assert(msp->poll(NULL, 4));
    uint64_t token = msp->open(NULL, 7, 2); assert(token);
    assert(scoped_controls == (full ? 2u : 0u));
    assert(!msp->close(NULL, token));
    assert(!driver->quiesce()); driver->stop(); assert(!driver->start(deps, 2));
    unsigned io = reads + writes + controls;
    attached = false; assert(msp->poll(NULL, 4));
    assert(!msp->close(NULL, token));
    assert(io == reads + writes + controls);
    if (has_release) {
        assert(checked_calls && !releases);
        permit_release = true;
        assert(!driver->quiesce()); /* Physical done, caller's receipt retained. */
        assert(releases == 2);
        assert(msp->close(NULL, token) && !msp->close(NULL, token));
        assert(driver->quiesce()); driver->stop();
        assert(dlclose(lib) == 0); free(table);
    } else {
        assert(!checked_calls && releases == 2);
        permit_release = true; /* Irrelevant: the ABI cannot return this proof. */
        assert(!msp->close(NULL, token) && !driver->quiesce());
        assert(releases == 2); /* No duplicate void release and no fake success. */
        /* No explicit dlclose or dependency free: this process ends with the
         * intentionally pinned conservative legacy session. No hardware. */
    }
    printf("PASS MSP size negotiation and custody: %s\n", argv[2]);
}

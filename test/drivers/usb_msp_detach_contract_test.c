/* Offline contract observation, not a repaired detach path or hardware test.
 * Reuse the existing fake TI probe framing while composing BOTH real ELFs. */
#define main baseline_msp_fixture_main
#include "usb_msp_test.c"
#undef main
#include "RiscUsbInterruptV1.h"

static unsigned event_kind = 1;
static unsigned physical_live;
static bool allow_physical_release;

static int32_t next_controller_event(void *ctx, risc_usb_controller_event_v1 *out) {
    (void)ctx;
    if (!event_kind) return 0;
    *out = (risc_usb_controller_event_v1){event_kind, 7};
    event_kind = 0;
    return 1;
}

static bool claim_controller(void *ctx, uint64_t device, uint8_t iface,
                              uint8_t alt, uint64_t *out) {
    bool ok = claim(ctx, device, iface, alt, out);
    assert(ok && !(physical_live & (1u << iface)));
    physical_live |= 1u << iface;
    return ok;
}

static bool release_controller(void *ctx, uint64_t token) {
    assert(token == 100 || token == 101);
    const unsigned bit = 1u << (unsigned)(token - 100);
    assert(physical_live & bit);
    release_claim(ctx, token);
    if (!allow_physical_release) return false;
    physical_live &= ~bit;
    return true;
}

static bool quiesce_controller(void *ctx) { (void)ctx; return physical_live == 0; }
static int32_t no_interrupt(void *ctx, uint64_t claim_id, uint8_t endpoint,
                             uint8_t *data, size_t capacity, uint32_t timeout) {
    (void)ctx; (void)claim_id; (void)endpoint; (void)data; (void)capacity; (void)timeout;
    assert(!"MSP debug flow must not request interrupt transfers");
    return -1;
}

static const risc_driver_v2 *load_driver(const char *path, void **library) {
    *library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    assert(*library);
    risc_driver_get_v2_fn get = (risc_driver_get_v2_fn)dlsym(*library, "t5_driver_get");
    assert(get);
    return get(RISC_PROVIDER_DRIVER_ABI_V2);
}

int main(int argc, char **argv) {
    assert(argc == 4);
    const bool initially_fails = strcmp(argv[3], "failed-release") == 0;
    assert(initially_fails || strcmp(argv[3], "successful-release") == 0);
    allow_physical_release = !initially_fails;
    reset_observation();
    void *host_library, *msp_library;
    const risc_driver_v2 *host_driver = load_driver(argv[1], &host_library);
    const risc_driver_v2 *msp_driver = load_driver(argv[2], &msp_library);
    assert(strcmp(host_driver->driver_id, "usb-host-v2") == 0);
    assert(strcmp(msp_driver->driver_id, "usb-msp") == 0);

    risc_usb_controller_interrupt_v1 controller_api = {
        {RISC_USB_CONTROLLER_API_V1, sizeof(controller_api), NULL,
         next_controller_event, configuration, claim_controller, release_controller,
         control, bulk_read, bulk_write, quiesce_controller}, no_interrupt
    };
    risc_provider_dependency_v1 controller_dep = {"usb.controller", 1, &controller_api};
    assert(host_driver->start(&controller_dep, 1));
    const risc_usb_host_discovery_v1 *host_api = host_driver->capability;
    risc_platform_clock_api_v1 clock = {
        RISC_PLATFORM_CLOCK_API_V1, sizeof(clock), NULL, monotonic_ms, NULL
    };
    risc_provider_dependency_v1 msp_deps[] = {
        {"usb.host", 1, host_api}, {"platform.clock", 1, &clock}
    };
    assert(msp_driver->start(msp_deps, 2));
    const risc_msp_fet_api_v1 *msp = msp_driver->capability;
    assert(msp->poll(NULL, 4));
    risc_msp_fet_probe_v1 probe = {0};
    size_t count = 1;
    assert(msp->snapshot(NULL, &probe, &count) && count == 1);
    uint64_t session = msp->open(NULL, probe.device, RISC_MSP_FET_INTERFACE_SBW);
    assert(session && physical_live == 3 && !msp_driver->quiesce());

    event_kind = 2;
    now_ms += 10;
    assert(msp->poll(NULL, 4));
    /* The MSP observations are identical whether physical release succeeded. */
    assert(msp_driver->quiesce());
    assert(!msp->close(NULL, session));
    assert(!msp->close(NULL, session));
    const unsigned io_before = reads + writes + controls;
    uint8_t out[8];
    assert(msp->execute(NULL, session, 0x15, NULL, 0, out, sizeof(out), 1000) < 0);
    assert(!msp->set_interface(NULL, session, RISC_MSP_FET_INTERFACE_JTAG));
    assert(io_before == reads + writes + controls);

    if (initially_fails) {
        assert(physical_live == 3);
        assert(!host_driver->quiesce());
        assert(physical_live != 0); /* Host, not MSP, still owns quarantined claims. */
        allow_physical_release = true;
    } else {
        assert(physical_live == 0);
    }
    assert(host_driver->quiesce() && physical_live == 0);
    /* Even external proof of host cleanup cannot be observed via MSP close. */
    assert(!msp->close(NULL, session));
    msp_driver->stop();
    host_driver->stop();
    assert(dlclose(msp_library) == 0 && dlclose(host_library) == 0);
    printf("OBSERVED %s: MSP quiescent/old-close=false; physical claims %s; "
           "old-close remains false after host cleanup\n", argv[3],
           initially_fails ? "initially retained" : "released");
    return 0;
}

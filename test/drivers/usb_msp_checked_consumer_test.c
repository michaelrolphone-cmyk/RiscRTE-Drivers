/* Three production providers; only the USB controller and TI probe are fake. */
#define main baseline_msp_fixture_main
#include "usb_msp_test.c"
#undef main
#include "RiscUsbInterruptV1.h"
#include "RiscProgramMspV1.h"

typedef struct { uint64_t token, device; uint8_t iface; bool live; unsigned attempts; uint8_t *rx; uint8_t retained[16]; bool watching; } lease;
static lease leases[128];
static size_t lease_count;
static uint64_t next_lease = 1000;
static unsigned event_count, event_at;
static risc_usb_controller_event_v1 events[64];
static unsigned deny_release; /* bit 0 control, bit 1 data */
static bool deny_data_claim, deny_control, bad_jtag, bad_version;
static unsigned target_syncs, target_releases;
static const risc_driver_v2 *hd, *md, *pd;
static const risc_msp_fet_api_v1 *msp;
static const risc_program_msp_api_v1 *program;
static risc_provider_dependency_v1 hdep, mdeps[2], pdep;

static void event(uint32_t kind, uint64_t physical) {
    assert(event_count < 64);
    events[event_count++] = (risc_usb_controller_event_v1){kind, physical};
}
static int32_t next_event(void *ctx, risc_usb_controller_event_v1 *out) {
    (void)ctx;
    if (event_at == event_count) return 0;
    *out = events[event_at++]; return 1;
}
static bool config(void *ctx, uint64_t device, uint8_t *bytes, size_t *size,
                   uint16_t *vid, uint16_t *pid) {
    assert(device >= 7 && device < 100);
    return configuration(ctx, 7, bytes, size, vid, pid);
}
static lease *find_lease(uint64_t token) {
    for (size_t i = 0; i < lease_count; ++i)
        if (leases[i].token == token) return &leases[i];
    assert(!"unknown physical lease"); return NULL;
}
static unsigned live_count(void) {
    unsigned n = 0;
    for (size_t i = 0; i < lease_count; ++i) n += leases[i].live;
    return n;
}
static bool claim_physical(void *ctx, uint64_t device, uint8_t iface,
                           uint8_t alt, uint64_t *out) {
    (void)ctx; assert(iface < 2 && alt == 0);
    if (iface == 1 && deny_data_claim) return false;
    for (size_t i = 0; i < lease_count; ++i)
        assert(!leases[i].live || leases[i].device != device || leases[i].iface != iface);
    assert(lease_count < 128);
    *out = ++next_lease;
    leases[lease_count++] = (lease){.token=*out, .device=device, .iface=iface, .live=true};
    return true;
}
static bool release_physical(void *ctx, uint64_t token) {
    (void)ctx; lease *l = find_lease(token);
    assert(l->live); /* No second release of an acknowledged physical lease. */
    ++l->attempts;
    if (l->watching) assert(memcmp(l->rx, l->retained, sizeof(l->retained)) == 0);
    if (deny_release & (1u << l->iface)) {
        if (l->rx && !l->watching) {
            memcpy(l->retained, l->rx, sizeof(l->retained)); l->watching = true;
        }
        return false;
    }
    l->live = false; return true;
}
static bool physical_quiesce(void *ctx) { (void)ctx; return live_count() == 0; }
static int32_t controller_control(void *ctx, uint64_t device, uint8_t type,
    uint8_t req, uint16_t val, uint16_t index, uint8_t *bytes, uint16_t size, uint32_t timeout) {
    assert(device >= 7 && device < 100);
    if (deny_control) return -1;
    return control(ctx, 7, type, req, val, index, bytes, size, timeout);
}
static int32_t write_physical(void *ctx, uint64_t token, uint8_t ep,
                              const uint8_t *bytes, size_t size, uint32_t timeout) {
    lease *l = find_lease(token); assert(l->live && l->iface == 1);
    return bulk_write(ctx, 101, ep, bytes, size, timeout);
}
static int32_t read_physical(void *ctx, uint64_t token, uint8_t ep,
                             uint8_t *bytes, size_t size, uint32_t timeout) {
    lease *l = find_lease(token); assert(l->live && l->iface == 1);
    if (size >= 512) l->rx = bytes;
    if (pending == 1 && (pending_fid == 0 || pending_fid == 0x0c ||
                        pending_fid == 0x3a || pending_fid == 0x3c)) {
        uint8_t payload[8] = {0}; size_t n = 0;
        if (pending_fid == 0) { payload[1] = bad_version ? 0 : (3u << 6) | 8u; n = 8; }
        if (pending_fid == 0x0c) { payload[0] = bad_jtag ? 0x89 : 0x99; n = 1; }
        if (pending_fid == 0x3a) { ++target_syncs; payload[0] = 0x80; payload[3] = 0x44; n = 8; }
        if (pending_fid == 0x3c) { ++target_releases; }
        pending = 0; ++reads;
        assert(size >= n + 6);
        return (int32_t)make_frame(0x93, pending_ref, payload, n, bytes);
    }
    return bulk_read(ctx, 101, ep, bytes, size, timeout);
}
static const risc_driver_v2 *load(const char *path, void **library) {
    *library = dlopen(path, RTLD_NOW | RTLD_LOCAL); assert(*library);
    risc_driver_get_v2_fn get = (risc_driver_get_v2_fn)dlsym(*library, "t5_driver_get");
    assert(get); return get(RISC_PROVIDER_DRIVER_ABI_V2);
}
static void poll(void) { now_ms += 10; assert(msp->poll(NULL, 8)); }
static uint64_t probe_at(size_t at) {
    risc_msp_fet_probe_v1 probes[4]; size_t n = 4;
    assert(msp->snapshot(NULL, probes, &n) && at < n); return probes[at].device;
}
static void revoked(uint64_t token) {
    unsigned before = reads + writes + controls;
    uint8_t out[8];
    assert(msp->execute(NULL, token, 0x15, NULL, 0, out, sizeof(out), 1000) < 0);
    assert(!msp->set_interface(NULL, token, RISC_MSP_FET_INTERFACE_JTAG));
    assert(before == reads + writes + controls);
}
static void restart(void) {
    assert(pd->quiesce() && md->quiesce() && hd->quiesce() && !live_count());
    pd->stop(); md->stop(); hd->stop();
    assert(hd->start(&hdep, 1) && md->start(mdeps, 2) && pd->start(&pdep, 1));
}
static uint64_t open_msp(void) {
    uint64_t token = msp->open(NULL, probe_at(0), RISC_MSP_FET_INTERFACE_SBW);
    assert(token); return token;
}

int main(int argc, char **argv) {
    assert(argc == 4 || argc == 5);
    void *hl, *ml, *pl;
    hd = load(argv[1], &hl); md = load(argv[2], &ml); pd = load(argv[3], &pl);
    risc_usb_controller_api_v1 controller = {1, sizeof(controller), NULL,
        next_event, config, claim_physical, release_physical, controller_control,
        read_physical, write_physical, physical_quiesce};
    risc_platform_clock_api_v1 clock = {1, sizeof(clock), NULL, monotonic_ms, NULL};
    hdep = (risc_provider_dependency_v1){"usb.controller", 1, &controller};
    mdeps[0] = (risc_provider_dependency_v1){"usb.host", 1, hd->capability};
    mdeps[1] = (risc_provider_dependency_v1){"platform.clock", 1, &clock};
    pdep = (risc_provider_dependency_v1){"debug.vendor.msp", 1, md->capability};
    assert(hd->start(&hdep, 1) && md->start(mdeps, 2) && pd->start(&pdep, 1));
    msp = md->capability; program = pd->capability;
    event(1, 7); poll();

    if (argc == 5) {
        assert(strcmp(argv[4], "detach-before-target-close") == 0);
        risc_program_msp_target_v1 target = {0};
        uint64_t token = program->open(NULL, probe_at(0), 2, &target); assert(token);
        unsigned sync_before = target_syncs, release_before = target_releases;
        event(2, 7); poll(); assert(!live_count());
        assert(!program->close(NULL, token));
        assert(!pd->quiesce() && !md->quiesce());
        pd->stop(); md->stop();
        assert(!pd->start(&pdep, 1) && !md->start(mdeps, 2));
        assert(target_syncs == sync_before && target_releases == release_before);
        puts("PASS conservative boundary: detach before target-close completion retains programmer/dependency; no reset success inferred");
        return 0; /* Deliberately pinned providers: process end, no dlclose. */
    }

    /* Normal release; receipt consumed once, stale calls perform no I/O. */
    uint64_t first = open_msp(); assert(live_count() == 2);
    assert(msp->close(NULL, first) && !msp->close(NULL, first)); revoked(first);
    assert(md->quiesce() && hd->quiesce());

    /* One interface succeeds, the other remains. Both layers' quiesce/stop
     * may progress physical cleanup but must not swallow the public receipt. */
    uint64_t delayed = open_msp(); size_t data_index = lease_count - 1;
    deny_release = 1;
    assert(!msp->close(NULL, delayed) && live_count() == 1); revoked(delayed);
    unsigned data_attempts = leases[data_index].attempts;
    assert(!md->quiesce() && !hd->quiesce()); md->stop(); hd->stop();
    assert(!md->start(mdeps, 2) && !hd->start(&hdep, 1));
    assert(leases[data_index].attempts == data_attempts);
    deny_release = 0;
    assert(!hd->quiesce() && !live_count());
    assert(!md->quiesce()); md->stop();
    assert(!md->start(mdeps, 2));
    unsigned io = reads + writes + controls;
    assert(msp->close(NULL, delayed) && !msp->close(NULL, delayed));
    assert(io == reads + writes + controls && leases[data_index].attempts == data_attempts);

    /* Detach before close: no target I/O, retain the token/claims until each
     * checked physical release is observed, then deliver exactly one receipt. */
    uint64_t detached = open_msp(); deny_release = 3; event(2, 7); poll();
    assert(live_count() == 2 && !md->quiesce()); revoked(detached);
    io = reads + writes + controls;
    assert(!msp->close(NULL, detached));
    for (unsigned i = 0; i < 8; ++i) assert(!md->quiesce() && !hd->quiesce());
    assert(io == reads + writes + controls && live_count() == 2);
    deny_release = 0;
    /* Host quiesce retains each terminal claim receipt, hence can remain false. */
    assert(!hd->quiesce());
    assert(!md->quiesce() && live_count() == 0);
    assert(msp->close(NULL, detached) && !msp->close(NULL, detached));
    restart(); event(1, 7); poll();
    uint64_t after_restart = open_msp(); assert(after_restart != detached && after_restart != first);
    revoked(detached); assert(msp->close(NULL, after_restart));

    /* Failed opens keep unpublished custody too: partial claim, CDC failure,
     * and protocol rejection. Slot reuse waits for physical acknowledgement. */
    deny_release = 3; deny_data_claim = true;
    assert(!msp->open(NULL, probe_at(0), 2) && live_count() == 1 && !md->quiesce());
    deny_data_claim = false; assert(!msp->open(NULL, probe_at(0), 2));
    deny_release = 0; assert(md->quiesce() && !live_count());
    deny_release = 3; deny_control = true;
    assert(!msp->open(NULL, probe_at(0), 2) && live_count() == 2 && !md->quiesce());
    deny_control = false; deny_release = 0; assert(md->quiesce() && !live_count());
    deny_release = 3; bad_version = true;
    assert(!msp->open(NULL, probe_at(0), 2) && live_count() == 2 && !md->quiesce());
    bad_version = false; deny_release = 0; assert(md->quiesce() && !live_count());

    /* Real program-MSP failed-open quarantine can now drain after detach. */
    risc_program_msp_target_v1 target = {0}; bad_jtag = true; deny_release = 3;
    assert(!program->open(NULL, probe_at(0), 2, &target));
    assert(!pd->quiesce() && !md->quiesce() && live_count() == 2);
    pd->stop(); assert(!pd->start(&pdep, 1));
    event(2, 7); poll(); deny_release = 0;
    assert(!hd->quiesce()); assert(!md->quiesce());
    assert(pd->quiesce() && md->quiesce() && hd->quiesce());
    bad_jtag = false; restart(); event(1, 7); poll();

    /* Normal programmer close latches its successful target reset/release,
     * then retries only transport closure; no programming/reset inferred. */
    uint64_t pt = program->open(NULL, probe_at(0), 2, &target); assert(pt);
    deny_release = 3;
    assert(!program->close(NULL, pt));
    unsigned sync_before = target_syncs, release_before = target_releases;
    assert(!program->close(NULL, pt));
    assert(target_syncs == sync_before && target_releases == release_before);
    uint8_t bytes[2] = {0}; io = reads + writes + controls;
    assert(program->write(NULL, pt, 0x1000, bytes, 2) < 0);
    assert(program->verify(NULL, pt, 0x1000, bytes, 2) < 0);
    assert(io == reads + writes + controls);
    event(2, 7); poll(); deny_release = 0;
    assert(!md->quiesce()); assert(program->close(NULL, pt));
    assert(!program->close(NULL, pt));
    assert(target_syncs == sync_before && target_releases == release_before);
    restart();

    /* Fill every MSP session with detached public receipts. No slot can be
     * reused until its own close caller consumes the acknowledgement. */
    uint64_t tokens[4];
    for (unsigned i = 0; i < 4; ++i) event(1, 7 + i);
    poll();
    for (unsigned i = 0; i < 4; ++i) { tokens[i] = msp->open(NULL, probe_at(i), 2); assert(tokens[i]); }
    for (unsigned i = 0; i < 4; ++i) event(2, 7 + i);
    poll(); assert(!live_count() && !md->quiesce());
    event(1, 11); poll();
    assert(!msp->open(NULL, probe_at(0), 2));
    assert(msp->close(NULL, tokens[2]));
    uint64_t reused = msp->open(NULL, probe_at(0), 2); assert(reused && reused != tokens[2]);
    revoked(tokens[2]); assert(msp->close(NULL, reused));
    for (unsigned i = 0; i < 4; ++i) if (i != 2) assert(msp->close(NULL, tokens[i]));
    restart();
    revoked(first); assert(!program->close(NULL, pt));
    pd->stop(); md->stop(); hd->stop();
    assert(!live_count());
    assert(dlclose(pl) == 0 && dlclose(ml) == 0 && dlclose(hl) == 0);
    puts("PASS actual host + MSP + program-MSP: checked custody, detach, retries, receipts, generation and capacity; fake controller/probe only");
}

/* Qualification composition only: actual retained host + actual upstream class.
 * Physical-controller callbacks reuse upstream fixture behavior, not hardware. */
#define main upstream_class_fixture_main
#include "usb_cp210x_stream_migration_test.c"
#undef main
static bool announced, physical_live;
static int32_t event(void *ctx, risc_usb_controller_event_v1 *out) {
    (void)ctx;
    if (pending_detach) { pending_detach = false; present = false; *out = (risc_usb_controller_event_v1){2, 7}; return 1; }
    if (announced) return 0;
    announced = true; *out = (risc_usb_controller_event_v1){1, 7}; return 1;
}
static bool physical_claim(void *ctx, uint64_t device, uint8_t iface, uint8_t alt, uint64_t *out) {
    assert(!physical_live);
    bool ok = claim(ctx, device, iface, alt, out);
    if (ok) physical_live = true;
    return ok;
}
static bool physical_release(void *ctx, uint64_t token) {
    assert(physical_live);
    if (!checked_release(ctx, token)) return false;
    physical_live = false; return true;
}
static int32_t physical_control(void *ctx, uint64_t device, uint8_t type, uint8_t request,
    uint16_t value, uint16_t index, uint8_t *data, uint16_t length, uint32_t timeout) {
    assert(device == 7 && physical_live);
    return control(ctx, 17, type, request, value, index, data, length, timeout);
}
static bool physical_quiesce(void *ctx) { (void)ctx; return !physical_live; }
int main(int argc, char **argv) {
    assert(argc == 3);
    void *host_lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    void *class_lib = dlopen(argv[2], RTLD_NOW | RTLD_LOCAL);
    assert(host_lib && class_lib);
    risc_driver_get_v2_fn hg = (risc_driver_get_v2_fn)dlsym(host_lib, "t5_driver_get");
    risc_driver_get_v2_fn cg = (risc_driver_get_v2_fn)dlsym(class_lib, "t5_driver_get");
    assert(hg && cg);
    const risc_driver_v2 *h = hg(2), *c = cg(2);
    risc_usb_controller_api_v1 physical = {1, sizeof(physical), NULL, event,
        configuration, physical_claim, physical_release, physical_control,
        read_data, write_data, physical_quiesce};
    risc_provider_dependency_v1 dep = {"usb.controller",1,&physical};
    assert(h->start(&dep,1));
    const risc_usb_host_discovery_v1 *host = h->capability;
    dep = (risc_provider_dependency_v1){"usb.host",1,&host->host};
    assert(c->start(&dep,1));
    const risc_serial_port_inventory_v1 *inventory = c->capability;
    const risc_serial_port_api_v1 *serial = &inventory->discovery.serial;
    risc_serial_device_v1 found[1];size_t count=1;
    assert(inventory->snapshot(found,&count) && count==1);
    uint64_t device=found[0].provider_device;
    assert(device && !physical_live && !controls && !claims);
    /* Failed-open orphan must pin both providers until checked physical release. */
    fail_request=0;fail_release=true;
    assert(!serial->open(device) && physical_live);
    assert(!c->quiesce() && !h->quiesce());
    fail_release=false;fail_request=-1;
    assert(c->quiesce() && !physical_live && h->quiesce());
    fail_request=-1;
    uint64_t session=serial->open(device);
    assert(session && physical_live && !c->quiesce() && !h->quiesce());
    assert(serial->configure(session,115200,8,0,1));
    uint8_t bytes[2];assert(serial->read(session,bytes,2,30)==2);
    assert(serial->write(session,(const uint8_t *)"HI",2,40)==2);
    /* Host may finish physical release, but must keep the checked terminal
     * receipt until this class retries close. No duplicate physical close. */
    int prior_release=releases; fail_request=0;
    assert(!serial->close(session) && physical_live && releases==prior_release);
    assert(!c->quiesce()); /* Attached UART-disable failure is not detach. */
    fail_request=-1;
    fail_release=true;assert(!serial->close(session));
    assert(!c->quiesce() && !h->quiesce() && physical_live);
    fail_release=false;assert(!h->quiesce() && !physical_live);
    int attempts=releases;
    assert(serial->close(session) && releases==attempts);
    assert(c->quiesce() && h->quiesce());
    session=serial->open(device);assert(session && physical_live);
    pending_detach=true;fail_release=true;int before_controls=controls;
    assert(!serial->close(session) && !present && physical_live);
    assert(controls==before_controls && !c->quiesce() && !h->quiesce());
    fail_release=false;assert(serial->close(session) && !physical_live);
    assert(c->quiesce() && h->quiesce());
    c->stop();h->stop();assert(!physical_live);
    assert(dlclose(class_lib)==0 && dlclose(host_lib)==0);
    puts("Actual retained host + upstream CP210x: orphan/failed-close/terminal receipt PASS");
    return 0;
}

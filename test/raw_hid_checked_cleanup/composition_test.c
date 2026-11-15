/* Independent ELF composition qualification. The behavioral fakes are the
 * physical controller boundary and monotonic platform clock. Host/raw/mouse observation tables forward to
 * their real independently compiled providers without implementing semantics. */
#include "RiscUsbInterruptV1.h"
#include "RiscUsbMouseV1.h"
#include "RiscPlatformClockV1.h"
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PHYSICAL_DEVICE UINT64_C(0x501)
#define IFACE 3u
#define ENDPOINT 0x85u
#define MAX_CALLS 128u
static const uint8_t report_descriptor[] = {
    0x05,1, 0x09,2, 0xa1,1, 0x09,1, 0xa1,0,
    0x05,9, 0x19,1, 0x29,3, 0x15,0, 0x25,1,
    0x95,3, 0x75,1, 0x81,2, 0x95,1, 0x75,5, 0x81,1,
    0x05,1, 0x09,0x30, 0x09,0x31, 0x15,0x81, 0x25,0x7f,
    0x75,8, 0x95,2, 0x81,6, 0xc0,0xc0
};
static const uint8_t config[] = {
    9,2,34,0,1,1,0,0x80,50,
    9,4,IFACE,0,1,3,1,2,0,
    9,0x21,0x11,1,0,1,0x22,sizeof(report_descriptor),0,
    7,5,ENDPOINT,3,8,0,1
};
static risc_usb_controller_event_v1 event;
static bool pending_event, reject_release, report_pending;
/* 0=success, 1=false/nonzero custody, 2=false/zero custody. */
static unsigned claim_mode, control_mode;
static unsigned physical_claims, physical_releases, physical_controls, physical_reads;
static uint64_t last_physical_claim, physical_live, next_physical = 0x1000;
static uint64_t physical_release_log[MAX_CALLS];
static uint64_t clock_now;
static int32_t next_event(void *ctx, risc_usb_controller_event_v1 *out) {
    (void)ctx;
    if (!pending_event) return 0;
    *out = event; pending_event = false; return 1;
}
static bool configuration(void *ctx, uint64_t device, uint8_t *bytes,
                          size_t *length, uint16_t *vid, uint16_t *pid) {
    (void)ctx;
    assert(device == PHYSICAL_DEVICE && bytes && length && vid && pid);
    if (*length < sizeof(config)) return false;
    memcpy(bytes, config, sizeof(config)); *length = sizeof(config);
    *vid = 0x1234; *pid = 0x9876; return true;
}
static bool claim(void *ctx, uint64_t device, uint8_t iface,
                  uint8_t alternate, uint64_t *out) {
    (void)ctx;
    assert(device == PHYSICAL_DEVICE && iface == IFACE && alternate == 0 && out);
    ++physical_claims;
    assert(!physical_live);
    if (claim_mode == 2) { *out = 0; return false; }
    physical_live = last_physical_claim = ++next_physical;
    *out = physical_live; return claim_mode == 0;
}
static bool release(void *ctx, uint64_t token) {
    (void)ctx;
    assert(token && token == physical_live && physical_releases < MAX_CALLS);
    physical_release_log[physical_releases++] = token;
    if (reject_release) return false;
    physical_live = 0; return true;
}
static int32_t control(void *ctx, uint64_t device, uint8_t type, uint8_t request,
                       uint16_t value, uint16_t index, uint8_t *out,
                       uint16_t length, uint32_t timeout) {
    (void)ctx;
    assert(device == PHYSICAL_DEVICE && physical_live && index == IFACE && timeout == 100);
    ++physical_controls;
    if (control_mode == 1) return -1;
    if (type == 0x81 && request == 6) {
        assert(value == 0x2200 && out && length == sizeof(report_descriptor));
        memcpy(out, report_descriptor, length);
        if (control_mode == 2) return length - 1;
        if (control_mode == 3) return length + 1;
        return length;
    }
    assert(type == 0x21 && request == 0x0b && value <= 1 && !out && !length);
    return control_mode == 3 ? 1 : 0;
}
static int32_t bulk_read(void *ctx, uint64_t token, uint8_t endpoint,
                         uint8_t *out, size_t length, uint32_t timeout) {
    (void)ctx; (void)token; (void)endpoint; (void)out; (void)length; (void)timeout;
    assert(!"HID must not issue bulk reads"); return -1;
}
static int32_t bulk_write(void *ctx, uint64_t token, uint8_t endpoint,
                          const uint8_t *out, size_t length, uint32_t timeout) {
    (void)ctx; (void)token; (void)endpoint; (void)out; (void)length; (void)timeout;
    assert(!"HID must not issue bulk writes"); return -1;
}
static bool controller_quiet(void *ctx) { (void)ctx; return !physical_live; }
static int32_t interrupt_read(void *ctx, uint64_t token, uint8_t endpoint,
                              uint8_t *out, size_t capacity, uint32_t timeout) {
    (void)ctx;
    assert(token == physical_live && token == last_physical_claim && endpoint == ENDPOINT);
    assert(out && capacity >= 8 && capacity <= 64 && timeout && timeout <= 100);
    ++physical_reads;
    if (!report_pending) return 0;
    report_pending = false; out[0] = 1; out[1] = 7; out[2] = (uint8_t)-3; return 3;
}
static const risc_usb_controller_interrupt_v1 controller = {
    {1, sizeof(controller), 0, next_event, configuration, claim, release,
     control, bulk_read, bulk_write, controller_quiet}, interrupt_read
};
static uint64_t monotonic(void *ctx) { (void)ctx; return clock_now; }
static void sleep_ms(void *ctx, uint32_t ms) { (void)ctx; assert(ms == 1); clock_now += ms; }
static const risc_platform_clock_api_v1 clock_api = {1, sizeof(clock_api), 0, monotonic, sleep_ms};

static const risc_driver_v2 *host_driver, *raw_driver, *mouse_driver;
static const risc_usb_host_discovery_v1 *host;
static const risc_usb_hid_api_v1 *raw;
static const risc_usb_mouse_api_v1 *mouse;
static risc_usb_host_discovery_v1 observed_host;
static risc_usb_hid_api_v1 observed_raw;
static unsigned host_claim_calls, checked_calls, checked_control_calls, raw_calls, raw_close_calls;
static uint64_t last_host_claim, checked_log[MAX_CALLS], control_claim_log[MAX_CALLS];
static uint64_t last_raw_token, raw_close_log[MAX_CALLS];
static bool observe_claim(void *ctx, uint64_t device, uint8_t iface,
                          uint8_t alternate, uint64_t *out) {
    ++host_claim_calls;
    bool ok = host->host.claim(ctx, device, iface, alternate, out);
    last_host_claim = *out; return ok;
}
static bool observe_checked(void *ctx, uint64_t token) {
    assert(checked_calls < MAX_CALLS);
    checked_log[checked_calls++] = token;
    return host->release_checked(ctx, token);
}
static int32_t observe_control(void *ctx, uint64_t token, uint8_t type, uint8_t request,
                               uint16_t value, uint16_t index, uint8_t *out,
                               uint16_t length, uint32_t timeout) {
    assert(checked_control_calls < MAX_CALLS);
    control_claim_log[checked_control_calls++] = token;
    assert(token == last_host_claim && index == IFACE);
    assert(type == 0x81 || type == 0x21);
    return host->control_claim(ctx, token, type, request, value, index, out, length, timeout);
}
static void forbidden_legacy_release(void *ctx, uint64_t token) {
    (void)ctx; (void)token; assert(!"legacy void release is forbidden");
}
static int32_t forbidden_legacy_control(void *ctx, uint64_t token, uint8_t type,
    uint8_t req, uint16_t value, uint16_t index, uint8_t *out, uint16_t n, uint32_t timeout) {
    (void)ctx; (void)token; (void)type; (void)req; (void)value; (void)index;
    (void)out; (void)n; (void)timeout; assert(!"device-token control is forbidden"); return -1;
}
static bool raw_scan(void *ctx, size_t n) { ++raw_calls; return raw->scan(ctx,n); }
static bool raw_interfaces(void *ctx, risc_usb_hid_interface_v1 *out, size_t *n) {
    ++raw_calls; return raw->interfaces(ctx,out,n);
}
static uint64_t raw_open(void *ctx, uint64_t device, uint8_t iface, uint8_t alt) {
    ++raw_calls; last_raw_token = raw->open(ctx,device,iface,alt); return last_raw_token;
}
static bool raw_descriptor(void *ctx, uint64_t token, uint8_t *out, size_t *n) {
    ++raw_calls; return raw->report_descriptor(ctx,token,out,n);
}
static bool raw_boot(void *ctx, uint64_t token, bool boot) {
    ++raw_calls; return raw->set_boot_protocol(ctx,token,boot);
}
static int32_t raw_read(void *ctx, uint64_t token, uint8_t *out, size_t n, uint32_t timeout) {
    ++raw_calls; return raw->read(ctx,token,out,n,timeout);
}
static bool raw_present(void *ctx, uint64_t token) { ++raw_calls; return raw->present(ctx,token); }
static bool raw_close(void *ctx, uint64_t token) {
    ++raw_calls; assert(raw_close_calls < MAX_CALLS);
    raw_close_log[raw_close_calls++] = token; return raw->close(ctx,token);
}
static const risc_driver_v2 *load(const char *path, void **handle) {
    *handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!*handle) fprintf(stderr,"dlopen %s: %s\n",path,dlerror());
    assert(*handle);
    risc_driver_get_v2_fn get = (risc_driver_get_v2_fn)dlsym(*handle,"t5_driver_get");
    assert(get && !get(1));
    const risc_driver_v2 *driver = get(2);
    assert(driver && driver->abi_version == 2 && driver->quiesce);
    return driver;
}
static void queue(uint32_t kind) {
    assert(!pending_event); event = (risc_usb_controller_event_v1){kind, PHYSICAL_DEVICE};
    pending_event = true;
}
static void bind_raw(void) {
    observed_host = *host;
    observed_host.host.claim = observe_claim;
    observed_host.host.release = forbidden_legacy_release;
    observed_host.host.control = forbidden_legacy_control;
    observed_host.release_checked = observe_checked;
    observed_host.control_claim = observe_control;
    risc_provider_dependency_v1 dep = {"usb.host",1,&observed_host};
    assert(raw_driver->start(&dep,1));
}
static uint64_t discover(void) {
    assert(raw->scan(raw->context,16));
    risc_usb_hid_interface_v1 item[16]; size_t n = 16;
    assert(raw->interfaces(raw->context,item,&n) && n == 1);
    assert(item[0].device && item[0].interface_number == IFACE && item[0].protocol == 2);
    return item[0].device;
}
static uint64_t open_raw(uint64_t device) {
    uint64_t token = raw->open(raw->context,device,IFACE,0);
    assert(token && last_host_claim && last_host_claim != last_physical_claim);
    return token;
}
static void denied_raw(uint64_t token) {
    unsigned controls_before = physical_controls, reads_before = physical_reads;
    uint8_t bytes[512]; size_t n = sizeof(bytes);
    assert(!raw->present(raw->context,token));
    assert(!raw->report_descriptor(raw->context,token,bytes,&n));
    assert(!raw->set_boot_protocol(raw->context,token,true));
    assert(raw->read(raw->context,token,bytes,64,1) < 0);
    assert(physical_controls == controls_before && physical_reads == reads_before);
}
static void gate_tables(void) {
    assert(!raw_driver->start(0,0));
    assert(!raw_driver->start(0,1));
    risc_provider_dependency_v1 invalid[] = {{"usb.host",1,host},{"usb.host",1,host}};
    assert(!raw_driver->start(invalid,0));
    assert(!raw_driver->start(invalid,2));
    invalid[0].api = 0; assert(!raw_driver->start(invalid,1));
    invalid[0].api = host; invalid[0].capability_id = 0;
    assert(!raw_driver->start(invalid,1));
    invalid[0].capability_id = "usb.other"; assert(!raw_driver->start(invalid,1));
    invalid[0].capability_id = "usb.host"; invalid[0].api_version = 0;
    assert(!raw_driver->start(invalid,1));
    /* Full backing makes the shortened-prefix mutation fail this exact
     * assertion rather than depending on undefined access to a short object. */
    risc_usb_host_discovery_v1 legacy_copy = *host;
    legacy_copy.host.struct_size = sizeof(risc_usb_host_interrupt_v1);
    risc_provider_dependency_v1 legacy_declared = {"usb.host",1,&legacy_copy};
    assert(!raw_driver->start(&legacy_declared,1));
    /* Every declared byte length has a genuinely short allocation, so an
     * early tail load is detected by ASan instead of hidden in a full object. */
    for (size_t n = 2 * sizeof(uint32_t); n < sizeof(*host); ++n) {
        void *short_table = malloc(n); assert(short_table);
        memcpy(short_table,host,n);
        uint32_t size = (uint32_t)n;
        memcpy((uint8_t *)short_table + sizeof(uint32_t),&size,sizeof(size));
        risc_provider_dependency_v1 dep = {"usb.host",1,short_table};
        assert(!raw_driver->start(&dep,1));
        free(short_table);
    }
    for (unsigned missing = 0; missing < 7; ++missing) {
        risc_usb_host_discovery_v1 copy = *host;
        switch (missing) {
            case 0: copy.release_checked = 0; break;
            case 1: copy.control_claim = 0; break;
            case 2: copy.interrupt_read = 0; break;
            case 3: copy.poll = 0; break;
            case 4: copy.devices = 0; break;
            case 5: copy.host.claim = 0; break;
            case 6: copy.host.configuration = 0; break;
        }
        risc_provider_dependency_v1 dep = {"usb.host",1,&copy};
        assert(!raw_driver->start(&dep,1));
    }
    risc_usb_host_discovery_v1 copy = *host;
    copy.host.api_version = 2;
    risc_provider_dependency_v1 dep = {"usb.host",1,&copy};
    assert(!raw_driver->start(&dep,1));
    copy = *host; copy.host.release = 0; copy.host.control = 0;
    assert(raw_driver->start(&dep,1)); /* old callbacks are not dependencies */
    assert(raw_driver->quiesce()); raw_driver->stop();
    assert(!physical_claims && !physical_controls && !physical_reads && !physical_releases);
}
static void raw_retry(void) {
    bind_raw(); queue(1); uint64_t device = discover(); uint64_t token = open_raw(device);
    const uint64_t custody = last_host_claim, physical = last_physical_claim;
    assert(token != custody); /* this scenario deliberately has disjoint test tokens */
    uint8_t bytes[512]; size_t n = sizeof(bytes);
    assert(raw->report_descriptor(0,token,bytes,&n) && n == sizeof(report_descriptor));
    assert(!memcmp(bytes,report_descriptor,n));
    assert(raw->set_boot_protocol(0,token,true));
    report_pending = true; assert(raw->read(0,token,bytes,64,1) == 3);
    assert(control_claim_log[0] == custody && control_claim_log[1] == custody);
    reject_release = true; assert(!raw->close(0,token));
    assert(checked_calls == 1 && checked_log[0] == custody && physical_live == physical);
    denied_raw(token); denied_raw(UINT64_MAX); denied_raw(custody);
    assert(!raw->close(0,UINT64_MAX) && !raw->close(0,custody));
    assert(!raw->open(0,device,IFACE,0) && physical_claims == 1);
    assert(!raw_driver->quiesce() && checked_calls == 1);
    raw_driver->stop(); assert(checked_calls == 1);
    risc_provider_dependency_v1 dep = {"usb.host",1,&observed_host};
    assert(!raw_driver->start(&dep,1));
    /* Graph cleanup may finish physical work but must leave the receipt for
     * this exact checked caller. It must not make raw quiescence true. */
    reject_release = false; assert(!host_driver->quiesce());
    assert(!physical_live && !raw_driver->quiesce());
    unsigned releases_before_ack = physical_releases;
    assert(raw->close(0,token));
    assert(checked_calls == 2 && checked_log[1] == custody);
    assert(physical_releases == releases_before_ack);
    denied_raw(token); assert(!raw->close(0,token));
    uint64_t newer = open_raw(device); assert(newer != token);
    assert(!raw->close(0,token) && raw->present(0,newer));
    assert(raw->close(0,newer));
    assert(physical_release_log[0] == physical && physical_release_log[1] == physical);
}
static void orphan(bool zero, bool immediate) {
    bind_raw(); queue(1); uint64_t device = discover();
    claim_mode = zero ? 2 : 1; reject_release = !immediate;
    assert(raw->open(0,device,IFACE,0) == 0);
    assert(physical_claims == 1);
    if (zero || immediate) {
        assert(raw_driver->quiesce() && !physical_live);
        assert(checked_calls == (zero ? 0u : 1u));
    } else {
        const uint64_t custody = last_host_claim;
        assert(custody && checked_calls == 1 && checked_log[0] == custody);
        /* Unpublished raw identity 1 and the host custody token authorize no
         * raw I/O or public close. Cleanup belongs to quiesce only. */
        denied_raw(1); denied_raw(custody);
        assert(!raw->close(0,1) && !raw->close(0,custody));
        assert(!raw->open(0,device,IFACE,0) && physical_claims == 1);
        assert(!raw_driver->quiesce() && checked_calls == 2 && checked_log[1] == custody);
        reject_release = false;
        assert(!host_driver->quiesce()); /* retains exact checked receipt */
        unsigned releases = physical_releases;
        assert(raw_driver->quiesce());
        assert(checked_calls == 3 && checked_log[2] == custody && physical_releases == releases);
    }
    claim_mode = 0; reject_release = false;
    uint64_t token = open_raw(device); assert(raw->close(0,token));
}
/* PR41 is not the required cohort. Preserve its pre-PR40 custody gap as an
 * explicit negative witness: a raw consumer cannot retain a token the host
 * never returned. Do not fake that missing contract in the observation seam. */
static void pr41_custody_gap(bool reject) {
    bind_raw(); queue(1); uint64_t device = discover();
    claim_mode = 1; reject_release = reject;
    assert(raw->open(0,device,IFACE,0) == 0);
    assert(physical_claims == 1 && physical_live);
    assert(last_host_claim == 0 && checked_calls == 0 && physical_releases == 0);
    assert(raw_driver->quiesce());
    assert(!host_driver->quiesce());
    assert(physical_live && physical_releases == 0);
}
static void control_failures(void) {
    bind_raw(); queue(1); uint64_t device = discover(); uint64_t token = open_raw(device);
    uint8_t bytes[512];
    for (control_mode = 1; control_mode <= 3; ++control_mode) {
        size_t n = sizeof(bytes);
        assert(!raw->report_descriptor(0,token,bytes,&n));
        if (control_mode != 2) assert(!raw->set_boot_protocol(0,token,true));
        assert(raw->present(0,token));
    }
    control_mode = 0;
    size_t n = sizeof(bytes); assert(raw->report_descriptor(0,token,bytes,&n));
    assert(raw->set_boot_protocol(0,token,false));
    unsigned before = physical_controls;
    assert(host->control_claim(0,last_host_claim,0x81,6,0x2200,IFACE + 1,bytes,sizeof(report_descriptor),100) < 0);
    assert(host->control_claim(0,UINT64_MAX,0x81,6,0x2200,IFACE,bytes,sizeof(report_descriptor),100) < 0);
    assert(host->control_claim(0,last_host_claim,0x82,6,0x2200,IFACE,bytes,sizeof(report_descriptor),100) < 0);
    assert(physical_controls == before);
    assert(raw->close(0,token));
}
static void raw_detach(void) {
    bind_raw(); queue(1); uint64_t device = discover(); uint64_t token = open_raw(device);
    uint64_t custody = last_host_claim;
    queue(2); assert(raw->scan(0,16)); denied_raw(token);
    assert(!raw->open(0,device,IFACE,0));
    reject_release = true; assert(!raw->close(0,token));
    assert(!raw_driver->quiesce());
    reject_release = false; assert(raw->close(0,token));
    assert(checked_calls == 2 && checked_log[0] == custody && checked_log[1] == custody);
    queue(1); uint64_t newer_device = discover(); assert(newer_device != device);
    assert(!raw->open(0,device,IFACE,0));
    uint64_t newer = open_raw(newer_device); assert(newer != token);
    assert(!raw->close(0,token) && raw->close(0,newer));
}
static uint64_t start_mouse(void) {
    bind_raw();
    observed_raw = *raw;
    observed_raw.scan = raw_scan; observed_raw.interfaces = raw_interfaces;
    observed_raw.open = raw_open; observed_raw.report_descriptor = raw_descriptor;
    observed_raw.set_boot_protocol = raw_boot; observed_raw.read = raw_read;
    observed_raw.present = raw_present; observed_raw.close = raw_close;
    risc_provider_dependency_v1 deps[] = {{"usb.hid",1,&observed_raw},{"platform.clock",1,&clock_api}};
    assert(mouse_driver->start(deps,2));
    uint64_t sub = mouse->subscribe(0,0); assert(sub);
    risc_usb_mouse_state_v1 state[4]; size_t n = 4;
    assert(mouse->snapshot(0,sub,state,&n) && n == 0);
    queue(1); return sub;
}
static void connect_mouse(uint64_t sub) {
    report_pending = true; assert(mouse->poll(0,4));
    risc_usb_mouse_event_v1 ev;
    assert(mouse->next(0,sub,&ev) == 1 && ev.kind == RISC_USB_MOUSE_CONNECTED);
    assert(mouse->next(0,sub,&ev) == 1 && ev.kind == RISC_USB_MOUSE_REPORT);
    assert(ev.x == 7 && ev.y == -3 && ev.pressed == 1 && ev.state.buttons == 1);
    assert(ev.state.interface_number == IFACE && !ev.wheel && !ev.pan);
    assert(mouse->next(0,sub,&ev) == 0);
    assert(last_raw_token && last_host_claim && physical_live);
}
static bool mouse_case(const char *name) {
    uint64_t sub = start_mouse();
    if (!strcmp(name,"mouse-setup-failure")) {
        control_mode = 1; reject_release = true;
        assert(!mouse->poll(0,4));
        assert(mouse->status(0) == RISC_USB_MOUSE_CLOSING);
        assert(last_raw_token && raw_close_calls == 1);
        denied_raw(last_raw_token);
        unsigned calls = raw_calls;
        reject_release = false; control_mode = 0;
        assert(mouse->poll(0,4));
        assert(raw_calls == calls + 1 && raw_close_calls == 2);
        assert(raw_close_log[0] == last_raw_token && raw_close_log[1] == last_raw_token);
        assert(mouse->unsubscribe(0,sub)); return false;
    }
    connect_mouse(sub);
    const uint64_t token = last_raw_token, custody = last_host_claim;
    reject_release = true;
    if (!strcmp(name,"mouse-detach-retry")) {
        queue(2); assert(!mouse->poll(0,4));
        assert(mouse->status(0) == RISC_USB_MOUSE_CLOSING && raw_close_calls == 1);
        denied_raw(token);
        unsigned calls = raw_calls;
        reject_release = false; assert(mouse->poll(0,4));
        assert(raw_calls == calls + 1 && raw_close_calls == 2);
        assert(raw_close_log[0] == token && raw_close_log[1] == token);
        assert(checked_log[0] == custody && checked_log[1] == custody);
        risc_usb_mouse_state_v1 states[4]; size_t n = 4;
        assert(mouse->snapshot(0,sub,states,&n) && n == 0);
        assert(mouse->unsubscribe(0,sub)); return false;
    }
    assert(mouse->unsubscribe(0,sub));
    assert(!mouse_driver->quiesce());
    assert(raw_close_calls == 1 && checked_calls == 1);
    assert(raw_close_log[0] == token && checked_log[0] == custody);
    denied_raw(token); assert(!raw_driver->quiesce());
    unsigned calls = raw_calls;
    assert(!mouse->poll(0,4) && raw_calls == calls); /* stopping is local */
    if (!strcmp(name,"mouse-retained")) {
        assert(!mouse_driver->quiesce()); assert(!mouse_driver->quiesce());
        assert(mouse->status(0) == RISC_USB_MOUSE_RETAINED);
        assert(raw_close_calls == 3 && checked_calls == 3);
        for (unsigned i = 0; i < 3; ++i)
            assert(raw_close_log[i] == token && checked_log[i] == custody);
        calls = raw_calls;
        for (unsigned i = 0; i < 4; ++i) {
            assert(!mouse->poll(0,4)); assert(!mouse_driver->quiesce()); mouse_driver->stop();
            assert(mouse->status(0) == RISC_USB_MOUSE_RETAINED && raw_calls == calls);
        }
        assert(physical_live && !raw_driver->quiesce());
        return true; /* simulated reboot boundary: intentionally still mapped */
    }
    assert(!strcmp(name,"mouse-quiesce-retry"));
    reject_release = false; assert(mouse_driver->quiesce());
    assert(raw_close_calls == 2 && raw_close_log[1] == token);
    assert(checked_calls == 2 && checked_log[1] == custody && !physical_live);
    return false;
}
int main(int argc, char **argv) {
    assert(argc == 5);
    void *host_elf, *raw_elf, *mouse_elf;
    host_driver = load(argv[1],&host_elf); raw_driver = load(argv[2],&raw_elf);
    mouse_driver = load(argv[3],&mouse_elf);
    host = host_driver->capability; raw = raw_driver->capability; mouse = mouse_driver->capability;
    assert(host->host.struct_size >= sizeof(*host) && host->release_checked && host->control_claim);
    assert(!strcmp(raw_driver->driver_id,"usb-hid") && !strcmp(raw_driver->capability_id,"usb.hid"));
    assert(raw->struct_size == sizeof(*raw) && raw->api_version == 1);
    risc_provider_dependency_v1 dep = {"usb.controller",1,&controller};
    assert(host_driver->start(&dep,1));
    const char *name = argv[4]; bool retained = false, mouse_started = false;
    if (!strcmp(name,"table-gates")) gate_tables();
    else if (!strcmp(name,"raw-retry")) raw_retry();
    else if (!strcmp(name,"orphan-retained")) orphan(false,false);
    else if (!strcmp(name,"orphan-immediate")) orphan(false,true);
    else if (!strcmp(name,"claim-zero")) orphan(true,false);
    else if (!strcmp(name,"control-failures")) control_failures();
    else if (!strcmp(name,"raw-detach")) raw_detach();
    else if (!strcmp(name,"pr41-orphan-retained-gap")) { pr41_custody_gap(true); retained = true; }
    else if (!strcmp(name,"pr41-orphan-immediate-gap")) { pr41_custody_gap(false); retained = true; }
    else { mouse_started = true; retained = mouse_case(name); }
    if (!retained) {
        if (mouse_started) { assert(mouse_driver->quiesce()); mouse_driver->stop(); }
        assert(raw_driver->quiesce()); raw_driver->stop();
        assert(host_driver->quiesce()); host_driver->stop(); assert(!physical_live);
        assert(!dlclose(mouse_elf) && !dlclose(raw_elf) && !dlclose(host_elf));
    }
    printf("%s %s%s\n",!strncmp(name,"pr41-orphan-",12) ? "EXPECTED_HOST_CUSTODY_GAP" : "PASS",
           name,retained ? " (providers remain mapped)" : "");
    return 0;
}

#include "RiscUsbHidV1.h"
#include "RiscUsbInterruptV1.h"
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

static const uint8_t gamepad_descriptor[] = {
    0x05,0x01, 0x09,0x05, 0xa1,0x01,
    0x05,0x09, 0x19,0x01, 0x29,0x02,
    0x15,0x00, 0x25,0x01, 0x75,0x01, 0x95,0x02, 0x81,0x02,
    0x75,0x06, 0x95,0x01, 0x81,0x03,
    0x05,0x01, 0x09,0x30, 0x09,0x31,
    0x15,0x81, 0x25,0x7f, 0x75,0x08, 0x95,0x02, 0x81,0x02,
    0xc0
};
static const uint8_t configuration_descriptor[] = {
    9,2,65,0,3,1,0,0x80,50,
    9,4,0,0,1,3,1,1,0,
    9,0x21,0x11,0x01,0,1,0x22,63,0,
    7,5,0x81,3,8,0,10,
    9,4,1,0,1,3,0,0,0,
    9,0x21,0x11,0x01,0,1,0x22,sizeof(gamepad_descriptor),0,
    7,5,0x82,3,8,0,10,
    /* Unrelated vendor function with a shortened interface record.  Its
     * private layout must not poison either complete HID interface above. */
    6,4,2,0,0,0xff
};
static bool attached = true, claimed[2];
static unsigned malformed, burst, burst_read;
static unsigned pad_burst, pad_burst_read, pad_reads;
static unsigned keyboard_reports, gamepad_reports, releases;
static bool host_poll(void *ctx, size_t max, size_t *processed) {
    (void)ctx;
    if (!max || !processed) return false;
    *processed = 0; return true;
}
static bool host_devices(void *ctx, uint64_t *devices, size_t *count) {
    (void)ctx;
    if (!count) return false;
    size_t n = attached ? 1u : 0u;
    if (*count < n || (n && !devices)) { *count = n; return false; }
    if (n) devices[0] = 42;
    *count = n; return true;
}
static bool host_configuration(void *ctx, uint64_t token, uint8_t *bytes,
                               size_t *len, uint16_t *vid, uint16_t *pid) {
    (void)ctx;
    if (malformed == 8) return false; // transport failure retains last snapshot
    if (!attached || token != 42 || !bytes || !len || !vid || !pid ||
        *len < sizeof(configuration_descriptor)) return false;
    memcpy(bytes, configuration_descriptor, sizeof(configuration_descriptor));
    // One physical composite device, with corruption in either sibling.
    if (malformed == 1) bytes[48] = 0;       // sibling HID bNumDescriptors
    if (malformed == 2) bytes[54] = 0xf2;    // sibling reserved endpoint bits
    if (malformed == 3) bytes[43] = 0;       // broken framing in sibling
    if (malformed == 4) bytes[23] = 0;       // first interface invalid; second survives
    if (malformed == 5) bytes[18] = 0;       // first scope framing lost; cannot find sibling
    if (malformed == 6) bytes[43] = 255;     // sibling length beyond buffer
    if (malformed == 7) bytes[56] = 65;      // sibling oversized report packet
    *len = sizeof(configuration_descriptor); *vid = 0x1234; *pid = 0x9876;
    return true;
}
static bool host_claim(void *ctx, uint64_t token, uint8_t iface,
                       uint8_t alt, uint64_t *handle) {
    (void)ctx;
    if (!attached || token != 42 || iface >= 2 || alt || !handle ||
        claimed[iface]) return false;
    claimed[iface] = true;
    *handle = 100u + iface;
    return true;
}
static void host_release(void *ctx, uint64_t handle) {
    (void)ctx;
    assert(handle >= 100 && handle < 102 && claimed[handle - 100]);
    claimed[handle - 100] = false; ++releases;
}
static int32_t host_control(void *ctx, uint64_t token, uint8_t type,
                            uint8_t request, uint16_t value, uint16_t iface,
                            uint8_t *payload, uint16_t len, uint32_t timeout) {
    (void)ctx;
    if (token != 42 || !timeout) return -1;
    if (type == 0x21 && request == 0x0b && iface == 0 && !value && !len)
        return 0;
    if (type == 0x81 && request == 0x06 && value == 0x2200 && iface == 1 &&
        len == sizeof(gamepad_descriptor) && payload && claimed[1]) {
        memcpy(payload, gamepad_descriptor, len);
        return len;
    }
    return -1;
}
static int32_t bulk_read(void *ctx, uint64_t claim, uint8_t ep, uint8_t *dst,
                         size_t cap, uint32_t ms) {
    (void)ctx; (void)claim; (void)ep; (void)dst; (void)cap; (void)ms;
    return -1;
}
static int32_t bulk_write(void *ctx, uint64_t claim, uint8_t ep,
                          const uint8_t *src, size_t len, uint32_t ms) {
    (void)ctx; (void)claim; (void)ep; (void)src; (void)len; (void)ms;
    return -1;
}
static int32_t interrupt_read(void *ctx, uint64_t claim, uint8_t ep,
                              uint8_t *dst, size_t cap, uint32_t ms) {
    (void)ctx;
    assert(ms && dst && cap >= 8 && attached);
    if (claim == 100 && ep == 0x81 && claimed[0]) {
        if (burst && burst_read < 4) {
            const uint8_t usages[] = {0, 5, 0, 4};
            memset(dst, 0, 8); dst[2] = usages[burst_read++]; return 8;
        }
        if (keyboard_reports++) return 0;
        const uint8_t report[8] = {0,0,4,0,0,0,0,0};
        memcpy(dst, report, 8);
        return 8;
    }
    if (claim == 101 && ep == 0x82 && claimed[1]) {
        ++pad_reads;
        if (pad_burst && pad_burst_read < 4) {
            const uint8_t reports[4][3] = {{0,0,0}, {2,129,127}, {0,0,0}, {1,127,129}};
            memcpy(dst, reports[pad_burst_read++], 3); return 3;
        }
        if (gamepad_reports++) return 0;
        const uint8_t report[3] = {1,127,129};
        memcpy(dst, report, sizeof(report));
        return sizeof(report);
    }
    return -1;
}
static const risc_usb_host_interrupt_v1 fake_host = {
    {{RISC_USB_HOST_API_V1, sizeof(risc_usb_host_interrupt_v1), NULL,
      host_configuration, host_claim, host_release, host_control,
      bulk_read, bulk_write}, host_poll, host_devices},
    interrupt_read
};
static const risc_driver_v2 *load(const char *file, void **module) {
    *module = dlopen(file, RTLD_NOW);
    assert(*module);
    risc_driver_get_v2_fn get = (risc_driver_get_v2_fn)dlsym(*module, "t5_driver_get");
    assert(get && !get(1));
    const risc_driver_v2 *driver = get(2);
    assert(driver && driver->capability && driver->quiesce);
    return driver;
}
int main(int argc, char **argv) {
    assert(argc == 4);
    void *hid_module = NULL, *keyboard_module = NULL, *gamepad_module = NULL;
    const risc_driver_v2 *generic = load(argv[1], &hid_module);
    const risc_driver_v2 *keyboard = load(argv[2], &keyboard_module);
    const risc_driver_v2 *gamepad = load(argv[3], &gamepad_module);
    assert(strcmp(generic->capability_id, "usb.hid") == 0);
    assert(strcmp(keyboard->capability_id, "usb.hid.keyboard") == 0);
    assert(strcmp(gamepad->capability_id, "usb.hid.gamepad") == 0);
    risc_provider_dependency_v1 host_dep = {"usb.host", 1, &fake_host.discovery.host};
    assert(generic->start(&host_dep, 1));
    const risc_usb_hid_api_v1 *hid = generic->capability;
    for (malformed = 0; malformed <= 7; ++malformed) {
        assert(hid->scan(hid->context, 4));
        risc_usb_hid_interface_v1 found[4] = {{0}};
        size_t count = 4;
        assert(hid->interfaces(hid->context, found, &count));
        assert(count == (malformed == 0 ? 2u : malformed == 5 ? 0u : 1u));
        if (count == 1) {
            assert(found[0].interface_number == (malformed == 4 ? 1 : 0));
            const uint64_t session = hid->open(hid->context, 42, found[0].interface_number, 0);
            assert(session && hid->close(hid->context, session));
        }
    }
    malformed = 0;
    assert(hid->scan(hid->context, 4));
    malformed = 8;
    assert(!hid->scan(hid->context, 4));
    risc_usb_hid_interface_v1 preserved[4] = {{0}};
    size_t preserved_count = 4;
    assert(hid->interfaces(hid->context, preserved, &preserved_count) && preserved_count == 2);
    malformed = 0; releases = 0;
    risc_provider_dependency_v1 hid_dep = {"usb.hid", 1, hid};
    assert(keyboard->start(&hid_dep, 1));
    assert(gamepad->start(&hid_dep, 1));
    const risc_usb_keyboard_api_v1 *keys = keyboard->capability;
    const risc_usb_gamepad_api_v1 *pads = gamepad->capability;
    uint64_t key_sub = keys->subscribe(keys->context, 42);
    uint64_t pad_sub = pads->subscribe(pads->context, 42);
    assert(key_sub && pad_sub);
    /* An activated HID stack with an empty bus is a normal steady state, not
     * an acquisition or polling failure. */
    attached = false;
    assert(keys->poll(keys->context, 4));
    assert(pads->poll(pads->context, 4));
    attached = true;
    assert(keys->poll(keys->context, 4));
    assert(pads->poll(pads->context, 4));
    risc_usb_keyboard_event_v1 key_event = {0};
    assert(keys->next(keys->context, key_sub, &key_event) == 1 &&
           key_event.kind == 1 && key_event.device == 42);
    assert(keys->next(keys->context, key_sub, &key_event) == 1 &&
           key_event.kind == 3 && key_event.usage == 4);
    risc_usb_gamepad_event_v1 pad_event = {0};
    assert(pads->next(pads->context, pad_sub, &pad_event) == 1 &&
           pad_event.kind == 1 && pad_event.state.connected);
    assert((pad_event.state.buttons & 1u) &&
           pad_event.state.x == 32767 && pad_event.state.y == -32767);
    risc_usb_keyboard_state_v1 key_state[4] = {{0}};
    risc_usb_gamepad_state_v1 pad_state[4] = {{0}};
    size_t n = 4;
    assert(keys->snapshot(keys->context, key_state, &n) && n == 1 &&
           key_state[0].keys[0] == 4);
    n = 4;
    assert(pads->snapshot(pads->context, pad_state, &n) && n == 1 &&
           pad_state[0].buttons == 1);
    burst = 1;
    assert(keys->poll(keys->context, 4) && burst_read == 4);
    const unsigned kinds[] = {4, 3, 4, 3}, usages[] = {4, 5, 5, 4};
    for (unsigned i = 0; i < 4; ++i) {
        assert(keys->next(keys->context, key_sub, &key_event) == 1);
        assert(key_event.kind == kinds[i] && key_event.usage == usages[i]);
    }
    burst = 0;
    pad_burst = 1;
    unsigned reads_before = pad_reads;
    assert(pads->poll(pads->context, 2) && pad_burst_read == 2);
    assert(pad_reads == reads_before + 2); /* Honor a smaller work budget. */
    assert(pads->poll(pads->context, 4) && pad_burst_read == 4);
    assert(pad_reads == reads_before + 5); /* Stop after the first idle read. */
    /* Four past states collapse to the controller's current state. */
    assert(pads->next(pads->context, pad_sub, &pad_event) == 1);
    assert(pad_event.kind == 3 && pad_event.state.buttons == 1);
    assert(pad_event.state.x == 32767 && pad_event.state.y == -32767);
    assert(pads->next(pads->context, pad_sub, &pad_event) == 0);
    pad_burst = 0;
    assert(!keyboard->quiesce() && !gamepad->quiesce() && !generic->quiesce());
    attached = false;
    assert(keys->poll(keys->context, 4));
    assert(pads->poll(pads->context, 4));
    assert(keys->next(keys->context, key_sub, &key_event) == 1 &&
           key_event.kind == 4 && key_event.usage == 4);
    assert(keys->next(keys->context, key_sub, &key_event) == 1 &&
           key_event.kind == 2);
    assert(pads->next(pads->context, pad_sub, &pad_event) == 1 &&
           pad_event.kind == 2 && !pad_event.state.connected &&
           !pad_event.state.buttons);
    n = 4;
    assert(keys->snapshot(keys->context, key_state, &n) && n == 0);
    n = 4;
    assert(pads->snapshot(pads->context, pad_state, &n) && n == 0);
    assert(releases == 2 && !claimed[0] && !claimed[1]);
    assert(keys->unsubscribe(keys->context, key_sub));
    assert(pads->unsubscribe(pads->context, pad_sub));
    assert(keyboard->quiesce() && gamepad->quiesce() && generic->quiesce());
    keyboard->stop(); gamepad->stop(); generic->stop();
    assert(!dlclose(gamepad_module) && !dlclose(keyboard_module) && !dlclose(hid_module));
    puts("USB HID class, keyboard and gamepad report subscriptions/disconnect: PASS");
    return 0;
}

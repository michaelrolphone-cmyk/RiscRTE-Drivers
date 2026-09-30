#include "../../Drivers/usb_hid_gamepad/driver.c"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* One valid controller input followed by an unrelated vendor feature report.
 * The feature report ID must not invalidate or change the input layout. */
static const uint8_t feature_descriptor[] = {
    0x05,1, 0x09,5, 0xa1,1, 0x85,1,
    0x05,9, 0x19,1, 0x29,8, 0x15,0, 0x25,1,
    0x75,1, 0x95,8, 0x81,2,
    0x05,1, 0x09,0x30, 0x09,0x31, 0x15,0, 0x26,255,0,
    0x75,8, 0x95,2, 0x81,2,
    0x06,0,0xff, 0x09,1, 0x85,2, 0x75,8, 0x95,8, 0xb1,2, 0xc0
};
static const uint8_t vendor_input[] = {
    0x06,0,0xff, 0x09,1, 0xa1,1, 0x85,3,
    0x15,0, 0x26,255,0, 0x75,8, 0x95,8, 0x81,2, 0xc0
};
static uint8_t descriptor[256];
static size_t descriptor_length;
static bool attached = true, descriptor_ok = true, claimed;
static unsigned reads, opens, descriptor_reads;
static uint64_t now_ms;
static bool auxiliary, aux_claimed, close_ok = true;
static uint64_t fake_now(void *ctx) { (void)ctx; return now_ms; }
static const risc_platform_clock_api_v1 fake_clock = {
    RISC_PLATFORM_CLOCK_API_V1, sizeof(risc_platform_clock_api_v1), 0, fake_now, 0
};
static bool fake_scan(void *ctx, size_t budget) { (void)ctx; return budget != 0; }
static bool fake_interfaces(void *ctx, risc_usb_hid_interface_v1 *out, size_t *n) {
    (void)ctx;
    assert(*n >= 1);
    *n = attached ? (auxiliary ? 2 : 1) : 0;
    if (*n) *out = (risc_usb_hid_interface_v1){.device=42, .max_packet=8};
    if (*n == 2) out[1] = (risc_usb_hid_interface_v1){.device=42, .interface_number=1, .max_packet=8};
    return true;
}
static uint64_t fake_open(void *ctx, uint64_t device, uint8_t iface, uint8_t alt) {
    (void)ctx; (void)iface; (void)alt;
    if (iface == 1) { assert(!aux_claimed); ++opens; aux_claimed = true; return 8; }
    assert(device == 42 && !claimed);
    ++opens; claimed = true; return 7;
}
static bool fake_descriptor(void *ctx, uint64_t token, uint8_t *out, size_t *n) {
    (void)ctx; ++descriptor_reads;
    /* Model the permitted control timeout without slowing the host test. */
    now_ms += 100;
    if (token == 8) {
        assert(aux_claimed && *n >= sizeof(vendor_input));
        memcpy(out, vendor_input, sizeof(vendor_input)); *n = sizeof(vendor_input); return true;
    }
    assert(token == 7 && claimed && *n >= descriptor_length);
    if (!descriptor_ok) return false;
    memcpy(out, descriptor, descriptor_length); *n = descriptor_length; return true;
}
static int32_t fake_read(void *ctx, uint64_t token, uint8_t *out, size_t n, uint32_t ms) {
    (void)ctx; assert(token == 7 && claimed && n >= 4 && ms <= 100);
    if (reads++) return 0;
    const uint8_t report[] = {1, 2, 255, 0};
    memcpy(out, report, sizeof(report)); return sizeof(report);
}
static bool fake_present(void *ctx, uint64_t token) { (void)ctx; return token == 7 && attached; }
static bool fake_close(void *ctx, uint64_t token) {
    (void)ctx;
    if (!close_ok) return false;
    if (token == 8) { assert(aux_claimed); aux_claimed = false; return true; }
    assert(token == 7 && claimed); claimed = false; return true;
}
static const risc_usb_hid_api_v1 fake_hid = {
    RISC_USB_HID_API_V1, sizeof(risc_usb_hid_api_v1), 0,
    fake_scan, fake_interfaces, fake_open, fake_descriptor, 0,
    fake_read, fake_present, fake_close
};
int main(void) {
    gamepad pad = {0};
    assert(layout(&pad, feature_descriptor, sizeof(feature_descriptor)));
    assert(pad.report_id == 1 && pad.report_bits == 24 && pad.field_count == 10);
    memcpy(descriptor, vendor_input, sizeof(vendor_input));
    memcpy(descriptor + sizeof(vendor_input), feature_descriptor, sizeof(feature_descriptor));
    descriptor_length = sizeof(vendor_input) + sizeof(feature_descriptor);
    pad = (gamepad){0};
    assert(layout(&pad, descriptor, descriptor_length));
    assert(pad.report_id == 1 && pad.report_bits == 24 && pad.fields[0].bit == 0);
    /* A second logical controller must not reject the first usable controller. */
    memcpy(descriptor + descriptor_length, feature_descriptor, sizeof(feature_descriptor));
    descriptor[descriptor_length + 7] = 4;
    descriptor_length += sizeof(feature_descriptor);
    pad = (gamepad){0};
    assert(layout(&pad, descriptor, descriptor_length));
    assert(pad.report_id == 1 && pad.report_bits == 24);
    const risc_provider_dependency_v1 deps[] = {
        {"usb.hid", 1, &fake_hid}, {"platform.clock", 1, &fake_clock}
    };
    assert(!start(deps, 1));
    assert(start(deps, 2));
    uint64_t subscription = subscribe(0, 0);
    assert(subscription && poll(0, 4));
    risc_usb_gamepad_event_v1 event;
    assert(next(0, subscription, &event) == 1 && event.kind == 1);
    assert(event.state.report_id == 1 && event.state.buttons == 2);
    assert(event.state.x == 32767 && event.state.y == -32767);
    assert(next(0, subscription, &event) == 0 && opens == 1);
    char message[48];
    assert(api.base.struct_size == sizeof(api) && api.diagnostic(0, message, sizeof(message)));
    assert(!strcmp(message, "GAMEPAD INPUT LAYOUT CONNECTED"));
    attached = false;
    assert(poll(0, 4) && !claimed);
    assert(next(0, subscription, &event) == 1 && event.kind == 2);
    assert(api.diagnostic(0, message, sizeof(message)));
    assert(!strcmp(message, "NO HID INTERFACE DISCOVERED"));
    attached = true; descriptor_ok = false;
    assert(poll(0, 4) && !claimed);
    assert(api.diagnostic(0, message, sizeof(message)));
    assert(!strcmp(message, "HID REPORT DESCRIPTOR READ FAILED"));
    descriptor_ok = true; --descriptor_length; // missing End Collection
    assert(poll(0, 4) && !claimed);
    assert(api.diagnostic(0, message, sizeof(message)));
    assert(!strcmp(message, "HID REPORT DESCRIPTOR UNSUPPORTED"));
    char tiny[2] = {'x', 'x'};
    assert(api.diagnostic(0, tiny, sizeof(tiny)) && tiny[1] == 0);
    assert(!api.diagnostic(0, tiny, 0) && !api.diagnostic(0, 0, sizeof(tiny)));
    const unsigned rejected_reads = descriptor_reads;
    for (unsigned i = 0; i < 500; ++i) { now_ms += 20; assert(poll(0, 4)); }
    assert(descriptor_reads == rejected_reads && !claimed);
    attached = false; assert(poll(0, 4));
    attached = true;
    ++descriptor_length; reads = 0;
    assert(poll(0, 4) && claimed);
    assert(next(0, subscription, &event) == 1 && event.kind == 1);
    assert(next(0, subscription, &event) == 0);
    /* Auxiliary vendor HID must not repeatedly delay touch consumption, while
     * the valid controller still delivers its latest state on every poll. */
    auxiliary = true;
    assert(poll(0, 4));
    const unsigned composite_reads = descriptor_reads;
    for (unsigned i = 0; i < 500; ++i) {
        reads = 0; now_ms += 20;
        assert(poll(0, 4) && reads >= 1 && !aux_claimed);
    }
    assert(descriptor_reads == composite_reads);
    attached = false; assert(poll(0, 4));
    attached = true; descriptor_ok = false;
    unsigned before = descriptor_reads;
    assert(poll(0, 4) && descriptor_reads == before + 1); // one probe, not both
    assert(poll(0, 4) && descriptor_reads == before + 2); // retry due after 100 ms
    before = descriptor_reads;
    assert(poll(0, 4) && descriptor_reads == before + 1); // auxiliary gets a turn
    before = descriptor_reads;
    now_ms = UINT64_MAX; assert(poll(0, 4) && descriptor_reads == before);
    now_ms = 0; assert(poll(0, 4) && descriptor_reads == before); // clock regression
    now_ms = 100000; assert(poll(0, 4) && descriptor_reads == before); // deadline
    attached = false; assert(poll(0, 4));
    auxiliary = false; attached = true;
    before = descriptor_reads;
    for (unsigned i = 0; i < 1000; ++i) { now_ms += 20; assert(poll(0, 4)); }
    assert(descriptor_reads == before + 8 && !claimed); // bounded lifetime retries
    attached = false; assert(poll(0, 4));
    attached = true; close_ok = false;
    assert(!poll(0, 4) && claimed);
    assert(unsubscribe(0, subscription));
    assert(!quiesce()); // failed close retains the physical claim
    close_ok = true;
    assert(quiesce() && !claimed);
    stop();
    puts("Gamepad feature IDs, unrelated inputs, multiple collections, input delivery and detach: PASS");
}

#include "RiscUsbHidV1.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../../Drivers/usb_hid_keyboard/driver.c"

static risc_usb_hid_interface_v1 iface = {
    .device = 0x1111, .interface_number = 2, .alternate = 0,
    .subclass = 1, .protocol = 1, .interrupt_in = 0x81, .max_packet = 8
};
static uint8_t reports[40][8];
static size_t report_count, report_index;
static bool present_state = true, close_ok = true;
static unsigned scans, opens, closes, boot_protocol_calls;

static bool scan_mock(void *ctx, size_t max_events) {
    (void)ctx; assert(max_events == 16); ++scans; return true;
}
static bool interfaces_mock(void *ctx, risc_usb_hid_interface_v1 *out, size_t *count) {
    (void)ctx; assert(count);
    if (*count < 1) { *count = 1; return false; }
    out[0] = iface; *count = 1; return true;
}
static uint64_t open_mock(void *ctx, uint64_t device, uint8_t interface_number, uint8_t alternate) {
    (void)ctx; assert(device == iface.device); assert(interface_number == 2); assert(alternate == 0);
    ++opens; return 0x2222;
}
static bool report_descriptor_mock(void *ctx, uint64_t session, uint8_t *out, size_t *len) {
    (void)ctx; (void)session; (void)out; (void)len; return false;
}
static bool boot_mock(void *ctx, uint64_t session, bool boot) {
    (void)ctx; assert(session == 0x2222); assert(boot); ++boot_protocol_calls; return true;
}
static int32_t read_mock(void *ctx, uint64_t session, uint8_t *out, size_t cap, uint32_t timeout_ms) {
    (void)ctx; assert(session == 0x2222); assert(cap >= 8); assert(timeout_ms == 10);
    if (report_index >= report_count) return 0;
    memcpy(out, reports[report_index++], 8);
    return 8;
}
static bool present_mock(void *ctx, uint64_t session) {
    (void)ctx; assert(session == 0x2222); return present_state;
}
static bool close_mock(void *ctx, uint64_t session) {
    (void)ctx; assert(session == 0x2222); ++closes; return close_ok;
}

static risc_usb_hid_api_v1 mock_hid = {
    RISC_USB_HID_API_V1, sizeof(risc_usb_hid_api_v1), 0,
    scan_mock, interfaces_mock, open_mock, report_descriptor_mock,
    boot_mock, read_mock, present_mock, close_mock
};

static void queue_report(uint8_t modifiers, uint8_t k0) {
    assert(report_count < 40);
    reports[report_count][0] = modifiers;
    reports[report_count][2] = k0;
    ++report_count;
}

int main(void) {
    risc_provider_dependency_v1 dep = {"usb.hid", RISC_USB_HID_API_V1, &mock_hid};
    assert(start(&dep, 1));
    assert(!start(&dep, 1));
    const risc_usb_keyboard_api_v1 *kbd = &api;

    uint64_t all = kbd->subscribe(0, 0);
    uint64_t filtered = kbd->subscribe(0, iface.device);
    assert(all && filtered);

    queue_report(0, 4);
    queue_report(2, 4);
    queue_report(2, 0);
    queue_report(0, 0);
    assert(kbd->poll(0, 8));
    assert(scans && opens == 1 && boot_protocol_calls == 1);

    risc_usb_keyboard_event_v1 ev = {0};
    assert(kbd->next(0, all, &ev) == 1 && ev.kind == 1 && ev.device == iface.device);
    assert(kbd->next(0, all, &ev) == 1 && ev.kind == 3 && ev.usage == 4);
    assert(kbd->next(0, all, &ev) == 1 && ev.kind == 3 && ev.usage == 0xe1 && (ev.modifiers & 2));
    assert(kbd->next(0, all, &ev) == 1 && ev.kind == 4 && ev.usage == 4);
    assert(kbd->next(0, all, &ev) == 1 && ev.kind == 4 && ev.usage == 0xe1);
    assert(kbd->next(0, all, &ev) == 0);

    risc_usb_keyboard_state_v1 snap[1];
    size_t cap = 0;
    assert(!kbd->snapshot(0, NULL, &cap) && cap == 1);
    cap = 1;
    assert(kbd->snapshot(0, snap, &cap) && cap == 1 && snap[0].connected);

    assert(!quiesce());
    assert(kbd->unsubscribe(0, all));
    assert(kbd->unsubscribe(0, filtered));
    assert(quiesce());
    assert(closes == 1);

    uint64_t sub = kbd->subscribe(0, 0);
    assert(kbd->poll(0, 1));
    assert(kbd->next(0, sub, &ev) == 1 && ev.kind == 1);
    present_state = false;
    iface.subclass = 0;
    assert(kbd->poll(0, 1));
    assert(kbd->next(0, sub, &ev) == 1 && ev.kind == 2);
    assert(kbd->unsubscribe(0, sub));

    present_state = true; iface.subclass = 1;
    sub = kbd->subscribe(0, 0);
    assert(kbd->poll(0, 1));
    (void)kbd->next(0, sub, &ev);
    for (unsigned i = 0; i < 34; ++i) {
        queue_report(0, (i & 1) ? 0 : 4);
    }
    while (report_index < report_count) assert(kbd->poll(0, 16));
    assert(kbd->next(0, sub, &ev) == -1);
    assert(kbd->unsubscribe(0, sub));

    assert(quiesce());
    stop();
    assert(!hid);
    puts("USB HID keyboard boot reports, modifiers, subscriptions, snapshot, gap and quiescence: PASS");
    return 0;
}

#include "../../Drivers/usb_xinput_gamepad/driver.c"
#include <assert.h>
#include <stdio.h>
#include <string.h>

// Composite receiver with HID first, then a non-default XInput interface and
// endpoint, followed by Xbox's unrelated shortened vendor interface record.
static uint8_t config[] = {
    9,2,54,0,3,1,0,0x80,50,
    9,4,0,0,1,3,0,0,0, 7,5,0x81,3,8,0,10,
    9,4,2,0,2,0xff,0x5d,1,0, 7,5,0x83,3,32,0,4, 7,5,4,3,32,0,8,
    6,4,3,0,0,0xff
};
static uint64_t fake_device = 51;
static uint16_t fake_vid = 0x045e, fake_pid = 0x028e;
static uint8_t packet[64];
static int32_t packet_length;
static unsigned claims, releases, configurations, reads;
static bool reject_claim, reject_config;
static uint64_t fake_ms;
static uint64_t fake_monotonic(void *ctx) { (void)ctx; return fake_ms; }
static risc_platform_clock_api_v1 fake_clock = {
    .api_version = 1, .struct_size = sizeof(risc_platform_clock_api_v1),
    .monotonic_ms = fake_monotonic
};
static bool fake_poll(void *ctx, size_t budget, size_t *processed) {
    (void)ctx; assert(budget == 8); *processed = 0; return true;
}
static bool fake_devices(void *ctx, uint64_t *out, size_t *count) {
    (void)ctx; assert(*count >= 1); *count = fake_device ? 1 : 0;
    if (fake_device) out[0] = fake_device;
    return true;
}
static bool fake_config(void *ctx, uint64_t device, uint8_t *out, size_t *count,
                         uint16_t *vid, uint16_t *pid) {
    (void)ctx; assert(device == fake_device && *count >= sizeof(config));
    ++configurations;
    if (reject_config) return false;
    memcpy(out, config, sizeof(config)); *count = sizeof(config);
    *vid = fake_vid; *pid = fake_pid; return true;
}
static bool fake_claim(void *ctx, uint64_t device, uint8_t iface, uint8_t alt, uint64_t *out) {
    (void)ctx; assert(device == fake_device && iface == 2 && alt == config[28]);
    ++claims;
    if (reject_claim) return false;
    *out = device + 1000; return true;
}
static void fake_release(void *ctx, uint64_t claim) { (void)ctx; assert(claim > 1000); ++releases; }
static int32_t fake_read(void *ctx, uint64_t claim, uint8_t endpoint, uint8_t *out,
                         size_t capacity, uint32_t timeout) {
    (void)ctx;
    assert(claim == fake_device + 1000 && endpoint == 0x83 && capacity == 64 && timeout == 10);
    ++reads;
    int32_t result = packet_length; packet_length = 0;
    if (result > 0 && result <= 64) memcpy(out, packet, (size_t)result);
    return result;
}
static risc_usb_host_interrupt_v1 fake_host = {
    .discovery = {
        .host = {.api_version = 1, .struct_size = sizeof(risc_usb_host_interrupt_v1),
                 .configuration = fake_config, .claim = fake_claim, .release = fake_release},
        .poll = fake_poll, .devices = fake_devices
    }, .interrupt_read = fake_read
};
// Dependencies need not arrive in manifest order.
static risc_provider_dependency_v1 dependencies[] = {
    {"platform.clock", 1, &fake_clock}, {"usb.host", 1, &fake_host}
};
static void send(uint8_t directions, uint8_t buttons) {
    memset(packet, 0, sizeof(packet)); packet[1] = 20;
    packet[2] = directions; packet[3] = buttons; packet_length = 20;
}
static void drain(const risc_usb_gamepad_api_v1 *input, uint64_t subscription) {
    risc_usb_gamepad_event_v1 event;
    while (input->next(0, subscription, &event) > 0) {}
}
static void startup_recovery_test(const risc_driver_v2 *driver) {
    const risc_usb_gamepad_api_v1 *input = driver->capability;
    // Device was present before start. Recover two failed configuration reads
    // on the SAME attachment; snapshots need no event subscription to connect.
    fake_device = 100; fake_ms = 0; reject_config = true;
    claims = configurations = releases = 0;
    assert(driver->start(dependencies, 2));
    assert(input->poll(0, 4) && configurations == 1 && claims == 0);
    for (unsigned i = 0; i < 1000; ++i) assert(input->poll(0, 4));
    assert(configurations == 1); // Polling frequency does not shorten backoff.
    fake_ms = 100; assert(input->poll(0, 4) && configurations == 2);
    reject_config = false; send(0, 0x10);
    fake_ms = 299; assert(input->poll(0, 4) && configurations == 2);
    fake_ms = 300; assert(input->poll(0, 4) && configurations == 3 && claims == 1);
    risc_usb_gamepad_state_v1 state; size_t count = 1;
    assert(input->snapshot(0, &state, &count) && count == 1 && state.connected);
    assert(state.device == 100 && state.buttons == 2);
    driver->stop(); assert(releases == 1);

    // Persistent claim failure terminates after the finite attempt budget.
    // Successful descriptors are reused instead of repeatedly read.
    reject_claim = true; fake_ms = 0; claims = configurations = 0;
    assert(driver->start(dependencies, 2));
    assert(input->poll(0, 4) && claims == 1);
    const unsigned delays[] = {100, 200, 400, 800, 1600, 2000, 2000};
    for (unsigned i = 0; i < sizeof(delays) / sizeof(delays[0]); ++i) {
        fake_ms += delays[i] - 1;
        assert(input->poll(0, 4) && claims == i + 1);
        ++fake_ms; assert(input->poll(0, 4) && claims == i + 2);
    }
    fake_ms += 100000;
    assert(input->poll(0, 4) && claims == 8 && configurations == 1);
    char reason[64]; assert(diagnostic(0, reason, sizeof(reason)));
    assert(!strcmp(reason, "XINPUT INTERFACE CLAIM RETRY LIMIT"));
    driver->stop();

    // Elapsed deadline also stops retries when the consumer was not polling.
    claims = 0; assert(driver->start(dependencies, 2));
    assert(input->poll(0, 4) && claims == 1);
    fake_ms += 10000; assert(input->poll(0, 4) && claims == 1);
    assert(diagnostic(0, reason, sizeof(reason)));
    assert(!strcmp(reason, "XINPUT DISCOVERY RETRY DEADLINE"));
    driver->stop(); reject_claim = false;

    // A clock failure pauses discovery without consuming the attached device.
    claims = configurations = 0; fake_ms = UINT64_MAX;
    assert(driver->start(dependencies, 2));
    assert(input->poll(0, 4) && configurations == 0);
    fake_ms = 0; send(0, 0x20);
    assert(input->poll(0, 4) && claims == 1);
    count = 1;
    assert(input->snapshot(0, &state, &count) && state.device == 100 && state.connected);
    driver->stop();

    // Failed attachment disappears during backoff: never claim a stale handle.
    claims = 0; reject_claim = true;
    assert(driver->start(dependencies, 2));
    assert(input->poll(0, 4) && claims == 1);
    fake_device = 0; fake_ms = 100;
    assert(input->poll(0, 4) && claims == 1);
    fake_device = 101; reject_claim = false; send(0, 0x10);
    assert(input->poll(0, 4) && claims == 2);
    driver->stop();

    // Unsupported interfaces remain cached even after the retry deadline.
    config[32] = 0x82; claims = configurations = 0;
    assert(driver->start(dependencies, 2));
    assert(input->poll(0, 4) && configurations == 1 && claims == 0);
    fake_ms += 100000;
    assert(input->poll(0, 4) && configurations == 1 && claims == 0);
    driver->stop(); config[32] = 1;
}
int main(void) {
    assert(!t5_driver_get(1));
    const risc_driver_v2 *driver = t5_driver_get(2);
    assert(!strcmp(driver->capability_id, "usb.xinput.gamepad"));
    const risc_usb_gamepad_api_v1 *input = driver->capability;
    assert(!driver->start(0, 0));
    assert(!driver->start(dependencies + 1, 1)); // Missing clock dependency.
    fake_host.discovery.host.struct_size = sizeof(risc_usb_host_discovery_v1);
    assert(!driver->start(dependencies, 2));
    fake_host.discovery.host.struct_size = sizeof(fake_host);
    fake_clock.monotonic_ms = 0;
    assert(!driver->start(dependencies, 2));
    fake_clock.monotonic_ms = fake_monotonic;
    assert(driver->start(dependencies, 2) && !driver->start(dependencies, 2));
    uint64_t subscription = input->subscribe(0, 0), filtered = input->subscribe(0, 999);
    assert(subscription && filtered && !driver->quiesce());
    assert(!input->poll(0, 0) && !input->poll(0, 17));
    assert(input->poll(0, 4) && claims == 1 && configurations == 1);
    assert(reads == 1); // Idle endpoint is attempted only once per poll.
    for (unsigned i = 0; i < 10; ++i) assert(input->poll(0, 4));
    assert(configurations == 1); // No repeated configuration scans.
    risc_usb_gamepad_state_v1 state;
    size_t count = 1;
    assert(input->snapshot(0, &state, &count) && count == 1 && !state.connected);
    risc_usb_gamepad_event_v1 event;
    assert(input->next(0, subscription, &event) == 0);
    for (int length = 1; length < 20; ++length) {
        send(0, 0xf0); packet_length = length;
        assert(input->poll(0, 4) && input->next(0, subscription, &event) == 0);
    }
    send(0, 0xf0); packet[0] = 1; // LED/status notification.
    assert(input->poll(0, 4) && input->next(0, subscription, &event) == 0);
    send(0, 0xf0); packet[1] = 13;
    assert(input->poll(0, 4) && input->next(0, subscription, &event) == 0);
    send(0x39, 0xf7); // NE, Back, Start, all face/shoulder buttons and Guide.
    packet[4] = packet[5] = 255;
    packet[6] = 0; packet[7] = 0x80; // X = -32768
    packet[8] = 0; packet[9] = 0x80; // Y saturates positive when inverted.
    packet[10] = 0xff; packet[11] = 0x7f;
    packet[12] = 0xff; packet[13] = 0x7f;
    assert(input->poll(0, 4));
    assert(input->next(0, subscription, &event) == 1 && event.kind == 1);
    assert(event.state.device == fake_device && event.state.connected && event.state.hat == 1);
    assert(event.state.buttons == 0x13ff && event.state.x == -32768 && event.state.y == 32767);
    assert(event.state.rx == 32767 && event.state.ry == -32768);
    assert(event.state.z == 32767 && event.state.rz == 32767);
    assert(input->next(0, filtered, &event) == 0);
    // Repeated unchanged data emits no extra state transition.
    packet_length = 20;
    assert(input->poll(0, 4) && input->next(0, subscription, &event) == 0);
    // The working standalone accepts a 14-byte header length in a complete
    // 20-byte transfer. Xbox A must remain A (semantic bit 1), not become B.
    send(0, 0x10); packet[1] = 14;
    assert(input->poll(0, 4));
    assert(input->next(0, subscription, &event) == 1 && event.kind == 3 && event.state.buttons == 2);
    const uint8_t face_bits[] = {0x10, 0x20, 0x40, 0x80};
    const uint32_t face_expected[] = {2, 1, 8, 4};
    for (size_t i = 0; i < 4; ++i) {
        send(0, face_bits[i]); assert(input->poll(0, 4)); count = 1;
        assert(input->snapshot(0, &state, &count) && state.buttons == face_expected[i]);
        drain(input, subscription);
    }
    const uint8_t hats[] = {8,0,4,8,6,7,5,6,2,1,3,2,8,0,4,8};
    for (uint8_t dpad = 0; dpad < 16; ++dpad) {
        send(dpad, 0); assert(input->poll(0, 4)); count = 1;
        assert(input->snapshot(0, &state, &count) && state.hat == hats[dpad]);
        drain(input, subscription);
    }
    send(0xf0, 0xf7); packet[4] = packet[5] = 128;
    assert(input->poll(0, 4)); count = 1;
    assert(input->snapshot(0, &state, &count) && state.buttons == 0x1fff);
    drain(input, subscription);
    for (unsigned i = 0; i < 4096; ++i) {
        send(0, i & 1 ? 0x20 : 0); assert(input->poll(0, 4));
    }
    assert(input->next(0, subscription, &event) == 1 && event.state.buttons == 1);
    assert(input->next(0, subscription, &event) == 0);
    count = 1;
    assert(input->snapshot(0, &state, &count) && state.buttons == 1);
    fake_device = 0;
    assert(input->poll(0, 4) && releases == 1);
    assert(input->next(0, subscription, &event) == 1 && event.kind == 2);
    assert(!event.state.connected && !event.state.buttons && !event.state.x && event.state.hat == 8);
    count = 1; assert(input->snapshot(0, &state, &count) && count == 0);
    // Bind by the proven ff/5d/protocol interface, including a clone's VID/PID
    // and a nonzero alternate. RF presence messages are not 20-byte inputs.
    fake_device = 52; fake_vid = 0x1234; fake_pid = 0x9876;
    config[32] = 0x81; config[28] = 1;
    assert(input->poll(0, 4) && claims == 2);
    packet[0] = 8; packet[1] = 0x80; packet_length = 2;
    assert(input->poll(0, 4));
    assert(input->next(0, subscription, &event) == 1 && event.kind == 1);
    send(0x18, 0x33);
    memmove(packet + 4, packet, 20);
    packet[0] = 0; packet[1] = 1; packet[2] = packet[3] = 0;
    packet_length = 23;
    assert(input->poll(0, 4) && input->next(0, subscription, &event) == 0);
    packet_length = 24; assert(input->poll(0, 4));
    assert(input->next(0, subscription, &event) == 1 && event.kind == 3);
    assert(event.state.buttons == 0x233 && event.state.hat == 2);
    packet[0] = 8; packet[1] = 0; packet_length = 2;
    assert(input->poll(0, 4));
    assert(input->next(0, subscription, &event) == 1 && event.kind == 2 && !event.state.buttons);
    fake_device = 53; config[32] = 0x82; config[28] = 0;
    assert(input->poll(0, 4) && claims == 2); // Unknown vendor protocol stays unclaimed.
    fake_device = 54; config[32] = 1; config[37] = 2;
    assert(input->poll(0, 4) && claims == 2); // Reject bulk instead of interrupt.
    fake_device = 55; config[37] = 3; config[2] = 53;
    assert(input->poll(0, 4) && claims == 2); // Reject truncated/mismatched configuration.
    fake_device = 56; config[2] = 54; reject_claim = true;
    assert(input->poll(0, 4) && claims == 3);
    char reason[64]; assert(diagnostic(0, reason, sizeof(reason)));
    assert(!strcmp(reason, "XINPUT INTERFACE CLAIM RETRYING"));
    assert(input->poll(0, 4) && claims == 3); // Failed attachment never busy-retries claims.
    reject_claim = false; send(0, 0x10);
    fake_ms = 99; assert(input->poll(0, 4) && claims == 3);
    fake_ms = 100; assert(input->poll(0, 4) && claims == 4);
    assert(input->next(0, subscription, &event) == 1 && event.state.device == 56);
    assert(event.state.connected && event.state.buttons == 2); // No unplug/replug.
    fake_device = 57; reject_claim = false; send(0, 0x10);
    assert(input->poll(0, 4) && claims == 5);
    assert(input->next(0, subscription, &event) == 1 && event.kind == 2 && event.state.device == 56);
    assert(input->next(0, subscription, &event) == 1 && event.kind == 1 && event.state.device == 57);
    drain(input, subscription);
    packet_length = -1;
    assert(!input->poll(0, 4));
    assert(input->next(0, subscription, &event) == 1 && event.kind == 2 && !event.state.buttons);
    send(0, 0x20); assert(input->poll(0, 4));
    assert(input->next(0, subscription, &event) == 1 && event.kind == 1 && event.state.buttons == 1);
    assert(!driver->quiesce());
    assert(input->unsubscribe(0, subscription) && input->unsubscribe(0, filtered));
    assert(driver->quiesce() && releases == 4); // Still-plugged shutdown.
    driver->stop();
    assert(driver->start(dependencies, 2));
    uint64_t fresh = input->subscribe(0, 0); assert(fresh && fresh != subscription);
    assert(input->next(0, subscription, &event) == -1);
    assert(input->unsubscribe(0, fresh)); driver->stop();
    startup_recovery_test(driver);
    puts("XInput discovery, current state, bounded startup retries, hotplug and shutdown: PASS");
}

#include "RiscTouchV1.h"
#include "RiscI2cBusV1.h"
#include "RiscPlatformClockV1.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

static uint8_t controller_address = 0x5du;
static uint8_t status_reg;
static uint8_t points[RISC_TOUCH_MAX_CONTACTS * 8u];
static uint64_t fake_ms = 1000u;
static uint64_t claim_serial;
static uint64_t active_claim;
static uint8_t claimed_address;
static unsigned release_calls;
static unsigned fail_point_reads;
static unsigned fail_ack_writes;
static bool failed_ack_reaches_controller;
static pthread_mutex_t gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condition = PTHREAD_COND_INITIALIZER;
static bool pause_status, in_status, resume_status;
static const risc_touch_api_v1 *thread_api;
static void *poll_thread(void *unused) {
    (void)unused;
    assert(thread_api->poll(NULL, 16u));
    return NULL;
}

static bool claim_device(void *context, uint8_t address, uint64_t *out) {
    (void)context;
    if (out) *out = 0;
    if (!out || active_claim || address != controller_address) return false;
    active_claim = ++claim_serial;
    claimed_address = address;
    *out = active_claim;
    return true;
}

static bool transact(void *context, uint64_t claim,
                     const uint8_t *write_bytes, size_t write_length,
                     uint8_t *read_bytes, size_t read_length,
                     uint32_t timeout_ms) {
    (void)context;
    assert(claim == active_claim);
    assert(timeout_ms == 20u);
    assert(write_bytes && write_length >= 2u);
    const uint16_t reg = (uint16_t)(((uint16_t)write_bytes[0] << 8u) |
                                    write_bytes[1]);
    if (write_length == 3u && !read_length && reg == 0x814eu) {
        if (fail_ack_writes) {
            --fail_ack_writes;
            if (failed_ack_reaches_controller) status_reg = write_bytes[2];
            return false;
        }
        status_reg = write_bytes[2];
        return true;
    }
    if (write_length != 2u || !read_bytes || !read_length) return false;
    if (reg == 0x8140u && read_length == 11u) {
        memset(read_bytes, 0, read_length);
        read_bytes[0] = '9';
        read_bytes[1] = '1';
        read_bytes[2] = '1';
        read_bytes[6] = 0x1cu;
        read_bytes[7] = 0x02u;
        read_bytes[8] = 0xc0u;
        read_bytes[9] = 0x03u;
        return true;
    }
    if (reg == 0x814eu && read_length == 1u) {
        pthread_mutex_lock(&gate);
        if (pause_status) {
            in_status = true;
            pthread_cond_broadcast(&condition);
            while (!resume_status) pthread_cond_wait(&condition, &gate);
        }
        pthread_mutex_unlock(&gate);
        read_bytes[0] = status_reg;
        return true;
    }
    if (reg == 0x814fu && read_length <= sizeof(points)) {
        if (fail_point_reads) { --fail_point_reads; return false; }
        memcpy(read_bytes, points, read_length);
        return true;
    }
    return false;
}

static bool release_device(void *context, uint64_t claim) {
    (void)context;
    if (!claim || claim != active_claim) return false;
    active_claim = 0;
    claimed_address = 0;
    ++release_calls;
    return true;
}

static uint64_t monotonic_ms(void *context) {
    (void)context;
    return fake_ms;
}

static void sleep_ms(void *context, uint32_t ms) {
    (void)context;
    (void)ms;
}

static const risc_i2c_bus_api_v1 bus_api = {
    RISC_I2C_BUS_API_V1, sizeof(risc_i2c_bus_api_v1), NULL,
    claim_device, transact, release_device
};

static const risc_platform_clock_api_v1 clock_api = {
    RISC_PLATFORM_CLOCK_API_V1, sizeof(risc_platform_clock_api_v1), NULL,
    monotonic_ms, sleep_ms
};

static const risc_provider_dependency_v1 dependencies[] = {
    {"i2c.bus", RISC_I2C_BUS_API_V1, &bus_api},
    {"platform.clock", RISC_PLATFORM_CLOCK_API_V1, &clock_api}
};

static void report_one(uint8_t id, uint16_t x, uint16_t y) {
    memset(points, 0, sizeof(points));
    points[0] = id;
    points[1] = (uint8_t)x;
    points[2] = (uint8_t)(x >> 8u);
    points[3] = (uint8_t)y;
    points[4] = (uint8_t)(y >> 8u);
    status_reg = 0x81u;
}

static void report_release(void) {
    status_reg = 0x80u;
}

static void report_home(bool pressed) {
    status_reg = pressed ? 0x90u : 0x80u;
}

static risc_touch_event_v1 take(const risc_touch_api_v1 *api, uint64_t sub) {
    risc_touch_event_v1 event = {0};
    assert(api->next(api->context, sub, &event) == 1);
    return event;
}

int main(void) {
    assert(t5_driver_get(1u) == NULL);
    const risc_driver_v2 *driver = t5_driver_get(RISC_PROVIDER_DRIVER_ABI_V2);
    assert(driver && strcmp(driver->driver_id, "gt911-touch") == 0);
    assert(strcmp(driver->capability_id, "input.touch.raw") == 0);
    assert(driver->capability_api == RISC_TOUCH_API_V1);
    assert(driver->quiesce && driver->start && driver->stop);

    const risc_touch_api_v1 *api = driver->capability;
    assert(api && api->api_version == RISC_TOUCH_API_V1);
    assert(api->struct_size == sizeof(*api));

    assert(!driver->start(NULL, 0));
    assert(driver->start(dependencies, 2u));
    assert(claimed_address == controller_address && active_claim);
    assert(!driver->start(dependencies, 2u));

    uint64_t a = api->subscribe(api->context);
    uint64_t b = api->subscribe(api->context);
    assert(a && b && a != b);

    risc_touch_snapshot_v1 snap = {0};
    assert(api->snapshot(api->context, &snap));
    assert(snap.width == 540u && snap.height == 960u &&
           snap.contact_count == 0u);

    fake_ms = 1010u;
    report_one(3u, 100u, 200u);
    assert(api->poll(api->context, 4u) && status_reg == 0u);
    risc_touch_event_v1 event_a = take(api, a);
    risc_touch_event_v1 event_b = take(api, b);
    assert(event_a.kind == RISC_TOUCH_EVENT_DOWN && event_a.id == 3u);
    assert(event_a.x == 100u && event_a.y == 200u &&
           event_a.timestamp_ms == 1010u);
    assert(memcmp(&event_a, &event_b, sizeof(event_a)) == 0);
    assert(api->snapshot(api->context, &snap));
    assert(snap.contact_count == 1u && snap.contacts[0].id == 3u &&
           snap.contacts[0].x == 100u && snap.contacts[0].y == 200u);

    fake_ms = 1020u;
    report_one(3u, 120u, 210u);
    assert(api->poll(api->context, 1u));
    event_a = take(api, a);
    event_b = take(api, b);
    assert(event_a.kind == RISC_TOUCH_EVENT_MOVE &&
           event_a.x == 120u && event_a.y == 210u &&
           event_b.sequence == event_a.sequence);

    fake_ms = 1030u;
    report_release();
    assert(api->poll(api->context, 1u));
    event_a = take(api, a);
    event_b = take(api, b);
    assert(event_a.kind == RISC_TOUCH_EVENT_UP && event_a.id == 3u &&
           event_b.kind == RISC_TOUCH_EVENT_UP);
    assert(api->snapshot(api->context, &snap) &&
           snap.contact_count == 0u && snap.buttons == 0u);
    assert(api->next(api->context, a, &event_a) == 0);

    // A failed point read must leave READY latched for a later retry.
    report_one(3u, 100u, 200u);
    fail_point_reads = 3;
    for (unsigned i = 0; i < 3; ++i) {
        assert(!api->poll(api->context, 1u));
        assert(status_reg == 0x81u);
        assert(api->next(api->context, a, &event_a) == 0);
    }
    assert(api->poll(api->context, 1u));
    assert(take(api, a).kind == RISC_TOUCH_EVENT_DOWN);
    assert(take(api, b).kind == RISC_TOUCH_EVENT_DOWN);
    report_release();
    fail_ack_writes = 1;
    assert(!api->poll(api->context, 1u));
    assert(take(api, a).kind == RISC_TOUCH_EVENT_UP);
    assert(take(api, b).kind == RISC_TOUCH_EVENT_UP);
    assert(api->poll(api->context, 1u));
    assert(api->next(api->context, a, &event_a) == 0);
    assert(api->next(api->context, b, &event_b) == 0);

    // A failed ACK may actually clear READY. Both edges still arrive once.
    failed_ack_reaches_controller = true;
    report_one(3u, 100u, 200u); fail_ack_writes = 1;
    assert(!api->poll(NULL, 1u) && status_reg == 0u);
    assert(take(api, a).kind == RISC_TOUCH_EVENT_DOWN);
    assert(take(api, b).kind == RISC_TOUCH_EVENT_DOWN);
    assert(api->poll(NULL, 1u));
    assert(api->next(NULL, a, &event_a) == 0);
    report_release(); fail_ack_writes = 1;
    assert(!api->poll(NULL, 1u) && status_reg == 0u);
    assert(take(api, a).kind == RISC_TOUCH_EVENT_UP);
    assert(take(api, b).kind == RISC_TOUCH_EVENT_UP);
    assert(api->poll(NULL, 1u));
    assert(api->next(NULL, a, &event_a) == 0);
    failed_ack_reaches_controller = false;

    // Malformed reports are explicitly signalled as GAP to both consumers.
    report_one(3u, 100u, 200u);
    assert(api->poll(api->context, 1u));
    assert(take(api, a).kind == RISC_TOUCH_EVENT_DOWN);
    assert(take(api, b).kind == RISC_TOUCH_EVENT_DOWN);
    report_one(3u, 600u, 200u); // outside the 540px surface
    assert(!api->poll(api->context, 1u));
    assert(status_reg == 0u);
    assert(api->next(api->context, a, &event_a) == -1);
    assert(api->next(api->context, b, &event_b) == -1);
    report_release();
    assert(api->poll(api->context, 1u));
    assert(take(api, a).kind == RISC_TOUCH_EVENT_UP);
    assert(take(api, b).kind == RISC_TOUCH_EVENT_UP);
    status_reg = 0x86u; // impossible six-contact report
    assert(!api->poll(api->context, 1u));
    assert(api->next(api->context, a, &event_a) == -1);
    assert(api->next(api->context, b, &event_b) == -1);

    // A second caller cannot interleave status/read/ack or observe torn state.
    report_one(3u, 100u, 200u);
    thread_api = api;
    pause_status = true;
    pthread_t thread;
    assert(pthread_create(&thread, NULL, poll_thread, NULL) == 0);
    pthread_mutex_lock(&gate);
    while (!in_status) pthread_cond_wait(&condition, &gate);
    pthread_mutex_unlock(&gate);
    assert(!api->poll(NULL, 1u));
    assert(api->next(NULL, a, &event_a) == -2); // busy is not GAP
    assert(!api->snapshot(NULL, &snap));
    pthread_mutex_lock(&gate);
    resume_status = true;
    pthread_cond_broadcast(&condition);
    pthread_mutex_unlock(&gate);
    assert(pthread_join(thread, NULL) == 0);
    pause_status = false;
    assert(take(api, a).kind == RISC_TOUCH_EVENT_DOWN);
    assert(take(api, b).kind == RISC_TOUCH_EVENT_DOWN);
    assert(api->poll(NULL, 16u));
    assert(api->next(NULL, a, &event_a) == 0); // no duplicate DOWN
    report_release();
    assert(api->poll(NULL, 1u));
    assert(take(api, a).kind == RISC_TOUCH_EVENT_UP);
    assert(take(api, b).kind == RISC_TOUCH_EVENT_UP);

    fake_ms = 1040u;
    report_home(true);
    assert(api->poll(api->context, 1u));
    event_a = take(api, a);
    event_b = take(api, b);
    assert(event_a.kind == RISC_TOUCH_EVENT_BUTTON_DOWN &&
           event_a.id == 0u && event_b.kind == event_a.kind);
    assert(api->snapshot(api->context, &snap) &&
           snap.buttons == RISC_TOUCH_BUTTON_PRIMARY);

    fake_ms = 1050u;
    report_home(false);
    assert(api->poll(api->context, 1u));
    event_a = take(api, a);
    event_b = take(api, b);
    assert(event_a.kind == RISC_TOUCH_EVENT_BUTTON_UP &&
           event_b.kind == event_a.kind);
    assert(api->snapshot(api->context, &snap) && snap.buttons == 0u);

    for (unsigned i = 0; i < RISC_TOUCH_QUEUE_LENGTH + 4u; ++i) {
        fake_ms++;
        if (i & 1u) report_release();
        else report_one(1u, (uint16_t)(10u + i), 20u);
        assert(api->poll(api->context, 1u));
        while (api->next(api->context, a, &event_a) == 1) {}
    }
    assert(api->next(api->context, b, &event_b) == -1);
    assert(api->snapshot(api->context, &snap));
    assert(snap.contact_count <= 1u);

    assert(!driver->quiesce());
    assert(api->unsubscribe(api->context, a));
    assert(api->unsubscribe(api->context, b));
    assert(driver->quiesce());
    assert(release_calls == 1u && !active_claim);
    driver->stop();

    controller_address = 0x14u;
    status_reg = 0;
    assert(driver->start(dependencies, 2u));
    assert(claimed_address == 0x14u);
    uint64_t fresh = api->subscribe(NULL);
    assert(fresh > b);
    assert(!api->unsubscribe(NULL, a));
    assert(api->next(NULL, a, &event_a) == -1);
    assert(api->unsubscribe(NULL, fresh));
    assert(driver->quiesce());
    driver->stop();
    assert(release_calls == 2u);

    puts("GT911 input.touch.raw provider: fanout, DOWN/MOVE/UP, GAP snapshot and lifecycle PASS");
    return 0;
}

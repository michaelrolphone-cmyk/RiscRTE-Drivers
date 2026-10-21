/* Synthetic HCI/clock fixtures exercise only the production capability entry
 * points, not parser internals. No controller or physical radio is involved. */
#include "../Drivers/ble_sensors/driver.c"
#include <assert.h>
#include <stdio.h>

static uint64_t fake_now = 100;
static bool owned;
static unsigned claims, sends, releases;
static uint8_t pending[260];
static size_t pending_size;
static const risc_bluetooth_sensors_v1 *sensors;
static uint64_t active_token;
static uint64_t fixture_clock(void *context) { (void)context; return fake_now; }
static bool fixture_claim(void *context, uint64_t *out) {
    (void)context; assert(!owned); owned = true; claims++; *out = 42; return true;
}
static int32_t fixture_release(void *context, uint64_t id) {
    (void)context; assert(owned && id == 42); owned = false; releases++;
    pending_size = 0; return 1;
}
static bool fixture_send(void *context, uint64_t id, uint8_t type,
                         const uint8_t *packet, size_t size) {
    (void)context; assert(owned && id == 42 && type == 1);
    assert(size == 3u + packet[2] && !pending_size); sends++;
    uint8_t ack[] = {0x0e, 4, 1, packet[0], packet[1], 0};
    memcpy(pending, ack, sizeof(ack)); pending_size = sizeof(ack); return true;
}
static int32_t fixture_next(void *context, uint64_t id, uint8_t *type,
                            uint8_t *packet, size_t capacity, size_t *size) {
    (void)context; assert(owned && id == 42 && capacity >= pending_size);
    *type = 4; *size = pending_size;
    if (!pending_size) return 0;
    memcpy(packet, pending, pending_size); pending_size = 0; return 1;
}
static risc_ble_sensor_status_v1 read_status(void) {
    risc_ble_sensor_status_v1 out = {.struct_size = sizeof(out)};
    assert(sensors->status(sensors->context, &out)); return out;
}
static risc_ble_sensor_device_v1 read_device(uint32_t index) {
    risc_ble_sensor_device_v1 out;
    assert(sensors->device(sensors->context, index, &out)); return out;
}
static void begin_scan(void) {
    assert(sensors->open(sensors->context, &active_token) && active_token);
    for (unsigned i = 0; i < 5; i++) {
        fake_now += 10; assert(sensors->poll(sensors->context, active_token, 1));
    }
    assert(read_status().state == RISC_BLE_SENSOR_SCANNING);
}
static void advert(uint8_t address, uint8_t address_type,
                   const uint8_t *payload, size_t size) {
    assert(size <= 31 && !pending_size);
    const uint8_t header[] = {0x3e, (uint8_t)(12 + size), 2, 1, 0,
                             address_type, address, 2, 3, 4, 5, 6, (uint8_t)size};
    memcpy(pending, header, sizeof(header));
    memcpy(pending + sizeof(header), payload, size);
    pending[sizeof(header) + size] = (uint8_t)-53;
    pending_size = sizeof(header) + size + 1;
    assert(sensors->poll(sensors->context, active_token, 1));
}
static void bthome(uint8_t address, uint8_t address_type,
                   const uint8_t *data, size_t size) {
    uint8_t payload[31] = {(uint8_t)(size + 3), 0x16, 0xd2, 0xfc};
    assert(size <= sizeof(payload) - 4);
    memcpy(payload + 4, data, size); advert(address, address_type, payload, size + 4);
}
static void expect_empty(risc_ble_sensor_device_v1 d) {
    assert(!d.reading_count && !d.has_battery && !d.has_temperature && !d.has_humidity);
    for (unsigned i = 0; i < RISC_BLE_MAX_READINGS; i++)
        assert(!d.readings[i].metric && !d.readings[i].value);
}
int main(void) {
    portable_bluetooth_host_v1 h = {
        {1, sizeof(h), NULL, NULL, NULL, NULL, NULL},
        fixture_claim, fixture_send, fixture_next, fixture_release
    };
    risc_platform_clock_api_v1 k = {1, sizeof(k), NULL, fixture_clock, NULL};
    risc_provider_dependency_v1 deps[] = {
        {"bluetooth.hci", 1, &h}, {"platform.clock", 1, &k}
    };
    const risc_driver_v2 *driver_api = t5_driver_get(2);
    assert(driver_api && !strcmp(driver_api->capability_id, RISC_BLUETOOTH_SENSORS_CAPABILITY));
    sensors = driver_api->capability;
    assert(sensors->api_version == 1 && sensors->struct_size == sizeof(*sensors));
    assert(driver_api->start(deps, 2)); assert(!claims && !sends && !releases);
    assert(!read_status().count);
    begin_scan(); assert(claims == 1 && sends == 5);
    puts("PASS: capability registration and activation perform no implicit radio work");

    const uint8_t all_metrics[] = {0x40, 0, 9, 1, 88, 2, 0x9c, 0xff,
        3, 0x88, 0x13, 4, 0xcd, 0x8b, 1, 5, 0x40, 0xe2, 1, 0x0c, 0xd8, 0x0e, 0x16, 1};
    const int32_t values[] = {88, -100, 5000, 101325, 123456, 3800, 1};
    bthome(1, 0, all_metrics, sizeof(all_metrics));
    risc_ble_sensor_device_v1 d = read_device(0);
    assert(read_status().count == 1 && d.reading_count == 7 && d.bthome && d.bthome_version == 2);
    assert(!d.measurement_partial && !d.measurement_invalid && !d.encrypted && d.rssi == -53);
    for (unsigned i = 0; i < 7; i++) assert(d.readings[i].metric == i + 1 && d.readings[i].value == values[i]);
    assert(d.has_battery && d.battery == 88 && d.has_temperature && d.temperature == -100 && d.has_humidity && d.humidity == 5000);
    d.battery = 0; d.readings[0].value = 0;
    assert(read_device(0).battery == 88 && read_device(0).readings[0].value == 88);
    puts("PASS: all seven supported metrics, units, flags and copied-result isolation");

    const uint8_t battery[] = {0x40, 1, 77};
    bthome(1, 1, battery, sizeof(battery));
    bthome(2, 0, battery, sizeof(battery));
    assert(read_status().count == 3);
    assert(read_device(0).address_type == 0 && read_device(1).address_type == 1 && read_device(2).address[0] == 2);
    fake_now += 40; bthome(1, 0, battery, sizeof(battery));
    assert(read_status().count == 3 && read_device(0).reports == 2 && read_device(0).battery == 77);
    assert(read_device(1).reports == 1 && read_device(2).reports == 1);
    puts("PASS: multiple sensors, address-type identity, stable indexes and isolated refresh");

    const uint8_t named_only[] = {4, 9, 'R', 'o', 'o'};
    fake_now += 120; advert(1, 0, named_only, sizeof(named_only));
    fake_now += 50; d = read_device(0);
    assert(!strcmp(d.name, "Roo") && d.seen_age_ms == 50 && d.measurement_age_ms == 170 && d.battery == 77);
    const uint8_t encrypted[] = {0x41, 1, 99};
    bthome(1, 0, encrypted, sizeof(encrypted)); d = read_device(0);
    assert(d.encrypted && d.bthome && d.measurement_age_ms == 0); expect_empty(d);
    const uint8_t unsupported[] = {0x20, 1, 99};
    bthome(1, 0, unsupported, sizeof(unsupported)); d = read_device(0);
    assert(d.bthome_version == 1 && !d.encrypted); expect_empty(d);
    const uint8_t truncated[] = {0x40, 1, 99, 2, 0};
    bthome(1, 0, truncated, sizeof(truncated)); d = read_device(0);
    assert(d.measurement_invalid); expect_empty(d);
    bthome(1, 0, battery, sizeof(battery));
    assert(!read_device(0).measurement_invalid && read_device(0).battery == 77);
    puts("PASS: separate observation/measurement ages, unreadable refresh clearing and recovery");

    const uint8_t repeated[] = {0x40, 2, 0, 0x80, 2, 0xff, 0x7f};
    bthome(1, 0, repeated, sizeof(repeated)); d = read_device(0);
    assert(d.reading_count == 2 && d.readings[0].value == -32768 && d.readings[1].value == 32767);
    const uint8_t future[] = {0x40, 1, 50, 0xff, 0};
    bthome(1, 0, future, sizeof(future)); d = read_device(0);
    assert(d.measurement_partial && !d.measurement_invalid && d.reading_count == 1 && d.battery == 50);
    const uint8_t ambiguous[] = {6, 0x16, 0xd2, 0xfc, 0x40, 1, 88,
                                6, 0x16, 0xd2, 0xfc, 0x40, 1, 99};
    advert(1, 0, ambiguous, sizeof(ambiguous)); d = read_device(0);
    assert(d.measurement_invalid); expect_empty(d);
    bthome(1, 0, battery, sizeof(battery)); assert(read_device(0).battery == 77);
    puts("PASS: ordered repeated metrics, bounded future prefix and reused T061 ambiguity repair");

    const uint8_t gatt_only[] = {3, 3, 0x0f, 0x18};
    advert(3, 0, gatt_only, sizeof(gatt_only)); d = read_device(3);
    assert(d.service_count == 1 && d.services[0] == 0x180f && !d.bthome); expect_empty(d);
    const uint8_t malformed[] = {8, 0x16, 0xd2, 0xfc};
    uint32_t before_malformed = read_status().malformed;
    advert(4, 0, malformed, sizeof(malformed));
    assert(read_status().count == 4 && read_status().malformed == before_malformed + 1);
    for (unsigned i = 4; i <= 32; i++) bthome((uint8_t)i, 0, battery, sizeof(battery));
    assert(read_status().count == 32 && read_status().dropped == 1);
    assert(!sensors->device(sensors->context, 32, &d));
    assert(read_device(0).battery == 77 && read_device(31).address[0] == 31);
    puts("PASS: unsupported GATT stays undecoded, malformed report is atomic, table is bounded");

    fake_now += 15000; assert(sensors->poll(sensors->context, active_token, 1));
    assert(read_status().state == RISC_BLE_SENSOR_COMPLETE && read_status().cleanup_pending);
    uint64_t old_token = active_token;
    assert(sensors->close(sensors->context, active_token));
    assert(!owned && releases == 1 && !read_status().cleanup_pending);
    d = read_device(0); assert(d.battery == 77 && d.measurement_age_ms >= 15000);
    assert(!sensors->poll(sensors->context, old_token, 1));
    begin_scan(); assert(!read_status().count && active_token != old_token);
    assert(!sensors->device(sensors->context, 0, &d));
    assert(driver_api->quiesce() && !owned && releases == 2);
    driver_api->stop();
    assert(!sensors->device(sensors->context, 0, &d));
    puts("PASS: bounded completion, retained stale results, new-session reset and cleanup");
    puts("Generic sensor capability: 7 scenario groups passed; synthetic transport only");
}

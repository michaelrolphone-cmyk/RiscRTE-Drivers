#pragma once
#include "RiscTelemetryV1.h"
#define RISC_BLUETOOTH_SENSORS_CAPABILITY "bluetooth.sensors"
#define RISC_BLE_MAX_DEVICES 32u
#define RISC_BLE_MAX_READINGS 12u
enum { RISC_BLE_SENSOR_IDLE, RISC_BLE_SENSOR_STARTING, RISC_BLE_SENSOR_SCANNING,
 RISC_BLE_SENSOR_COMPLETE, RISC_BLE_SENSOR_ERROR };
typedef struct { uint32_t metric; int32_t value; } risc_ble_reading_v1;
typedef struct {
 uint8_t address[6],address_type,event_type,payload[31],payload_size;
 int8_t rssi; char name[32]; uint16_t services[8],company;
 uint8_t service_count,battery,bthome_version;
 bool has_company,bthome,encrypted,has_temperature,has_humidity,has_battery;
 int16_t temperature; uint16_t humidity; uint32_t seen,reports,measurement_seen;
 uint8_t reading_count; bool measurement_invalid,measurement_partial;
 risc_ble_reading_v1 readings[RISC_BLE_MAX_READINGS];
 uint32_t seen_age_ms,measurement_age_ms; /* copied at device() call */
} risc_ble_sensor_device_v1;
typedef struct {
 uint32_t struct_size,state,count,dropped,malformed;
 bool cleanup_pending,restore_failed;
 char error[48];
} risc_ble_sensor_status_v1;
/* An explicit open performs a bounded 15-second passive scan. Activation alone
 * never claims a radio. No connection, pairing, decryption, or GATT operation
 * occurs. Addresses are observed identities, not proof of trusted ownership.
 * Call poll regularly; close after COMPLETE/ERROR (and after failed open with
 * nonzero token). A failed close retains all code and dependency custody.
 * Data is copied; no app callback or pointer survives a call. Old results remain
 * readable until the next open. All entry points are nonblocking serialized. */
typedef struct {
 uint32_t api_version,struct_size; void *context;
 bool (*open)(void *, uint64_t *token);
 bool (*poll)(void *, uint64_t token, uint32_t max_events);
 bool (*status)(void *, risc_ble_sensor_status_v1 *out);
 bool (*device)(void *, uint32_t index, risc_ble_sensor_device_v1 *out);
 bool (*close)(void *, uint64_t token);
} risc_bluetooth_sensors_v1;

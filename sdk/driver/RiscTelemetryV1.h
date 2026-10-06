#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
/* Copied scalar values. Units are part of the metric, never board identity.
 * enumerate: 1 item, 0 end, -1 failure; IDs stay stable for a provider lifetime.
 * read: 1 current value, 0 temporarily unavailable, -1 failure. No cached value
 * may be presented as current. Implementations may expose a bounded subset.
 * The deployment chooses the source; consumers must not enumerate hardware. */
#define RISC_TELEMETRY_CAPABILITY "sensor.telemetry"
#define RISC_TELEMETRY_MAX_FIELDS 16u
enum {
 RISC_TELEMETRY_BATTERY_PERCENT=1, RISC_TELEMETRY_TEMPERATURE_CENTIC,
 RISC_TELEMETRY_HUMIDITY_CENTIPERCENT, RISC_TELEMETRY_PRESSURE_CENTIHPA,
 RISC_TELEMETRY_ILLUMINANCE_CENTILUX, RISC_TELEMETRY_VOLTAGE_MV,
 RISC_TELEMETRY_CHARGING
};
typedef struct { uint32_t id, metric; char label[32]; } risc_telemetry_field_v1;
typedef struct {
 uint32_t api_version, struct_size; void *context;
 int32_t (*enumerate)(void *, uint32_t index, risc_telemetry_field_v1 *out);
 int32_t (*read)(void *, uint32_t id, int32_t *value);
} risc_telemetry_v1;

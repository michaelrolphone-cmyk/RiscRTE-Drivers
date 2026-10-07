#pragma once
#include "RiscTelemetryV1.h"
#define RISC_BLUETOOTH_TELEMETRY_CAPABILITY "bluetooth.telemetry"
enum { RISC_BLE_TELEMETRY_OFF, RISC_BLE_TELEMETRY_STARTING,
 RISC_BLE_TELEMETRY_PUBLISHING, RISC_BLE_TELEMETRY_FAULT };
typedef struct {
 uint32_t struct_size,state; bool cleanup_pending,restore_failed;
 uint32_t updates; char error[48];
} risc_ble_telemetry_status_v1;
/* enumerate/read delegate only to the bound sensor.telemetry capability.
 * Explicit publish selects 1..16 IDs and requires public_broadcast=true: the
 * caller must explain that nearby receivers can read these values without
 * pairing. Only approved IDs may be passed. Activation/enumeration do not use
 * RF. Nonconnectable BTHome v2 packets contain no name or public MAC; a fresh
 * random-static address is generated through HCI LE Rand for each session.
 * This does not make public telemetry confidential or untrackable.
 * Poll regularly (at most 5 seconds apart). Values refresh every 5 seconds;
 * unknown, invalid, or changed source fields stop publication, never fake zero.
 * Failed publish may return a cleanup token. close=false retains all custody.
 * This advertising-only increment does not implement secure GATT sharing. */
typedef struct {
 uint32_t api_version,struct_size;void *context;
 int32_t (*enumerate)(void *,uint32_t index,risc_telemetry_field_v1 *out);
 int32_t (*read)(void *,uint32_t id,int32_t *value);
 bool (*publish)(void *,const uint32_t *ids,uint32_t count,bool public_broadcast,uint64_t *token);
 bool (*poll)(void *,uint64_t token,uint32_t max_events);
 bool (*status)(void *,risc_ble_telemetry_status_v1 *out);
 bool (*close)(void *,uint64_t token);
} risc_bluetooth_telemetry_v1;

#pragma once
#include "RiscBluetoothSensorsV1.h"
#include <stddef.h>
#define RISC_BLUETOOTH_SENSOR_LINK_TAG 0x534c4e31u
#define RISC_BLUETOOTH_SENSOR_LINK_VERSION 1u
enum { RISC_BLE_LINK_UNKNOWN=0, RISC_BLE_LINK_NOT_CONNECTED=1, RISC_BLE_LINK_CONNECTED=2 };
typedef struct {
 uint32_t struct_size,state;
 uint64_t generation,observed_ms;
 uint8_t address[6],address_type,reserved;
} risc_ble_sensor_link_state_v1;
/* Optional observational suffix on bluetooth.sensors@1. The original table and
 * device rows are unchanged. This is the selected peer's link TO THIS RECEIVER,
 * never a statement about links to other devices, ownership or authentication.
 * No connection, pairing, radio enable, scan or packet consumption is allowed.
 * Successful output is copied and scoped to the exact live scan token/address.
 * UNKNOWN has generation/observed_ms zero; known states carry token generation
 * and monotonic observation time. Prior evidence is invalid after close/error/
 * lease loss/new open. False leaves output unchanged and supplies no evidence.
 * Providers may establish NOT_CONNECTED from a healthy exclusive reset passive
 * host, but must invalidate that proof on unexpected connection/ACL events.
 * Availability is optional: an old/small table means UNKNOWN, not disconnected.
 */
typedef struct {
 risc_bluetooth_sensors_v1 sensors;
 uint32_t extension_tag,extension_version,extension_size;
 bool (*link)(void *,uint64_t,const uint8_t[6],uint8_t,risc_ble_sensor_link_state_v1 *);
} risc_bluetooth_sensor_link_api_v1;
/* 0 absent, 1 validated, -1 recognized but malformed extension. Never inspect
 * bytes beyond the prefix's declared size. A malformed advertised contract is
 * not permission to fall back to a stale or inferred connection state. */
static inline int risc_bluetooth_sensor_link_bind_v1(const risc_bluetooth_sensors_v1 *base,
 const risc_bluetooth_sensor_link_api_v1 **out) {
 if(out)*out=NULL;
 if(!out||!base||base->api_version!=1||base->struct_size<sizeof(*base))return -1;
 if(base->struct_size<offsetof(risc_bluetooth_sensor_link_api_v1,extension_tag)+sizeof(uint32_t))return 0;
 const risc_bluetooth_sensor_link_api_v1 *api=(const risc_bluetooth_sensor_link_api_v1 *)base;
 if(api->extension_tag!=RISC_BLUETOOTH_SENSOR_LINK_TAG)return 0;
 if(base->struct_size<sizeof(*api)||api->extension_version!=RISC_BLUETOOTH_SENSOR_LINK_VERSION||
    api->extension_size!=sizeof(*api)||!api->link)return -1;
 *out=api;return 1;
}

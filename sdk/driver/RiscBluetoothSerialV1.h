#pragma once
/* Copied, invocation-local BLE GATT serial client; no app callbacks or pointers
 * survive a call. Addresses use Bluetooth's six-byte little-endian order.
 * An unsuccessful open can return a cleanup token; close it before release.
 * poll is cooperative. A negative read/write or FAULT requires close/reopen.
 * This is BLE, not Bluetooth Classic SPP. No implicit pairing/bond replacement. */
#include "RiscProviderV2.h"
#define RISC_BLUETOOTH_SERIAL_CAPABILITY "bluetooth.serial"
#define RISC_SERIAL_MAX_PEERS 24u
#define RISC_SERIAL_MAX_CHANNELS 16u
#define RISC_SERIAL_MAX_WRITE 20u
#define RISC_SERIAL_KNOWN_UART 1u
#define RISC_SERIAL_UNAUTHENTICATED 2u
#define RISC_SERIAL_TX_BUSY 4u
#define RISC_SERIAL_CLEANUP_REQUIRED 8u
#define RISC_SERIAL_RELOAD_REQUIRED 16u
enum { RISC_SERIAL_OFF, RISC_SERIAL_STARTING, RISC_SERIAL_SCANNING,
       RISC_SERIAL_SCAN_DONE, RISC_SERIAL_CONNECTING, RISC_SERIAL_DISCOVERING,
       RISC_SERIAL_CHANNELS, RISC_SERIAL_SUBSCRIBING, RISC_SERIAL_READY,
       RISC_SERIAL_DISCONNECTED, RISC_SERIAL_FAULT };
typedef struct {
 uint32_t struct_size;
 uint8_t address[6], address_type, connectable;
 int8_t rssi;
 char name[32];
} risc_serial_peer_v1;
typedef struct {
 uint32_t struct_size, flags;
 uint16_t tx_handle, rx_handle;
 char service[37], tx_uuid[37], rx_uuid[37];
} risc_serial_channel_v1;
typedef struct {
 uint32_t struct_size, state, flags, peer_count, channel_count;
 uint32_t rx_bytes, tx_bytes, dropped_peers, generation;
 int32_t error_code;
 char message[72];
} risc_serial_status_v1;
typedef struct {
 uint32_t api_version, struct_size; void *context;
 bool (*open)(void *, uint64_t *token);
 bool (*poll)(void *, uint64_t, uint32_t work_limit);
 bool (*status)(void *, uint64_t, risc_serial_status_v1 *);
 bool (*peer)(void *, uint64_t, uint32_t index, risc_serial_peer_v1 *);
 bool (*connect)(void *, uint64_t, const risc_serial_peer_v1 *);
 bool (*channel)(void *, uint64_t, uint32_t index, risc_serial_channel_v1 *);
 bool (*select_channel)(void *, uint64_t, uint32_t index);
 /* write: full accepted byte count, zero backpressure, -1 error; max 20 bytes.
  * read: copied byte count, zero empty, -1 terminal error. Never blocks. */
 int32_t (*write)(void *, uint64_t, const uint8_t *, uint32_t size);
 int32_t (*read)(void *, uint64_t, uint8_t *, uint32_t capacity);
 bool (*close)(void *, uint64_t);
} risc_bluetooth_serial_v1;

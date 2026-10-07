#pragma once
#include <stdbool.h>
#include "RiscBoundKeyValueV1.h"
#include "host/ble_store.h"
bool hid_store_init(const risc_bound_key_value_v1 *);
bool hid_store_forget(void);
bool hid_store_bonded(void);
bool hid_store_failed(void);
bool hid_store_peer(const ble_addr_t *);
int hid_store_read(int, const union ble_store_key *, union ble_store_value *);
int hid_store_write(int, const union ble_store_value *);
int hid_store_delete(int, const union ble_store_key *);
int hid_store_key(uint8_t, struct ble_store_gen_key *, uint16_t);
void hid_store_clear(void);

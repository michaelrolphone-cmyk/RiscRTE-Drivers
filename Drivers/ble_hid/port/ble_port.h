#pragma once
#include "RiscBluetoothHostV1.h"
#include "RiscPlatformClockV1.h"
void hid_port_bind(const portable_bluetooth_host_v1 *, const risc_platform_clock_api_v1 *,
                   uint64_t);
bool hid_port_receive(void);
void hid_port_timers(void);
void hid_port_clear(void);
bool hid_port_faulted(void);
uint32_t hid_port_fault_reason(void);
void hid_port_fault(void);
void *hid_malloc(size_t);
void *hid_calloc(size_t, size_t);
void *hid_realloc(void *, size_t);
void hid_free(void *);
void hid_panic(void) __attribute__((noreturn));

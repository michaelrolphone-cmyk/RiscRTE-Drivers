#pragma once
#include "RiscProviderV2.h"
#define RISC_BLUETOOTH_HID_CAPABILITY "bluetooth.hid"
#define RISC_BLUETOOTH_HID_API_V1 1u
enum {
    RISC_HID_OFF,
    RISC_HID_STARTING,
    RISC_HID_ADVERTISING,
    RISC_HID_CONNECTED,
    RISC_HID_PAIR_CONFIRM,
    RISC_HID_READY,
    RISC_HID_FAULT
};
enum {
    RISC_HID_KEYBOARD_READY = 1,
    RISC_HID_MOUSE_READY = 2,
    RISC_HID_ENCRYPTED = 4,
    RISC_HID_AUTHENTICATED = 8,
    RISC_HID_BONDED = 16
};
typedef struct {
    uint32_t struct_size, state, flags, pairing_number;
    int32_t error;
    uint32_t connection_generation;
} risc_bluetooth_hid_status_v1;
/* Cooperative, serialized owner-task API. No application pointer or callback
 * survives a call. open copies a printable ASCII name of 1..20 characters.
 * Pairing is Secure Connections with explicit numeric comparison; a saved
 * peer reconnects without accepting a new peer. Forget is explicit, while OFF.
 * A failed open may return a cleanup token. close must be retried until true;
 * false retains the lease, provider and dependency custody. Reports are accepted
 * only on authenticated encrypted subscribed connections. Input reports are
 * neutral on every new connection. Call poll at least every 20 ms while active;
 * report state is watchdog-released after 1000 ms without a report update.
 * Hardware and host interoperability qualification remain a deployment task. */
typedef struct {
    uint32_t api_version, struct_size;
    void *context;
    bool (*open)(void *, const char *name, bool allow_pairing, uint64_t *token);
    bool (*poll)(void *, uint64_t token, uint32_t max_events);
    bool (*status)(void *, uint64_t token, risc_bluetooth_hid_status_v1 *out);
    bool (*confirm_pairing)(void *, uint64_t token, bool accept);
    bool (*keyboard)(void *, uint64_t token, uint8_t modifiers, const uint8_t keys[6]);
    bool (*mouse)(void *, uint64_t token, uint8_t buttons, int8_t dx, int8_t dy, int8_t wheel);
    bool (*release_all)(void *, uint64_t token);
    bool (*close)(void *, uint64_t token);
    bool (*forget_bond)(void *);
    bool (*battery)(void *, uint64_t token, uint8_t percent);
} risc_bluetooth_hid_v1;

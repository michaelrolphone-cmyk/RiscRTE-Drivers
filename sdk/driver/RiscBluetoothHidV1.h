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

/* Optional copied diagnostics; the original API prefix and status stay intact.
 * Contains only stages, result codes and counters, never keys, peer addresses,
 * pairing values or input report contents. Snapshot before close for the cause;
 * snapshot after successful close for the independent cleanup outcome. */
enum { RISC_HID_PORT_OK, RISC_HID_PORT_INTERNAL, RISC_HID_PORT_QUEUE_FULL,
       RISC_HID_PORT_SEND_COMMAND, RISC_HID_PORT_SEND_ACL, RISC_HID_PORT_RECEIVE,
       RISC_HID_PORT_EVENT_POOL, RISC_HID_PORT_EVENT_DISPATCH,
       RISC_HID_PORT_ACL_POOL, RISC_HID_PORT_ACL_APPEND,
       RISC_HID_PORT_ACL_DISPATCH, RISC_HID_PORT_PACKET };
typedef struct {
    uint32_t struct_size, port_fault, disconnect_reason, security_status;
    int32_t notify_error, host_stop_error, native_close_result;
    uint32_t notify_failures, stale_events, max_poll_gap_ms, poisoned, host_stopped;
} risc_bluetooth_hid_diagnostics_v1;
typedef struct {
    risc_bluetooth_hid_v1 base;
    bool (*diagnostics)(void *context, uint64_t token, risc_bluetooth_hid_diagnostics_v1 *out);
} risc_bluetooth_hid_diagnostics_api_v1;

/* Optional append-only two-axis mouse report extension. Check the original
 * base.struct_size >= sizeof(risc_bluetooth_hid_scroll_api_v1) and a non-NULL
 * mouse_scroll before calling. Both the original API and diagnostics prefix
 * remain unchanged. dx/dy/wheel_x/wheel_y saturate independently to -127..127.
 * wheel_x is Consumer AC Pan (positive right); wheel_y is Generic Desktop
 * Wheel (positive away/up). Host scroll preferences may reverse either axis.
 * Boot protocol rejects either nonzero wheel and buttons beyond its three-
 * button mask. All session, readiness, FIFO and cleanup rules still apply.
 * Relative deltas are sent once and are never retained for report reads. */
typedef struct {
    risc_bluetooth_hid_diagnostics_api_v1 base;
    bool (*mouse_scroll)(void *context, uint64_t token, uint8_t buttons,
                         int16_t dx, int16_t dy, int16_t wheel_x, int16_t wheel_y);
} risc_bluetooth_hid_scroll_api_v1;

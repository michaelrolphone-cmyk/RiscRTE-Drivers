#pragma once
#include "RiscProviderV2.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RISC_BLUETOOTH_SESSION_SETUP_CAPABILITY "bluetooth.session-setup"
#define RISC_BLUETOOTH_SESSION_SETUP_API_V1 1u
#define RISC_SESSION_SETUP_MAX_LIFETIME_MS 300000u
#define RISC_SESSION_SETUP_SERVICE_UUID "cc12f001-6b62-4c86-a72d-1b4864247521"
#define RISC_SESSION_SETUP_DESCRIPTOR_UUID "cc12f002-6b62-4c86-a72d-1b4864247521"
#define RISC_SESSION_SETUP_VALIDITY_UUID "cc12f003-6b62-4c86-a72d-1b4864247521"
enum { RISC_SESSION_HTTP = 1, RISC_SESSION_HTTPS = 2 };
enum { RISC_SETUP_OK = 0, RISC_SETUP_PENDING = 1, RISC_SETUP_BUSY = 2, RISC_SETUP_CLEANUP_PENDING = 3,
       RISC_SETUP_INVALID = -1, RISC_SETUP_CONTEXT = -2, RISC_SETUP_RETAINED = -3,
       RISC_SETUP_FAULT = -4, RISC_SETUP_EXPIRED = -5 };
enum { RISC_SETUP_OFF, RISC_SETUP_STARTING, RISC_SETUP_ADVERTISING,
       RISC_SETUP_CONNECTED, RISC_SETUP_PAIR_CONFIRM, RISC_SETUP_READY,
       RISC_SETUP_ENDED, RISC_SETUP_FAILED, RISC_SETUP_CLOSING };
enum { RISC_SETUP_ENCRYPTED = 1, RISC_SETUP_AUTHENTICATED = 2,
       RISC_SETUP_NUMERIC_CONFIRMED = 4, RISC_SETUP_READABLE = 8 };
typedef struct {
    uint32_t struct_size, transport;
    uint32_t remaining_lifetime_ms; /* WebDAV remaining duration sampled just before open. */
    char url[257], username[65], password[65], session_id[65];
} risc_bluetooth_session_descriptor_v1;
typedef struct {
    uint32_t struct_size, state, flags, pairing_number;
    uint32_t pairing_generation, connection_generation, remaining_ms;
    int32_t error, native_close_result;
} risc_bluetooth_session_setup_status_v1;
/* Serialized, cooperative owner-task API, without caller pointers/callbacks
 * retained after calls. Provider start requires the validated scheduler-only
 * platform.clock suffix from RiscPlatformClockWaitV1.h; legacy clocks reject
 * before any HCI claim. open copies name (1..20 printable ASCII) and descriptor.
 * URL: matching http(s) scheme, nonempty authority, no userinfo, controls,
 * whitespace or fragment. Credentials and session ID: 1..64 printable ASCII.
 * The caller must first establish a ready, authorized WebDAV session.
 * BLE never starts Wi-Fi, grants exports, sends file bytes, or closes WebDAV.
 * setup_lifetime_ms is 1..300000, capped by the copied WebDAV remaining
 * duration. One connection per setup session; disconnect/rejection/fault/expiry destroys the descriptor.
 * Credentials require 16-byte encrypted authenticated LE Secure Connections and
 * explicit confirmation of this exact pairing_generation and displayed_number.
 * Pairing is ephemeral: no persistent store, bonds, or key distribution.
 * Poll at least every 20 ms. ATT checks deadlines independently of poll.
 * OK/PENDING mean operation accepted, BUSY means serialized invocation occupied.
 * CONTEXT is terminal for stale/unknown tokens; do not use that token again.
 * FAULT/EXPIRED with a valid token still require close. Invalid numeric matching
 * returns INVALID without changing the pending comparison.
 * A failed open may return a nonzero cleanup token. CLEANUP_PENDING means
 * native close is unproven: retain token, module and dependencies; only close may retry.
 * poll/status/confirm reject CLEANUP_PENDING without entering host or transport.
 * Each refused close cooperates via the required scheduler-only clock suffix,
 * never legacy sleep_ms. After a native release attempt, retries never pump HCI.
 * RETAINED means the owner-checked scheduler wait explicitly rejected a valid
 * delay. Custody is terminal: no further provider call is legal; all API calls
 * reject without host/clock access and quiesce remains false. Keep module and
 * dependencies pinned. Generic BUSY/transport failures never imply owner loss.
 * Successful close (OK) proves native release, invalidates the token and permits unload.
 * No status or error contains credentials. status(0) is accepted only while OFF.
 */
typedef struct {
    uint32_t api_version, struct_size;
    void *context;
    int32_t (*open)(void *, const char *name,
                    const risc_bluetooth_session_descriptor_v1 *,
                    uint32_t setup_lifetime_ms, uint64_t *token);
    int32_t (*poll)(void *, uint64_t token, uint32_t max_events);
    int32_t (*status)(void *, uint64_t token, risc_bluetooth_session_setup_status_v1 *out);
    int32_t (*confirm_pairing)(void *, uint64_t token, uint32_t pairing_generation,
                               uint32_t displayed_number, bool accept);
    int32_t (*close)(void *, uint64_t token);
} risc_bluetooth_session_setup_v1;
#ifdef __cplusplus
}
#endif

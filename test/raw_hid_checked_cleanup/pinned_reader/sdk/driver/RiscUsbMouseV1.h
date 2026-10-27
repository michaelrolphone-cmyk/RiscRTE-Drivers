#pragma once
/* Additive driver-only copied pointer input ABI. No changes to existing HID
 * prefixes. Calls are serialized by the provider executor; no app pointers or
 * callbacks are retained. The consumer owns cursor position/acceleration. */
#include "RiscUsbHidV1.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RISC_USB_MOUSE_API_V1 1u
#define RISC_USB_MOUSE_MAX_DEVICES 4u
#define RISC_USB_MOUSE_CONNECTED 1u
#define RISC_USB_MOUSE_DISCONNECTED 2u
#define RISC_USB_MOUSE_REPORT 3u
#define RISC_USB_MOUSE_GAP 5u
#define RISC_USB_MOUSE_STOPPED 0u
#define RISC_USB_MOUSE_RUNNING 1u
#define RISC_USB_MOUSE_CLOSING 2u
#define RISC_USB_MOUSE_RETAINED 3u

typedef struct {
    uint64_t device, session;
    uint32_t buttons; /* bit zero = HID Button 1 */
    uint8_t interface_number, alternate, connected, reserved;
} risc_usb_mouse_state_v1;
typedef struct {
    uint64_t sequence;
    risc_usb_mouse_state_v1 state;
    int32_t x, y, wheel, pan; /* signed relative counts, never accumulated */
    uint32_t pressed, released;
    uint8_t kind, reserved[7];
} risc_usb_mouse_event_v1;
typedef struct {
    uint32_t api_version, struct_size;
    void *context;
    uint64_t (*subscribe)(void *context, uint64_t device_or_zero);
    bool (*unsubscribe)(void *context, uint64_t subscription);
    /* max_reports 1..16; one discovery attempt, <=20 ms report work, then an
     * actual 1 ms scheduler yield. Raw HID descriptor/control calls have their
     * own 100 ms bound. False includes transport/descriptor/report failures,
     * close pending, or terminal retention. Always inspect status/snapshot. */
    bool (*poll)(void *context, size_t max_reports);
    /* 1 copied event, 0 empty, -1 invalid handle/output, -2 sticky GAP (out is
     * initialized to GAP). GAP means lost relative motion AND button history;
     * discard assumptions and call snapshot. Snapshot cannot recover motion.
     * New subscriptions start in GAP. Unsubscribe and next remain local and
     * usable during retained cleanup; they never call a lower provider. */
    int32_t (*next)(void *context, uint64_t subscription,
                    risc_usb_mouse_event_v1 *out);
    /* Atomically replace this subscription's baseline with connected states
     * matching its filter, discard its queued history, and acknowledge GAP.
     * Insufficient capacity writes required count and does NOT acknowledge.
     * Session is an opaque semantic generation, not a raw HID session token.
     * A missing session means release every button previously held for it. */
    bool (*snapshot)(void *context, uint64_t subscription,
                      risc_usb_mouse_state_v1 *out, size_t *inout_count);
    /* RUNNING may include unsupported/failed interfaces; it means the provider
     * can operate, not that a mouse or physical host is qualified. CLOSING only
     * retries close; RETAINED makes zero further dependency calls and prevents
     * unload/restart. Dependencies must remain pinned until reboot. */
    uint32_t (*status)(void *context);
} risc_usb_mouse_api_v1;
#ifdef __cplusplus
}
#endif

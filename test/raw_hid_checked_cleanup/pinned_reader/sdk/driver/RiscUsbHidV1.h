#pragma once
/* Driver-only USB HID composition ABI. The hardware-blind runtime treats all
 * capability strings and pointers here as opaque, versioned provider data.
 * Calls are serialized by the provider executor. Subscribers use copied events
 * and opaque cursors, never pointers/callbacks into unloadable app modules. */
#include "RiscUsbControllerV1.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RISC_USB_HID_API_V1 1u
#define RISC_USB_KEYBOARD_API_V1 1u
#define RISC_USB_GAMEPAD_API_V1 1u
#define RISC_USB_HID_MAX_INTERFACES 16u
#define RISC_USB_HID_MAX_SESSIONS 8u
#define RISC_USB_HID_MAX_REPORT 64u
#define RISC_USB_HID_MAX_DESCRIPTOR 512u
#define RISC_USB_INPUT_MAX_SUBSCRIBERS 4u
#define RISC_USB_INPUT_QUEUE_LENGTH 32u

/* Device is the usb.host generation-qualified identity. Interface/alternate
 * select a single independently claimed HID function on composite devices. */
typedef struct {
    uint64_t device;
    uint16_t vid, pid;
    uint16_t report_descriptor_length;
    uint8_t interface_number, alternate, subclass, protocol;
    uint8_t interrupt_in, interval;
    uint16_t max_packet;
} risc_usb_hid_interface_v1;

typedef struct {
    uint32_t api_version, struct_size;
    void *context;
    bool (*scan)(void *context, size_t max_host_events);
    bool (*interfaces)(void *context, risc_usb_hid_interface_v1 *out,
                       size_t *inout_count);
    uint64_t (*open)(void *context, uint64_t device, uint8_t interface_number,
                     uint8_t alternate);
    bool (*report_descriptor)(void *context, uint64_t session,
                              uint8_t *out, size_t *inout_length);
    bool (*set_boot_protocol)(void *context, uint64_t session, bool boot);
    int32_t (*read)(void *context, uint64_t session, uint8_t *out,
                    size_t capacity, uint32_t timeout_ms);
    bool (*present)(void *context, uint64_t session);
    bool (*close)(void *context, uint64_t session);
} risc_usb_hid_api_v1;

/* Event kinds: 1=connected, 2=disconnected, 3=key down, 4=key up,
 * 5=GAP (subscriber must call snapshot). Usage is HID page 0x07, including
 * modifiers 0xe0..0xe7. Sequence is monotonically increasing per provider. */
typedef struct {
    uint64_t sequence, device;
    uint8_t kind, usage, modifiers, reserved;
} risc_usb_keyboard_event_v1;
typedef struct {
    uint64_t device;
    uint8_t modifiers, keys[6], connected;
} risc_usb_keyboard_state_v1;
typedef struct {
    uint32_t api_version, struct_size;
    void *context;
    uint64_t (*subscribe)(void *context, uint64_t device_or_zero);
    bool (*unsubscribe)(void *context, uint64_t subscription);
    bool (*poll)(void *context, size_t max_reports);
    /* 1 event, 0 empty, -1 stale handle or overflow/GAP; on -2 provider fault.
     * GAP is also copied as an event when space permits; always resnapshot. */
    int32_t (*next)(void *context, uint64_t subscription,
                    risc_usb_keyboard_event_v1 *out);
    bool (*snapshot)(void *context, risc_usb_keyboard_state_v1 *out,
                     size_t *inout_count);
} risc_usb_keyboard_api_v1;

/* Gamepads expose current state: call poll(), then snapshot(). No historical
 * button sequence is retained. Legacy next() coalesces changes per device,
 * including connect/disconnect, and is not an ordered input journal. Keyboard
 * next() remains an ordered event queue. Button bit zero is
 * HID Button 1. Axes are normalized to signed 16-bit; hat is 0..7 or 8=none.
 * Kinds: 1=connect, 2=disconnect, 3=state, 5=GAP. */
typedef struct {
    uint64_t device;
    uint32_t buttons;
    int16_t x, y, z, rx, ry, rz;
    uint8_t hat, report_id, connected, reserved;
} risc_usb_gamepad_state_v1;
typedef struct {
    uint64_t sequence;
    uint8_t kind;
    uint8_t reserved[7];
    risc_usb_gamepad_state_v1 state;
} risc_usb_gamepad_event_v1;
typedef struct {
    uint32_t api_version, struct_size;
    void *context;
    uint64_t (*subscribe)(void *context, uint64_t device_or_zero);
    bool (*unsubscribe)(void *context, uint64_t subscription);
    bool (*poll)(void *context, size_t max_reports);
    int32_t (*next)(void *context, uint64_t subscription,
                    risc_usb_gamepad_event_v1 *out);
    bool (*snapshot)(void *context, risc_usb_gamepad_state_v1 *out,
                     size_t *inout_count);
} risc_usb_gamepad_api_v1;
#ifdef __cplusplus
}
#endif

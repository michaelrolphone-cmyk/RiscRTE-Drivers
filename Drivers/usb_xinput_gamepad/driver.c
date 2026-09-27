#include "RiscUsbInterruptV1.h"
#include "RiscUsbGamepadDiagnosticsV1.h"
#include "RiscPlatformClockV1.h"

/* Xbox 360 wired/wireless-format input, including 2.4 GHz receivers advertising
 * 045e:028e. USB ownership stays with usb.host; no HID requests, firmware
 * callbacks, USB reset, output effects or controller-specific power changes.
 * Protocol facts: Linux drivers/input/joystick/xpad.c (XTYPE_XBOX360).
 * This is an independent implementation of the documented packet layout. */
#define PADS 4u
typedef struct {
    uint64_t device, claim;
    uint8_t endpoint;
    bool wireless;
    risc_usb_gamepad_state_v1 state;
} gamepad;
#include "../usb_hid_gamepad/StateMailbox.h"
/* Discovery is incremental: one attempt per poll, at most eight attempts in
 * ten seconds per attachment. Backoff is elapsed time, never a busy wait.
 * The caller returns to its scheduler between polls (as for interrupt input).
 * Cache successful descriptors; only a failed read/claim is retried. */
#define DISCOVERY_ATTEMPTS 8u
#define DISCOVERY_DEADLINE_MS 10000u
typedef struct {
    uint64_t device, begun_ms, attempted_ms;
    const char *status;
    uint8_t attempts, iface, alternate, endpoint;
    bool retry, configured, wireless, waiting_capacity;
} inspected_device;
static const risc_usb_host_interrupt_v1 *host;
static const risc_platform_clock_api_v1 *clock_api;
static gamepad pads[PADS];
static inspected_device inspected[RISC_USB_HOST_MAX_DEVICES];
static subscriber subscribers[RISC_USB_INPUT_MAX_SUBSCRIBERS];
static uint64_t serial, sequence;
static const char *status = "XINPUT DRIVER NOT STARTED";

static bool equal(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static bool diagnostic(void *ctx, char *out, size_t capacity) {
    (void)ctx;
    if (!out || !capacity) return false;
    size_t i = 0;
    for (; i + 1 < capacity && status[i]; ++i) out[i] = status[i];
    out[i] = 0; return true;
}
static bool emit(uint8_t kind, const risc_usb_gamepad_state_v1 *state) {
    if (sequence == UINT64_MAX) return false;
    risc_usb_gamepad_event_v1 event = {0};
    event.sequence = ++sequence; event.kind = kind; event.state = *state;
    for (size_t i = 0; i < RISC_USB_INPUT_MAX_SUBSCRIBERS; ++i)
        mailbox_publish(&subscribers[i], &event);
    return true;
}
static uint16_t le16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static int16_t invert_y(int16_t value) { return (int16_t)~value; }
static bool apply(gamepad *pad, const uint8_t *data, size_t length) {
    if (!data || length > 64) return true;
    if (pad->wireless) {
        if (length < 2) return true;
        if ((data[0] & 8u) && !(data[1] & 0x80u)) {
            if (!pad->state.connected) return true;
            pad->state = (risc_usb_gamepad_state_v1){0};
            pad->state.device = pad->device; pad->state.hat = 8;
            return emit(2, &pad->state);
        }
        if (data[1] != 1) {
            if ((data[0] & 8u) && (data[1] & 0x80u) && !pad->state.connected) {
                pad->state.connected = 1;
                return emit(1, &pad->state);
            }
            return true;
        }
        if (length < 24) return true;
        data += 4; length -= 4;
    }
    /* Ignore LED/status messages and partial/corrupt input without releasing
     * held keys or reading beyond the transfer's actual length. Match the
     * hardware-tested standalone decoder: 20 received bytes, header >=14. */
    if (length < 20 || data[0] != 0 || data[1] < 14) return true;
    risc_usb_gamepad_state_v1 next = {0};
    next.device = pad->device; next.connected = 1; next.hat = 8;
    // Match standalone named buttons: B/A/Y/X, L/R, triggers, Back/Start,
    // stick clicks and Guide. Do not swap Xbox A/B or X/Y by physical position.
    next.buttons = ((uint32_t)(data[3] & 0xa0u) >> 5) |
        ((uint32_t)(data[3] & 0x50u) >> 3) | ((uint32_t)(data[3] & 3u) << 4) |
        (data[4] >= 128 ? 1u << 6 : 0) | (data[5] >= 128 ? 1u << 7 : 0) |
        ((data[2] & 0x20u) ? 1u << 8 : 0) | ((data[2] & 0x10u) ? 1u << 9 : 0) |
        ((uint32_t)(data[2] & 0xc0u) << 4) | ((uint32_t)(data[3] & 4u) << 10);
    const int dx = !!(data[2] & 8u) - !!(data[2] & 4u);
    const int dy = !!(data[2] & 2u) - !!(data[2] & 1u);
    if (dy < 0) next.hat = dx < 0 ? 7 : dx > 0 ? 1 : 0;
    else if (dy > 0) next.hat = dx < 0 ? 5 : dx > 0 ? 3 : 4;
    else if (dx) next.hat = dx < 0 ? 6 : 2;
    next.x = (int16_t)le16(data + 6); next.y = invert_y((int16_t)le16(data + 8));
    next.rx = (int16_t)le16(data + 10); next.ry = invert_y((int16_t)le16(data + 12));
    next.z = (int16_t)((int)data[4] * 257 - 32768);
    next.rz = (int16_t)((int)data[5] * 257 - 32768);
    const bool first = !pad->state.connected;
    const bool changed = first || next.buttons != pad->state.buttons ||
        next.hat != pad->state.hat || next.x != pad->state.x || next.y != pad->state.y ||
        next.z != pad->state.z || next.rx != pad->state.rx || next.ry != pad->state.ry ||
        next.rz != pad->state.rz;
    pad->state = next;
    if (first && !emit(1, &next)) return false;
    return !changed || emit(3, &next);
}
static bool release_pad(gamepad *pad) {
    if (!pad->claim) return true;
    if (pad->state.connected) {
        pad->state = (risc_usb_gamepad_state_v1){0};
        pad->state.device = pad->device; pad->state.hat = 8;
        if (!emit(2, &pad->state)) return false;
    }
    // The host retains failed/pending physical releases and pins its controller
    // until they drain. No completion callback points into this class ELF.
    host->discovery.host.release(host->discovery.host.context, pad->claim);
    *pad = (gamepad){0}; return true;
}
static bool present(const uint64_t *devices, size_t count, uint64_t device) {
    for (size_t i = 0; i < count; ++i) if (devices[i] == device) return true;
    return false;
}
static bool input_interface(const uint8_t *data, size_t length,
                            uint8_t *interface_number, uint8_t *alternate,
                            uint8_t *endpoint, bool *wireless) {
    if (length < 9 || length > RISC_USB_CONFIG_LIMIT || data[0] < 9 ||
        data[1] != 2 || le16(data + 2) != length) return false;
    bool selected = false;
    *endpoint = 0;
    for (size_t at = 0; at < length;) {
        if (length - at < 2) return false;
        const uint8_t size = data[at], type = data[at + 1];
        if (size < 2 || size > length - at) return false;
        if (type == 4) {
            if (selected && *endpoint) return true;
            selected = size >= 9 && data[at + 5] == 0xff && data[at + 6] == 0x5d &&
                       (data[at + 7] == 1 || data[at + 7] == 0x81);
            if (selected) {
                *interface_number = data[at + 2]; *alternate = data[at + 3];
                *wireless = data[at + 7] == 0x81;
            }
        } else if (type == 5 && selected) {
            if (size < 7) return false;
            const uint8_t address = data[at + 2];
            if ((data[at + 3] & 3u) == 3 && (address & 0x80u)) {
                const uint16_t packet = le16(data + at + 4);
                if (*endpoint || !(address & 15u) || (address & 0x70u) ||
                    packet < 20 || packet > 64 || !data[at + 6]) return false;
                *endpoint = address;
            }
        }
        at += size;
    }
    return selected && *endpoint;
}
static void discovery_failed(inspected_device *slot, const char *retrying,
                             const char *failed) {
    slot->retry = slot->attempts < DISCOVERY_ATTEMPTS;
    slot->status = slot->retry ? retrying : failed;
}
static bool poll(void *ctx, size_t max_reports) {
    (void)ctx;
    if (!host || !max_reports || max_reports > 16) return false;
    const risc_usb_host_api_v1 *bus = &host->discovery.host;
    size_t processed = 0, count = RISC_USB_HOST_MAX_DEVICES;
    uint64_t devices[RISC_USB_HOST_MAX_DEVICES];
    status = "XINPUT USB DISCOVERY FAILED";
    if (!host->discovery.poll(bus->context, 8, &processed) ||
        !host->discovery.devices(bus->context, devices, &count) ||
        count > RISC_USB_HOST_MAX_DEVICES) return false;
    for (size_t i = 0; i < PADS; ++i)
        if (pads[i].claim && !present(devices, count, pads[i].device) && !release_pad(&pads[i]))
            return false;
    for (size_t i = 0; i < RISC_USB_HOST_MAX_DEVICES; ++i)
        if (inspected[i].device && !present(devices, count, inspected[i].device))
            inspected[i] = (inspected_device){0};
    const uint64_t now = clock_api->monotonic_ms(clock_api->context);
    status = "NO XINPUT DEVICE DISCOVERED";
    for (size_t i = 0; i < count; ++i) {
        inspected_device *slot = 0, *empty = 0;
        for (size_t j = 0; j < RISC_USB_HOST_MAX_DEVICES; ++j) {
            if (inspected[j].device == devices[i]) slot = &inspected[j];
            if (!inspected[j].device && !empty) empty = &inspected[j];
        }
        if (!slot) {
            if (!empty) continue;
            slot = empty;
            *slot = (inspected_device){.device = devices[i], .retry = true};
        }
        if (!slot->retry) continue;
        // A broken clock must not turn backoff into repeated USB operations.
        if (now == UINT64_MAX || (slot->attempts && now < slot->attempted_ms)) continue;
        gamepad *pad = 0;
        for (size_t j = 0; j < PADS; ++j) if (!pads[j].claim) { pad = &pads[j]; break; }
        if (slot->waiting_capacity) {
            if (!pad) continue; // Resume only when the resource actually changes.
            slot->waiting_capacity = false;
            slot->attempts = 0;
        }
        if (slot->attempts) {
            if (now - slot->begun_ms >= DISCOVERY_DEADLINE_MS) {
                slot->retry = false;
                slot->status = "XINPUT DISCOVERY RETRY DEADLINE";
                continue;
            }
            uint32_t delay_ms = 100u << (slot->attempts - 1);
            if (delay_ms > 2000u) delay_ms = 2000u;
            if (now - slot->attempted_ms < delay_ms) continue;
        } else slot->begun_ms = now;
        slot->attempted_ms = now;
        ++slot->attempts;
        if (!slot->configured) {
            uint8_t config[RISC_USB_CONFIG_LIMIT];
            size_t length = sizeof(config);
            uint16_t vid = 0, pid = 0;
            if (!bus->configuration(bus->context, devices[i], config, &length, &vid, &pid)) {
                discovery_failed(slot, "XINPUT CONFIGURATION READ RETRYING",
                                  "XINPUT CONFIGURATION READ RETRY LIMIT");
                break;
            }
            if (!input_interface(config, length, &slot->iface, &slot->alternate,
                                 &slot->endpoint, &slot->wireless)) {
                slot->retry = false; // Unsupported descriptors do not become valid later.
                slot->status = 0;
                break;
            }
            slot->configured = true;
        }
        if (!pad) {
            slot->waiting_capacity = true;
            slot->status = "XINPUT WAITING FOR FREE PAD SLOT";
            break;
        }
        uint64_t claim = 0;
        if (!bus->claim(bus->context, devices[i], slot->iface, slot->alternate, &claim) || !claim) {
            discovery_failed(slot, "XINPUT INTERFACE CLAIM RETRYING",
                              "XINPUT INTERFACE CLAIM RETRY LIMIT");
        } else {
            *pad = (gamepad){0};
            pad->device = devices[i]; pad->claim = claim; pad->endpoint = slot->endpoint;
            pad->wireless = slot->wireless;
            pad->state.device = devices[i]; pad->state.hat = 8;
            slot->retry = false;
            slot->status = 0;
        }
        break;
    }
    for (size_t i = 0; i < RISC_USB_HOST_MAX_DEVICES; ++i)
        if (inspected[i].status) status = inspected[i].status;
    if (now == UINT64_MAX) status = "XINPUT DISCOVERY CLOCK FAILED";
    bool quiet[PADS] = {0};
    size_t attempted = 0;
    for (size_t round = 0; round < max_reports && attempted < max_reports; ++round) {
        bool active = false;
        for (size_t i = 0; i < PADS && attempted < max_reports; ++i) {
            gamepad *pad = &pads[i];
            if (!pad->claim || quiet[i]) continue;
            active = true;
            uint8_t report[64];
            // Drain already completed transfers without waiting for future input.
            // max_reports bounds work; current state replaces earlier input.
            int32_t length = host->interrupt_read(bus->context, pad->claim, pad->endpoint,
                                                  report, sizeof(report), 10);
            ++attempted;
            if (length < 0 || length > (int32_t)sizeof(report)) {
                // Do not leave buttons held through a transport failure.
                // Retain the claim so a later valid packet can reconnect.
                if (pad->state.connected) {
                    pad->state = (risc_usb_gamepad_state_v1){0};
                    pad->state.device = pad->device; pad->state.hat = 8;
                    if (!emit(2, &pad->state)) return false;
                }
                status = "XINPUT INTERRUPT READ FAILED"; return false;
            }
            if (!length) quiet[i] = true;
            else if (!apply(pad, report, (size_t)length)) return false;
        }
        if (!active) break;
    }
    for (size_t i = 0; i < PADS; ++i)
        if (pads[i].claim) {
            status = pads[i].state.connected ? "XINPUT GAMEPAD CONNECTED" : "XINPUT WAITING FOR REPORT";
            if (pads[i].state.connected) break;
        }
    return true;
}
static uint64_t subscribe(void *ctx, uint64_t filter) {
    (void)ctx;
    if (!host || serial == UINT64_MAX) return 0;
    for (size_t i = 0; i < RISC_USB_INPUT_MAX_SUBSCRIBERS; ++i)
        if (!subscribers[i].token) {
            subscribers[i] = (subscriber){0};
            subscribers[i].token = ++serial; subscribers[i].filter = filter;
            return subscribers[i].token;
        }
    return 0;
}
static bool unsubscribe(void *ctx, uint64_t token) {
    (void)ctx;
    if (!token) return false;
    for (size_t i = 0; i < RISC_USB_INPUT_MAX_SUBSCRIBERS; ++i)
        if (subscribers[i].token == token) { subscribers[i] = (subscriber){0}; return true; }
    return false;
}
static int32_t next(void *ctx, uint64_t token, risc_usb_gamepad_event_v1 *out) {
    (void)ctx;
    if (!host || !token || !out) return -1;
    for (size_t i = 0; i < RISC_USB_INPUT_MAX_SUBSCRIBERS; ++i) {
        subscriber *s = &subscribers[i];
        if (s->token != token) continue;
        return mailbox_take(s, out);
    }
    return -1;
}
static bool snapshot(void *ctx, risc_usb_gamepad_state_v1 *out, size_t *capacity) {
    (void)ctx;
    if (!host || !capacity) return false;
    size_t count = 0;
    for (size_t i = 0; i < PADS; ++i) if (pads[i].claim) ++count;
    if (*capacity < count || (count && !out)) { *capacity = count; return false; }
    size_t n = 0;
    for (size_t i = 0; i < PADS; ++i) if (pads[i].claim) out[n++] = pads[i].state;
    *capacity = count; return true;
}
static bool start(const risc_provider_dependency_v1 *deps, size_t count) {
    if (host || !deps || count != 2) return false;
    const risc_usb_host_interrupt_v1 *api = 0;
    const risc_platform_clock_api_v1 *clock = 0;
    for (size_t i = 0; i < count; ++i) {
        if (equal(deps[i].capability_id, "usb.host") &&
            deps[i].api_version == RISC_USB_HOST_API_V1 && !api)
            api = (const risc_usb_host_interrupt_v1 *)deps[i].api;
        else if (equal(deps[i].capability_id, "platform.clock") &&
                 deps[i].api_version == RISC_PLATFORM_CLOCK_API_V1 && !clock)
            clock = (const risc_platform_clock_api_v1 *)deps[i].api;
        else return false;
    }
    if (!api || api->discovery.host.api_version != RISC_USB_HOST_API_V1 ||
        api->discovery.host.struct_size < sizeof(*api) || !api->discovery.poll ||
        !api->discovery.devices || !api->discovery.host.configuration ||
        !api->discovery.host.claim || !api->discovery.host.release || !api->interrupt_read ||
        !clock || clock->api_version != RISC_PLATFORM_CLOCK_API_V1 ||
        clock->struct_size < sizeof(*clock) || !clock->monotonic_ms) return false;
    host = api; clock_api = clock;
    status = "WAITING FOR XINPUT DISCOVERY"; return true;
}
static bool quiesce(void) {
    for (size_t i = 0; i < RISC_USB_INPUT_MAX_SUBSCRIBERS; ++i)
        if (subscribers[i].token) return false;
    for (size_t i = 0; i < PADS; ++i) if (!release_pad(&pads[i])) return false;
    return true;
}
static void stop(void) {
    if (!quiesce()) return;
    host = 0; clock_api = 0;
    for (size_t i = 0; i < RISC_USB_HOST_MAX_DEVICES; ++i) inspected[i] = (inspected_device){0};
    status = "XINPUT DRIVER NOT STARTED";
}
static const risc_usb_gamepad_diagnostics_v1 api = {
    {RISC_USB_GAMEPAD_API_V1, sizeof(risc_usb_gamepad_diagnostics_v1), 0,
     subscribe, unsubscribe, poll, next, snapshot}, diagnostic
};
static const risc_driver_v2 driver = {
    RISC_PROVIDER_DRIVER_ABI_V2, sizeof(risc_driver_v2),
    "usb-xinput-gamepad", "usb.xinput.gamepad", RISC_USB_GAMEPAD_API_V1,
    &api, start, stop, quiesce
};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &driver : 0;
}

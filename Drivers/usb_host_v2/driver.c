#include "RiscUsbInterruptV1.h"
#include "RiscUsbDiscoveryDiagnosticsV1.h"

/* The USB host is an independently installed ELF. The runtime sees usb.host
 * as an opaque capability; the physical controller owns transfers and VBUS.
 * All calls are serialized on the provider executor. */
typedef struct { uint64_t token, physical; bool present; } device_slot;
typedef struct {
    uint64_t token, physical_claim, device_token;
    uint8_t interface_number, alternate;
    uint16_t bulk_in, bulk_out, interrupt_in;
    bool closing;
} claim_slot;
static const risc_usb_controller_interrupt_v1 *controller;
static device_slot devices[RISC_USB_HOST_MAX_DEVICES];
static claim_slot claims[RISC_USB_HOST_MAX_CLAIMS];
static uint64_t sequence;
static uint8_t descriptor[RISC_USB_CONFIG_LIMIT];
static bool event_fault;

static bool equal(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static uint64_t next_token(void) {
    if (sequence == UINT64_MAX) { event_fault = true; return 0; }
    return ++sequence;
}
static device_slot *device_for(uint64_t token) {
    if (!controller || !token || event_fault) return 0;
    for (size_t i = 0; i < RISC_USB_HOST_MAX_DEVICES; ++i)
        if (devices[i].present && devices[i].token == token) return &devices[i];
    return 0;
}
static claim_slot *claim_for(uint64_t token) {
    if (!controller || !token || event_fault) return 0;
    for (size_t i = 0; i < RISC_USB_HOST_MAX_CLAIMS; ++i)
        if (claims[i].token == token && !claims[i].closing) return &claims[i];
    return 0;
}
static bool has_claim(uint64_t device_token) {
    for (size_t i = 0; i < RISC_USB_HOST_MAX_CLAIMS; ++i)
        if (claims[i].token && claims[i].device_token == device_token) return true;
    return false;
}
static bool start(const risc_provider_dependency_v1 *deps, size_t count) {
    if (controller || event_fault || !deps || count != 1 ||
        !equal(deps[0].capability_id, "usb.controller") ||
        deps[0].api_version != RISC_USB_CONTROLLER_API_V1 || !deps[0].api)
        return false;
    const risc_usb_controller_interrupt_v1 *api =
        (const risc_usb_controller_interrupt_v1 *)deps[0].api;
    const risc_usb_controller_api_v1 *base = &api->controller;
    if (base->api_version != RISC_USB_CONTROLLER_API_V1 ||
        base->struct_size < sizeof(*api) || !base->next_event ||
        !base->configuration || !base->claim || !base->release ||
        !base->control || !base->bulk_read || !base->bulk_write ||
        !base->quiesce || !api->interrupt_read) return false;
    controller = api;
    return true;
}
static bool quiesce(void) {
    if (!controller) return true;
    const risc_usb_controller_api_v1 *base = &controller->controller;
    for (size_t i = 0; i < RISC_USB_HOST_MAX_CLAIMS; ++i) {
        claim_slot *c = &claims[i];
        if (!c->token) continue;
        if (!c->closing || !base->release(base->context, c->physical_claim))
            return false; /* Keep failed claims and dependency mapped. */
        *c = (claim_slot){0};
    }
    return !event_fault && base->quiesce(base->context);
}
static void stop(void) {
    if (!quiesce()) return;
    for (size_t i = 0; i < RISC_USB_HOST_MAX_DEVICES; ++i)
        devices[i] = (device_slot){0};
    controller = 0;
    /* Never reuse device/claim tokens across provider restart. */
}
static bool poll_devices(void *ctx, size_t max_events, size_t *processed) {
    (void)ctx;
    if (!controller || event_fault || !processed || !max_events || max_events > 64)
        return false;
    const risc_usb_controller_api_v1 *base = &controller->controller;
    *processed = 0;
    for (; *processed < max_events; ++*processed) {
        risc_usb_controller_event_v1 event = {0};
        int32_t rc = base->next_event(base->context, &event);
        if (rc == 0) return true;
        if (rc != 1 || !event.physical_device ||
            (event.kind != 1 && event.kind != 2)) {
            event_fault = true; return false;
        }
        size_t found = RISC_USB_HOST_MAX_DEVICES;
        for (size_t i = 0; i < RISC_USB_HOST_MAX_DEVICES; ++i)
            if (devices[i].physical == event.physical_device &&
                (devices[i].present || has_claim(devices[i].token))) {
                found = i; break;
            }
        if (event.kind == 1) {
            if (found != RISC_USB_HOST_MAX_DEVICES) {
                event_fault = true; return false;
            }
            size_t empty = RISC_USB_HOST_MAX_DEVICES;
            for (size_t i = 0; i < RISC_USB_HOST_MAX_DEVICES; ++i)
                if (!devices[i].present && !has_claim(devices[i].token)) {
                    empty = i; break;
                }
            uint64_t token = next_token();
            if (empty == RISC_USB_HOST_MAX_DEVICES || !token) {
                event_fault = true; return false;
            }
            devices[empty] = (device_slot){token, event.physical_device, true};
        } else {
            if (found == RISC_USB_HOST_MAX_DEVICES || !devices[found].present) {
                event_fault = true; return false;
            }
            devices[found].present = false;
            /* Claims remain pinned until their class closes them. */
        }
    }
    return true;
}
static bool list_devices(void *ctx, uint64_t *out, size_t *count) {
    (void)ctx;
    if (!controller || event_fault || !count) return false;
    size_t actual = 0;
    for (size_t i = 0; i < RISC_USB_HOST_MAX_DEVICES; ++i)
        if (devices[i].present) ++actual;
    if (*count < actual || (actual && !out)) {
        *count = actual; return false;
    }
    size_t n = 0;
    for (size_t i = 0; i < RISC_USB_HOST_MAX_DEVICES; ++i)
        if (devices[i].present) out[n++] = devices[i].token;
    *count = n;
    return true;
}
static bool configuration(void *ctx, uint64_t token, uint8_t *bytes,
                          size_t *length, uint16_t *vid, uint16_t *pid) {
    (void)ctx;
    device_slot *d = device_for(token);
    if (!d || !bytes || !length || !vid || !pid ||
        *length < 9 || *length > RISC_USB_CONFIG_LIMIT) return false;
    size_t cap = *length;
    const risc_usb_controller_api_v1 *base = &controller->controller;
    if (!base->configuration(base->context, d->physical,
                             bytes, length, vid, pid) ||
        *length < 9 || *length > cap || bytes[0] < 9 || bytes[1] != 2)
        return false;
    size_t total = (size_t)bytes[2] | ((size_t)bytes[3] << 8);
    return total == *length;
}
static bool endpoints(uint64_t physical, uint8_t iface, uint8_t alt,
                      uint16_t *bulk_in, uint16_t *bulk_out,
                      uint16_t *interrupt_in) {
    const risc_usb_controller_api_v1 *base = &controller->controller;
    size_t length = sizeof(descriptor);
    uint16_t vid = 0, pid = 0;
    if (!base->configuration(base->context, physical, descriptor,
                             &length, &vid, &pid) ||
        length < 9 || length > sizeof(descriptor) ||
        descriptor[0] < 9 || descriptor[1] != 2 ||
        ((size_t)descriptor[2] | ((size_t)descriptor[3] << 8)) != length)
        return false;
    bool selected = false, found = false;
    *bulk_in = *bulk_out = *interrupt_in = 0;
    for (size_t pos = 0; pos < length;) {
        if (length - pos < 2) return false;
        uint8_t size = descriptor[pos], type = descriptor[pos + 1];
        if (size < 2 || size > length - pos) return false;
        if (type == 4) {
            /* Once the requested interface is complete, descriptors belonging
             * to another composite function cannot change its endpoint set.
             * Do not reject a valid claim because that unrelated function has
             * a vendor-shortened interface record. */
            if (selected) return found;
            if (size < 6) return false;
            selected = size >= 9 && descriptor[pos + 2] == iface &&
                       descriptor[pos + 3] == alt;
            if (selected) {
                if (found) return false;
                found = true;
            }
        } else if (type == 5 && selected) {
            if (size < 7) return false;
            uint8_t address = descriptor[pos + 2];
            uint16_t packet = (uint16_t)descriptor[pos + 4] |
                              ((uint16_t)descriptor[pos + 5] << 8);
            if (!packet || !(address & 15u) || (address & 0x70u)) return false;
            uint8_t transfer = descriptor[pos + 3] & 3u;
            uint16_t *mask = 0;
            if (transfer == 2 && packet <= 512)
                mask = (address & 0x80u) ? bulk_in : bulk_out;
            else if (transfer == 3 && packet <= 64 && (address & 0x80u))
                mask = interrupt_in;
            if (mask) {
                uint16_t bit = (uint16_t)(1u << (address & 15u));
                if (*mask & bit) return false;
                *mask |= bit;
            }
        }
        pos += size;
    }
    return found;
}
static bool claim_interface(void *ctx, uint64_t token, uint8_t iface,
                            uint8_t alt, uint64_t *out) {
    (void)ctx;
    device_slot *d = device_for(token);
    if (!d || !out) return false;
    for (size_t i = 0; i < RISC_USB_HOST_MAX_CLAIMS; ++i)
        if (claims[i].token && claims[i].device_token == token &&
            claims[i].interface_number == iface) return false;
    size_t empty = RISC_USB_HOST_MAX_CLAIMS;
    for (size_t i = 0; i < RISC_USB_HOST_MAX_CLAIMS; ++i)
        if (!claims[i].token) { empty = i; break; }
    uint16_t in = 0, out_mask = 0, intr = 0;
    if (empty == RISC_USB_HOST_MAX_CLAIMS ||
        !endpoints(d->physical, iface, alt, &in, &out_mask, &intr)) return false;
    const risc_usb_controller_api_v1 *base = &controller->controller;
    uint64_t physical = 0;
    if (!base->claim(base->context, d->physical, iface, alt, &physical) ||
        !physical) return false;
    uint64_t assigned = next_token();
    if (!assigned) {
        claims[empty] = (claim_slot){UINT64_MAX, physical, token,
                                      iface, alt, in, out_mask, intr, true};
        return false;
    }
    claims[empty] = (claim_slot){assigned, physical, token,
                                  iface, alt, in, out_mask, intr, false};
    *out = assigned;
    return true;
}
static void release_claim(void *ctx, uint64_t token) {
    (void)ctx;
    if (!controller || !token) return;
    const risc_usb_controller_api_v1 *base = &controller->controller;
    for (size_t i = 0; i < RISC_USB_HOST_MAX_CLAIMS; ++i) {
        claim_slot *c = &claims[i];
        if (c->token != token || c->closing) continue;
        c->closing = true;
        if (base->release(base->context, c->physical_claim)) *c = (claim_slot){0};
        return;
    }
}
static int32_t control(void *ctx, uint64_t token, uint8_t type,
                       uint8_t request, uint16_t value, uint16_t index,
                       uint8_t *payload, uint16_t length, uint32_t timeout) {
    (void)ctx;
    device_slot *d = device_for(token);
    if (!d || length > RISC_USB_CONFIG_LIMIT || (length && !payload) ||
        !timeout) return -1;
    if ((type & 0x1fu) == 1u) {
        bool authorized = false;
        for (size_t i = 0; i < RISC_USB_HOST_MAX_CLAIMS; ++i)
            if (claims[i].token && !claims[i].closing &&
                claims[i].device_token == token && index <= 255u &&
                claims[i].interface_number == (uint8_t)index) {
                authorized = true; break;
            }
        if (!authorized) return -1;
    }
    const risc_usb_controller_api_v1 *base = &controller->controller;
    int32_t n = base->control(base->context, d->physical, type, request,
                              value, index, payload, length, timeout);
    return n >= 0 && n <= length ? n : -1;
}
static int32_t bulk_read(void *ctx, uint64_t token, uint8_t endpoint,
                         uint8_t *dst, size_t capacity, uint32_t timeout) {
    (void)ctx;
    claim_slot *c = claim_for(token);
    if (!c || !device_for(c->device_token) || !dst || !capacity ||
        capacity > RISC_USB_CONFIG_LIMIT || !timeout || !(endpoint & 0x80u) ||
        (endpoint & 0x70u) ||
        !(c->bulk_in & (uint16_t)(1u << (endpoint & 15u)))) return -1;
    const risc_usb_controller_api_v1 *base = &controller->controller;
    int32_t n = base->bulk_read(base->context, c->physical_claim,
                                endpoint, dst, capacity, timeout);
    return n >= 0 && (size_t)n <= capacity ? n : -1;
}
static int32_t bulk_write(void *ctx, uint64_t token, uint8_t endpoint,
                          const uint8_t *src, size_t length, uint32_t timeout) {
    (void)ctx;
    claim_slot *c = claim_for(token);
    if (!c || !device_for(c->device_token) || !src || !length ||
        length > RISC_USB_CONFIG_LIMIT || !timeout || (endpoint & 0x80u) ||
        (endpoint & 0x70u) ||
        !(c->bulk_out & (uint16_t)(1u << (endpoint & 15u)))) return -1;
    const risc_usb_controller_api_v1 *base = &controller->controller;
    int32_t n = base->bulk_write(base->context, c->physical_claim,
                                 endpoint, src, length, timeout);
    return n >= 0 && (size_t)n <= length ? n : -1;
}
static int32_t interrupt_read(void *ctx, uint64_t token, uint8_t endpoint,
                              uint8_t *dst, size_t capacity, uint32_t timeout) {
    (void)ctx;
    claim_slot *c = claim_for(token);
    if (!c || !device_for(c->device_token) || !dst || !capacity ||
        capacity > 64 || !timeout || timeout > 100 ||
        !(endpoint & 0x80u) || (endpoint & 0x70u) ||
        !(c->interrupt_in & (uint16_t)(1u << (endpoint & 15u)))) return -1;
    int32_t n = controller->interrupt_read(controller->controller.context,
                                           c->physical_claim, endpoint,
                                           dst, capacity, timeout);
    return n >= 0 && (size_t)n <= capacity ? n : -1;
}
static bool diagnostic(void *ctx, char *out, size_t capacity) {
    (void)ctx;
    if (!controller || !out || !capacity ||
        controller->controller.struct_size < sizeof(risc_usb_controller_diagnostics_v1)) return false;
    const risc_usb_controller_diagnostics_v1 *extended =
        (const risc_usb_controller_diagnostics_v1 *)controller;
    return extended->diagnostic && extended->diagnostic(controller->controller.context, out, capacity);
}
static const risc_usb_host_diagnostics_v1 interface = {
    {{{RISC_USB_HOST_API_V1, sizeof(risc_usb_host_diagnostics_v1), 0,
      configuration, claim_interface, release_claim, control,
      bulk_read, bulk_write}, poll_devices, list_devices},
    interrupt_read}, diagnostic
};
static const risc_driver_v2 driver = {
    RISC_PROVIDER_DRIVER_ABI_V2, sizeof(risc_driver_v2),
    "usb-host-v2", "usb.host", RISC_USB_HOST_API_V1,
    &interface.base.discovery.host, start, stop, quiesce
};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &driver : 0;
}

#include "RiscUsbHidV1.h"
#include "RiscUsbInterruptV1.h"

/* All USB descriptor, claim, HID request and input-report behavior lives in
 * this ELF. No firmware HID implementation or firmware USB bridge is used.
 * The provider executor serializes calls, including dependency invocations. */
typedef struct {
    uint64_t token;
    risc_usb_hid_interface_v1 identity;
    uint64_t host_claim;
} session_slot;
static const risc_usb_host_interrupt_v1 *host;
static risc_usb_hid_interface_v1 interfaces[RISC_USB_HID_MAX_INTERFACES];
static session_slot sessions[RISC_USB_HID_MAX_SESSIONS];
static size_t interface_count;
static uint64_t serial;
static uint8_t config[RISC_USB_CONFIG_LIMIT];
static bool fault;

static bool equal(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static bool same(const risc_usb_hid_interface_v1 *a,
                 const risc_usb_hid_interface_v1 *b) {
    return a->device == b->device &&
           a->interface_number == b->interface_number &&
           a->alternate == b->alternate;
}
static const risc_usb_hid_interface_v1 *current(const risc_usb_hid_interface_v1 *key) {
    for (size_t i = 0; i < interface_count; ++i)
        if (same(key, &interfaces[i])) return &interfaces[i];
    return 0;
}
static session_slot *lookup(uint64_t token) {
    if (!host || !token || fault) return 0;
    for (size_t i = 0; i < RISC_USB_HID_MAX_SESSIONS; ++i)
        if (sessions[i].token == token) return &sessions[i];
    return 0;
}
static bool append(risc_usb_hid_interface_v1 *candidate, size_t *count,
                   const risc_usb_hid_interface_v1 *value) {
    if (!value->device || !value->interrupt_in || !value->max_packet ||
        !value->report_descriptor_length) return true; /* non-input interface */
    if (*count == RISC_USB_HID_MAX_INTERFACES) return false;
    for (size_t i = 0; i < *count; ++i)
        if (same(&candidate[i], value)) return false;
    candidate[(*count)++] = *value;
    return true;
}
/* Complete, length-checked configuration descriptor walk. The HID descriptor
 * belongs to its own interface/alternate, never to a neighboring composite
 * function. Unknown descriptor types are skipped after validating bLength. */
static bool parse(uint64_t device, uint16_t vid, uint16_t pid, size_t length,
                  risc_usb_hid_interface_v1 *candidate, size_t *count) {
    if (length < 9 || length > sizeof(config) || config[0] < 9 ||
        config[1] != 2 || ((size_t)config[2] | ((size_t)config[3] << 8)) != length)
        return false;
    risc_usb_hid_interface_v1 item = {0};
    bool selected = false, valid = true, hid_descriptor = false, endpoint = false;
    for (size_t pos = 0; pos < length;) {
        /* Broken framing has no trustworthy next boundary. Keep only already
         * completed interfaces; discard this scope and never byte-resynchronize. */
        if (length - pos < 2) return true;
        uint8_t n = config[pos], type = config[pos + 1];
        if (n < 2 || n > length - pos) return true;
        if (type == 4) {
            if (selected && valid && !append(candidate, count, &item)) return false;
            selected = false;
            valid = true;
            hid_descriptor = endpoint = false;
            item = (risc_usb_hid_interface_v1){0};
            if (n >= 9) selected = config[pos + 5] == 3;
            if (selected) {
                item.device = device;
                item.vid = vid; item.pid = pid;
                item.interface_number = config[pos + 2];
                item.alternate = config[pos + 3];
                item.subclass = config[pos + 6];
                item.protocol = config[pos + 7];
            }
        } else if (selected && valid && type == 0x21) {
            if (hid_descriptor || n < 9 || config[pos + 5] == 0 ||
                (size_t)n < 6u + 3u * config[pos + 5]) {
                valid = false;
            } else {
                hid_descriptor = true;
                for (size_t j = 0; j < config[pos + 5]; ++j) {
                    size_t off = pos + 6u + 3u * j;
                    if (config[off] != 0x22) continue;
                    if (item.report_descriptor_length) { valid = false; break; }
                    item.report_descriptor_length =
                        (uint16_t)config[off + 1] | ((uint16_t)config[off + 2] << 8);
                    if (!item.report_descriptor_length ||
                        item.report_descriptor_length > RISC_USB_HID_MAX_DESCRIPTOR) {
                        valid = false;
                        break;
                    }
                }
                if (!item.report_descriptor_length) valid = false;
            }
        } else if (selected && valid && type == 5) {
            if (n < 7) {
                valid = false;
            } else {
                uint8_t address = config[pos + 2];
                uint16_t packet = (uint16_t)config[pos + 4] |
                                  ((uint16_t)config[pos + 5] << 8);
                if ((config[pos + 3] & 3u) == 3u && (address & 0x80u)) {
                    if (endpoint || !(address & 15u) || (address & 0x70u) ||
                        !packet || packet > RISC_USB_HID_MAX_REPORT) {
                        valid = false;
                    } else {
                        endpoint = true;
                        item.interrupt_in = address;
                        item.interval = config[pos + 6];
                        item.max_packet = packet;
                    }
                }
            }
        }
        pos += n;
    }
    return !selected || !valid || append(candidate, count, &item);
}

static bool scan(void *context, size_t max_events) {
    (void)context;
    if (!host || fault || !max_events || max_events > 16) return false;
    size_t processed = 0;
    if (!host->discovery.poll(host->discovery.host.context, max_events, &processed) ||
        processed > max_events) return false;
    uint64_t devices[RISC_USB_HOST_MAX_DEVICES] = {0};
    size_t devices_count = RISC_USB_HOST_MAX_DEVICES;
    if (!host->discovery.devices(host->discovery.host.context,
                                 devices, &devices_count) ||
        devices_count > RISC_USB_HOST_MAX_DEVICES) return false;
    risc_usb_hid_interface_v1 next[RISC_USB_HID_MAX_INTERFACES] = {0};
    size_t count = 0;
    for (size_t i = 0; i < devices_count; ++i) {
        if (!devices[i]) return false;
        size_t length = sizeof(config);
        uint16_t vid = 0, pid = 0;
        if (!host->discovery.host.configuration(host->discovery.host.context,
                                                 devices[i], config, &length,
                                                 &vid, &pid) ||
            !parse(devices[i], vid, pid, length, next, &count))
            return false; /* Keep last coherent snapshot on transient failure. */
    }
    for (size_t i = 0; i < count; ++i) interfaces[i] = next[i];
    interface_count = count;
    return true;
}
static bool enumerate(void *context, risc_usb_hid_interface_v1 *out,
                      size_t *capacity) {
    (void)context;
    if (!host || fault || !capacity) return false;
    if (*capacity < interface_count || (interface_count && !out)) {
        *capacity = interface_count;
        return false;
    }
    for (size_t i = 0; i < interface_count; ++i) out[i] = interfaces[i];
    *capacity = interface_count;
    return true;
}
static uint64_t open_interface(void *context, uint64_t device,
                               uint8_t iface, uint8_t alternate) {
    (void)context;
    if (!host || fault || !device) return 0;
    risc_usb_hid_interface_v1 key = {0};
    key.device = device; key.interface_number = iface; key.alternate = alternate;
    const risc_usb_hid_interface_v1 *item = current(&key);
    if (!item) return 0;
    session_slot *free_slot = 0;
    for (size_t i = 0; i < RISC_USB_HID_MAX_SESSIONS; ++i) {
        if (sessions[i].token && same(&sessions[i].identity, item)) return 0;
        if (!sessions[i].token && !free_slot) free_slot = &sessions[i];
    }
    if (!free_slot || serial == UINT64_MAX) return 0;
    uint64_t claim = 0;
    if (!host->discovery.host.claim(host->discovery.host.context, device,
                                    iface, alternate, &claim) || !claim) return 0;
    *free_slot = (session_slot){++serial, *item, claim};
    return free_slot->token;
}
static bool report_descriptor(void *context, uint64_t token,
                              uint8_t *out, size_t *capacity) {
    (void)context;
    session_slot *session = lookup(token);
    if (!session || !current(&session->identity) || !capacity) return false;
    size_t required = session->identity.report_descriptor_length;
    if (*capacity < required || !out) { *capacity = required; return false; }
    int32_t n = host->discovery.host.control(host->discovery.host.context,
                   session->identity.device, 0x81, 0x06, 0x2200,
                   session->identity.interface_number, out, (uint16_t)required, 100);
    if (n != (int32_t)required) return false;
    *capacity = required;
    return true;
}
static bool set_boot(void *context, uint64_t token, bool boot) {
    (void)context;
    session_slot *session = lookup(token);
    if (!session || !current(&session->identity) ||
        session->identity.subclass != 1) return false;
    return host->discovery.host.control(host->discovery.host.context,
               session->identity.device, 0x21, 0x0b, boot ? 0 : 1,
               session->identity.interface_number, 0, 0, 100) == 0;
}
static int32_t read_report(void *context, uint64_t token, uint8_t *out,
                           size_t capacity, uint32_t timeout) {
    (void)context;
    session_slot *session = lookup(token);
    if (!session || !current(&session->identity) || !out ||
        capacity < session->identity.max_packet ||
        capacity > RISC_USB_HID_MAX_REPORT || !timeout || timeout > 100)
        return -1;
    return host->interrupt_read(host->discovery.host.context,
                 session->host_claim, session->identity.interrupt_in,
                 out, capacity, timeout);
}
static bool present(void *context, uint64_t token) {
    (void)context;
    session_slot *session = lookup(token);
    return session && current(&session->identity);
}
static bool close_interface(void *context, uint64_t token) {
    (void)context;
    session_slot *session = lookup(token);
    if (!session) return false;
    /* host owns claim failure quarantine; never reuse a HID session token. */
    host->discovery.host.release(host->discovery.host.context, session->host_claim);
    *session = (session_slot){0};
    return true;
}
static bool start(const risc_provider_dependency_v1 *deps, size_t count) {
    if (host || !deps || count != 1 || !equal(deps[0].capability_id, "usb.host") ||
        deps[0].api_version < RISC_USB_HOST_API_V1 || !deps[0].api) return false;
    const risc_usb_host_interrupt_v1 *api =
        (const risc_usb_host_interrupt_v1 *)deps[0].api;
    if (api->discovery.host.api_version != RISC_USB_HOST_API_V1 ||
        api->discovery.host.struct_size < sizeof(*api) ||
        !api->discovery.poll || !api->discovery.devices ||
        !api->discovery.host.configuration || !api->discovery.host.claim ||
        !api->discovery.host.release || !api->discovery.host.control ||
        !api->interrupt_read) return false;
    host = api;
    return true;
}
static bool quiesce(void) {
    for (size_t i = 0; i < RISC_USB_HID_MAX_SESSIONS; ++i)
        if (sessions[i].token) return false;
    return true;
}
static void stop(void) {
    if (!quiesce()) return;
    host = 0;
    interface_count = 0;
    fault = false;
}
static const risc_usb_hid_api_v1 api = {
    RISC_USB_HID_API_V1, sizeof(risc_usb_hid_api_v1), 0,
    scan, enumerate, open_interface, report_descriptor, set_boot,
    read_report, present, close_interface
};
static const risc_driver_v2 driver = {
    RISC_PROVIDER_DRIVER_ABI_V2, sizeof(risc_driver_v2),
    "usb-hid", "usb.hid", RISC_USB_HID_API_V1,
    &api, start, stop, quiesce
};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t version) {
    return version == RISC_PROVIDER_DRIVER_ABI_V2 ? &driver : 0;
}

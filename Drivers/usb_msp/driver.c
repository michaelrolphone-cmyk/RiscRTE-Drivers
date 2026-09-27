#include "RiscMspFetV1.h"
#include "RiscPlatformClockV1.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * TI MSP-FET / eZ-FET USB debug transport.
 *
 * The current application-mode probes enumerate as TI 2047:0014 (MSP-FET)
 * and 2047:0013 (eZ-FET lite). Recovery/bootloader identities 0204/0203 are
 * deliberately not accepted as debug probes.
 *
 * The probe's debug transport is CDC/VCP. This ELF selects the first CDC ACM
 * function on the known TI device (the debug channel), configures it for the
 * 460800 8N1 transport used by the MSP debug stack, implements the bounded
 * TI HAL packet transport, and owns JTAG/Spy-Bi-Wire start/stop. Target memory,
 * flash, register, breakpoint and device-family algorithms belong above
 * debug.vendor.msp.
 */

#define TI_VID 0x2047u
#define TI_EZFET_LITE_PID 0x0013u
#define TI_MSP_FET_PID 0x0014u

#define CDC_SET_LINE_CODING 0x20u
#define CDC_SET_CONTROL_LINE_STATE 0x22u
#define CDC_REQUEST_OUT 0x21u
#define MSP_FET_BAUD 460800u

#define HAL_TYPE_EXECUTE 0x81u
#define HAL_TYPE_ACK 0x91u
#define HAL_TYPE_EXCEPTION 0x92u
#define HAL_TYPE_DATA 0x93u

#define HAL_FID_VERSION 0x00u
#define HAL_FID_START_JTAG 0x04u
#define HAL_FID_STOP_JTAG 0x06u
#define HAL_FID_RESET_STATIC_GLOBAL_VARS 0x52u

#define DISCOVERY_ATTEMPTS 8u
#define DISCOVERY_DEADLINE_MS 10000u
#define IO_SLICE_MS 100u
#define IO_MAX_STEPS 64u
#define FRAME_MAX 512u

typedef struct {
    uint64_t device;
    uint16_t vid, pid;
    uint8_t control_iface, data_iface, data_alt, rx_ep, tx_ep, variant;
    uint8_t has_backchannel;
} probe_slot;

typedef struct {
    uint64_t token, device, control_claim, data_claim;
    uint8_t control_iface, data_iface, rx_ep, tx_ep;
    uint8_t ref_id, interface_mode;
    uint16_t protocol;
    uint8_t rx_buf[FRAME_MAX];
    size_t rx_len;
} session_slot;

typedef struct {
    uint64_t device, begun_ms, attempted_ms;
    uint8_t attempts;
    bool done, waiting_capacity;
} inspected_slot;

static const risc_usb_host_discovery_v1 *host;
static const risc_platform_clock_api_v1 *clock_api;
static probe_slot probes[RISC_MSP_FET_MAX_PROBES];
static session_slot sessions[RISC_MSP_FET_MAX_PROBES];
static inspected_slot inspected[RISC_USB_HOST_MAX_DEVICES];
static uint64_t serial;

static bool equal(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *b && *a == *b) { ++a; ++b; }
    return *a == *b;
}

static uint8_t variant_for(uint16_t vid, uint16_t pid) {
    if (vid != TI_VID) return RISC_MSP_FET_VARIANT_UNKNOWN;
    if (pid == TI_EZFET_LITE_PID) return RISC_MSP_FET_VARIANT_EZFET_LITE;
    if (pid == TI_MSP_FET_PID) return RISC_MSP_FET_VARIANT_MSP_FET;
    return RISC_MSP_FET_VARIANT_UNKNOWN;
}

static bool present(const uint64_t *devices, size_t count, uint64_t device) {
    for (size_t i = 0; i < count; ++i)
        if (devices[i] == device) return true;
    return false;
}

static probe_slot *probe_for(uint64_t device) {
    for (size_t i = 0; i < RISC_MSP_FET_MAX_PROBES; ++i)
        if (probes[i].device == device) return &probes[i];
    return 0;
}

static session_slot *session_for(uint64_t token) {
    if (!host || !token) return 0;
    for (size_t i = 0; i < RISC_MSP_FET_MAX_PROBES; ++i)
        if (sessions[i].token == token) return &sessions[i];
    return 0;
}

static bool parse_debug_cdc(const uint8_t *data, size_t length, probe_slot *out) {
    if (!data || !out || length < 9u || length > RISC_USB_CONFIG_LIMIT ||
        data[0] < 9u || data[1] != 2u ||
        ((size_t)data[2] | ((size_t)data[3] << 8)) != length)
        return false;

    uint8_t first_control = 0xffu;
    uint8_t union_data = 0xffu;
    uint8_t cdc_control_count = 0u;

    for (size_t at = 0; at < length;) {
        if (length - at < 2u) return false;
        const uint8_t n = data[at];
        const uint8_t type = data[at + 1u];
        if (n < 2u || n > length - at) return false;

        if (type == 4u) {
            if (n < 9u) return false;
            if (data[at + 3u] == 0u &&
                data[at + 5u] == 2u && data[at + 6u] == 2u) {
                if (cdc_control_count != UINT8_MAX) ++cdc_control_count;
                if (first_control == 0xffu) first_control = data[at + 2u];
            }
        } else if (type == 0x24u && n == 5u && data[at + 2u] == 6u &&
                   first_control != 0xffu && data[at + 3u] == first_control) {
            union_data = data[at + 4u];
        }
        at += n;
    }

    uint8_t iface = 0xffu, alt = 0xffu, cls = 0xffu;
    uint8_t selected_iface = 0xffu, selected_alt = 0u;
    uint8_t rx = 0u, tx = 0u, selected_rx = 0u, selected_tx = 0u;
    bool selected = false;

    for (size_t at = 0; at < length;) {
        const uint8_t n = data[at];
        const uint8_t type = data[at + 1u];

        if (type == 4u) {
            if (iface != 0xffu && cls == 10u && rx && tx) {
                const bool mapped = union_data != 0xffu ? iface == union_data :
                    (first_control == 0xffu || iface > first_control);
                if (mapped && !selected) {
                    selected = true;
                    selected_iface = iface;
                    selected_alt = alt;
                    selected_rx = rx;
                    selected_tx = tx;
                }
            }
            iface = data[at + 2u];
            alt = data[at + 3u];
            cls = data[at + 5u];
            rx = tx = 0u;
        } else if (type == 5u && iface != 0xffu && cls == 10u) {
            if (n < 7u) return false;
            const uint8_t ep = data[at + 2u];
            const uint8_t transfer = data[at + 3u] & 3u;
            const uint16_t mps = (uint16_t)data[at + 4u] |
                                 ((uint16_t)data[at + 5u] << 8);
            if (transfer == 2u) {
                if (!mps || mps > 512u || !(ep & 0x0fu) || (ep & 0x70u))
                    return false;
                if (ep & 0x80u) {
                    if (rx) return false;
                    rx = ep;
                } else {
                    if (tx) return false;
                    tx = ep;
                }
            }
        }
        at += n;
    }

    if (iface != 0xffu && cls == 10u && rx && tx && !selected) {
        const bool mapped = union_data != 0xffu ? iface == union_data :
            (first_control == 0xffu || iface > first_control);
        if (mapped) {
            selected = true;
            selected_iface = iface;
            selected_alt = alt;
            selected_rx = rx;
            selected_tx = tx;
        }
    }

    if (!selected) return false;
    out->control_iface = first_control == 0xffu ? selected_iface : first_control;
    out->data_iface = selected_iface;
    out->data_alt = selected_alt;
    out->rx_ep = selected_rx;
    out->tx_ep = selected_tx;
    out->has_backchannel = cdc_control_count > 1u ? 1u : 0u;
    return true;
}

static bool poll_probes(void *context, size_t max_events) {
    (void)context;
    if (!host || !clock_api || !max_events || max_events > 16u) return false;

    size_t processed = 0;
    if (!host->poll(host->host.context, max_events, &processed)) return false;

    uint64_t devices[RISC_USB_HOST_MAX_DEVICES] = {0};
    size_t count = RISC_USB_HOST_MAX_DEVICES;
    if (!host->devices(host->host.context, devices, &count) ||
        count > RISC_USB_HOST_MAX_DEVICES)
        return false;

    for (size_t i = 0; i < RISC_MSP_FET_MAX_PROBES; ++i) {
        if (probes[i].device && !present(devices, count, probes[i].device))
            probes[i] = (probe_slot){0};
        if (sessions[i].token && !present(devices, count, sessions[i].device)) {
            if (sessions[i].data_claim)
                host->host.release(host->host.context, sessions[i].data_claim);
            if (sessions[i].control_claim &&
                sessions[i].control_claim != sessions[i].data_claim)
                host->host.release(host->host.context, sessions[i].control_claim);
            sessions[i] = (session_slot){0};
        }
    }
    for (size_t i = 0; i < RISC_USB_HOST_MAX_DEVICES; ++i)
        if (inspected[i].device && !present(devices, count, inspected[i].device))
            inspected[i] = (inspected_slot){0};

    const uint64_t now = clock_api->monotonic_ms(clock_api->context);
    if (now == UINT64_MAX) return false;

    size_t work = 0;
    for (size_t i = 0; i < count && work < max_events; ++i) {
        if (probe_for(devices[i])) continue;

        inspected_slot *state = 0, *empty = 0;
        for (size_t j = 0; j < RISC_USB_HOST_MAX_DEVICES; ++j) {
            if (inspected[j].device == devices[i]) state = &inspected[j];
            if (!inspected[j].device && !empty) empty = &inspected[j];
        }
        if (!state) {
            if (!empty) continue;
            state = empty;
            *state = (inspected_slot){.device = devices[i], .begun_ms = now};
        }
        if (state->done) continue;

        probe_slot *slot = 0;
        for (size_t j = 0; j < RISC_MSP_FET_MAX_PROBES; ++j)
            if (!probes[j].device) { slot = &probes[j]; break; }
        if (state->waiting_capacity) {
            if (!slot) continue;
            state->waiting_capacity = false;
            state->attempts = 0u;
            state->begun_ms = now;
        }

        if (state->attempts >= DISCOVERY_ATTEMPTS ||
            (state->attempts && now - state->begun_ms >= DISCOVERY_DEADLINE_MS)) {
            state->done = true;
            continue;
        }
        if (state->attempts) {
            if (now < state->attempted_ms) {
                state->done = true;
                continue;
            }
            uint32_t delay_ms = 100u << (state->attempts - 1u);
            if (delay_ms > 2000u) delay_ms = 2000u;
            if (now - state->attempted_ms < delay_ms) continue;
        }

        ++work;
        state->attempted_ms = now;
        ++state->attempts;

        uint8_t config[RISC_USB_CONFIG_LIMIT];
        size_t length = sizeof(config);
        uint16_t vid = 0, pid = 0;
        if (!host->host.configuration(host->host.context, devices[i],
                                      config, &length, &vid, &pid))
            continue;

        const uint8_t variant = variant_for(vid, pid);
        if (!variant) {
            state->done = true;
            continue;
        }

        probe_slot candidate = {0};
        if (!parse_debug_cdc(config, length, &candidate)) {
            state->done = true;
            continue;
        }
        if (!slot) {
            state->waiting_capacity = true;
            continue;
        }

        candidate.device = devices[i];
        candidate.vid = vid;
        candidate.pid = pid;
        candidate.variant = variant;
        *slot = candidate;
        state->done = true;
    }
    return true;
}

static bool snapshot_probes(void *context, risc_msp_fet_probe_v1 *out,
                            size_t *capacity) {
    (void)context;
    if (!host || !capacity) return false;
    size_t count = 0;
    for (size_t i = 0; i < RISC_MSP_FET_MAX_PROBES; ++i)
        if (probes[i].device) ++count;
    if (*capacity < count || (count && !out)) {
        *capacity = count;
        return false;
    }

    size_t n = 0;
    for (size_t i = 0; i < RISC_MSP_FET_MAX_PROBES; ++i) {
        const probe_slot *p = &probes[i];
        if (!p->device) continue;
        out[n++] = (risc_msp_fet_probe_v1){
            p->device, p->vid, p->pid, p->control_iface, p->data_iface,
            p->rx_ep, p->tx_ep, p->variant, 1u, 1u, p->has_backchannel
        };
    }
    *capacity = count;
    return true;
}

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t parse_protocol(const uint8_t *version, size_t length) {
    if (!version || length < 4u) return 0u;
    if (length >= 40u) {
        const uint16_t sw = (uint16_t)le32(version);
        const uint8_t major = (uint8_t)((sw >> 14) + 1u);
        const uint8_t minor = (uint8_t)((sw >> 8) & 0x3fu);
        return (uint16_t)(((uint16_t)major << 8) | minor);
    }
    if (length < 8u) return 0u;
    return (uint16_t)(((uint16_t)(version[1] >> 6) << 8) |
                      (uint16_t)(version[1] & 0x3fu));
}

static uint8_t map_version(uint16_t protocol, uint8_t fid) {
    return protocol < 0x0300u && fid > 0x11u ? (uint8_t)(fid - 1u) : fid;
}

static uint32_t remaining_ms(uint64_t deadline) {
    const uint64_t now = clock_api->monotonic_ms(clock_api->context);
    if (now == UINT64_MAX || now >= deadline) return 0u;
    uint64_t remaining = deadline - now;
    if (remaining > IO_SLICE_MS) remaining = IO_SLICE_MS;
    return (uint32_t)remaining;
}

static bool write_all(session_slot *s, const uint8_t *src, size_t length,
                      uint32_t timeout_ms) {
    if (!s || !src || !length || !timeout_ms) return false;
    const uint64_t start = clock_api->monotonic_ms(clock_api->context);
    if (start == UINT64_MAX || UINT64_MAX - start < timeout_ms) return false;
    const uint64_t deadline = start + timeout_ms;

    size_t offset = 0;
    for (size_t step = 0; step < IO_MAX_STEPS && offset < length; ++step) {
        const uint32_t wait = remaining_ms(deadline);
        if (!wait) return false;
        const int32_t n = host->host.bulk_write(host->host.context, s->data_claim,
                                                s->tx_ep, src + offset,
                                                length - offset, wait);
        if (n <= 0 || (size_t)n > length - offset) return false;
        offset += (size_t)n;
    }
    return offset == length;
}

static bool send_frame(session_slot *s, uint8_t type, uint8_t ref,
                       const uint8_t *payload, size_t payload_length,
                       uint32_t timeout_ms) {
    if (!s || payload_length > 252u || (payload_length && !payload))
        return false;
    uint8_t frame[258] = {0};
    size_t length = 0;
    frame[length++] = (uint8_t)(payload_length + 3u);
    frame[length++] = type;
    frame[length++] = ref;
    frame[length++] = 0u;
    if (payload_length) {
        memcpy(frame + length, payload, payload_length);
        length += payload_length;
    }
    if (length & 1u) frame[length++] = 0u;
    return write_all(s, frame, length, timeout_ms);
}

static bool receive_frame(session_slot *s, uint8_t *type, uint8_t *ref,
                          uint8_t *payload, size_t *payload_length,
                          uint32_t timeout_ms) {
    if (!s || !type || !ref || !payload_length || !timeout_ms) return false;
    const uint64_t start = clock_api->monotonic_ms(clock_api->context);
    if (start == UINT64_MAX || UINT64_MAX - start < timeout_ms) return false;
    const uint64_t deadline = start + timeout_ms;

    for (size_t step = 0; step < IO_MAX_STEPS; ++step) {
        if (s->rx_len) {
            if (s->rx_buf[0] < 3u) return false;
            const size_t expected = (size_t)s->rx_buf[0] + 4u -
                                    ((size_t)s->rx_buf[0] & 1u);
            if (expected < 6u || expected > sizeof(s->rx_buf)) return false;
            if (s->rx_len >= expected) {
                uint8_t even = 0xffu, odd = 0xffu;
                for (size_t i = 0; i < expected; i += 2u) {
                    even ^= s->rx_buf[i];
                    odd ^= s->rx_buf[i + 1u];
                }
                if (even || odd) return false;

                const size_t body = (size_t)s->rx_buf[0] - 3u;
                if (body > *payload_length || (body && !payload)) return false;
                *type = s->rx_buf[1];
                *ref = s->rx_buf[2];
                if (body) memcpy(payload, s->rx_buf + 4u, body);
                *payload_length = body;

                if (s->rx_len > expected)
                    memmove(s->rx_buf, s->rx_buf + expected, s->rx_len - expected);
                s->rx_len -= expected;
                return true;
            }
        }

        if (s->rx_len >= sizeof(s->rx_buf)) return false;
        const uint32_t wait = remaining_ms(deadline);
        if (!wait) return false;
        const int32_t n = host->host.bulk_read(host->host.context, s->data_claim,
                                               s->rx_ep, s->rx_buf + s->rx_len,
                                               sizeof(s->rx_buf) - s->rx_len,
                                               wait);
        if (n < 0 || (size_t)n > sizeof(s->rx_buf) - s->rx_len) return false;
        if (n > 0) s->rx_len += (size_t)n;
    }
    return false;
}

static int32_t execute_internal(session_slot *s, uint8_t function_id,
                                const uint8_t *request, size_t request_length,
                                uint8_t *response, size_t response_capacity,
                                uint32_t timeout_ms) {
    if (!s || request_length > RISC_MSP_FET_MAX_REQUEST ||
        (request_length && !request) ||
        response_capacity > RISC_MSP_FET_MAX_RESPONSE ||
        (response_capacity && !response) ||
        !timeout_ms || timeout_ms > 5000u)
        return -1;

    const uint64_t started = clock_api->monotonic_ms(clock_api->context);
    if (started == UINT64_MAX || UINT64_MAX - started < timeout_ms) return -1;
    const uint64_t deadline = started + timeout_ms;

    uint8_t payload[RISC_MSP_FET_MAX_REQUEST + 2u] = {0};
    payload[0] = function_id;
    payload[1] = 0u;
    if (request_length) memcpy(payload + 2u, request, request_length);

    const uint8_t command_ref = s->ref_id;
    s->ref_id = (uint8_t)((s->ref_id + 1u) & 0x7fu);
    uint32_t remaining = remaining_ms(deadline);
    if (!remaining ||
        !send_frame(s, HAL_TYPE_EXECUTE, command_ref,
                    payload, request_length + 2u, remaining))
        return -1;

    size_t total = 0;
    for (size_t frame = 0; frame < 32u; ++frame) {
        uint8_t type = 0, ref = 0;
        uint8_t bytes[FRAME_MAX] = {0};
        size_t length = sizeof(bytes);
        remaining = remaining_ms(deadline);
        if (!remaining ||
            !receive_frame(s, &type, &ref, bytes, &length, remaining))
            return -1;

        if (type == HAL_TYPE_EXCEPTION) return -2;
        /* DATA continuation uses the high bit of ref; the low seven bits must
         * still identify the command we issued. Never accept a stale reply. */
        if ((ref & 0x7fu) != command_ref) return -1;
        if (type == HAL_TYPE_ACK) return (int32_t)total;
        if (type != HAL_TYPE_DATA) return -1;

        if (total + length > response_capacity) return -1;
        if (length) memcpy(response + total, bytes, length);
        total += length;

        remaining = remaining_ms(deadline);
        if (!remaining ||
            !send_frame(s, HAL_TYPE_ACK, command_ref, 0, 0, remaining))
            return -1;
    }
    return -1;
}

static int32_t execute_api(void *context, uint64_t token, uint8_t function_id,
                           const uint8_t *request, size_t request_length,
                           uint8_t *response, size_t response_capacity,
                           uint32_t timeout_ms) {
    (void)context;
    return execute_internal(session_for(token), function_id, request,
                            request_length, response, response_capacity,
                            timeout_ms);
}

static bool communication_reset(session_slot *s) {
    if (!s) return false;
    const uint8_t ref = s->ref_id;
    s->ref_id = (uint8_t)((s->ref_id + 1u) & 0x7fu);
    s->rx_len = 0;
    return send_frame(s, HAL_TYPE_EXCEPTION, ref, 0, 0, 1000u);
}

static bool stop_target(session_slot *s) {
    if (!s || !s->interface_mode) return true;
    uint8_t ignored[8] = {0};
    const int32_t rc = execute_internal(s, HAL_FID_STOP_JTAG, 0, 0,
                                        ignored, sizeof(ignored), 1000u);
    if (rc < 0) return false;
    s->interface_mode = RISC_MSP_FET_INTERFACE_NONE;
    return true;
}

static bool start_target(session_slot *s, uint8_t mode) {
    if (!s || (mode != RISC_MSP_FET_INTERFACE_JTAG &&
               mode != RISC_MSP_FET_INTERFACE_SBW))
        return false;

    const uint8_t protocol = mode == RISC_MSP_FET_INTERFACE_JTAG ? 0u : 1u;
    uint8_t response[8] = {0};
    const int32_t rc = execute_internal(s, HAL_FID_START_JTAG,
                                        &protocol, sizeof(protocol),
                                        response, sizeof(response), 1000u);
    if (rc < 1 || response[0] == 0u) return false;
    s->interface_mode = mode;
    return true;
}

static bool set_interface_api(void *context, uint64_t token, uint8_t mode) {
    (void)context;
    session_slot *s = session_for(token);
    if (!s || (mode != RISC_MSP_FET_INTERFACE_JTAG &&
               mode != RISC_MSP_FET_INTERFACE_SBW))
        return false;
    if (s->interface_mode == mode) return true;
    if (!stop_target(s)) return false;
    return start_target(s, mode);
}

static bool configure_debug_cdc(const probe_slot *probe) {
    uint8_t coding[7] = {
        (uint8_t)(MSP_FET_BAUD & 0xffu),
        (uint8_t)((MSP_FET_BAUD >> 8) & 0xffu),
        (uint8_t)((MSP_FET_BAUD >> 16) & 0xffu),
        (uint8_t)((MSP_FET_BAUD >> 24) & 0xffu),
        0u, 0u, 8u
    };
    if (host->host.control(host->host.context, probe->device, CDC_REQUEST_OUT,
                           CDC_SET_LINE_CODING, 0u, probe->control_iface,
                           coding, sizeof(coding), 1000u) != (int32_t)sizeof(coding))
        return false;
    return host->host.control(host->host.context, probe->device, CDC_REQUEST_OUT,
                              CDC_SET_CONTROL_LINE_STATE, 0u,
                              probe->control_iface, 0, 0, 1000u) == 0;
}

static uint64_t open_probe(void *context, uint64_t device, uint8_t mode) {
    (void)context;
    if (!host || !device || serial == UINT64_MAX ||
        (mode != RISC_MSP_FET_INTERFACE_JTAG &&
         mode != RISC_MSP_FET_INTERFACE_SBW))
        return 0;

    probe_slot *probe = probe_for(device);
    if (!probe) return 0;
    for (size_t i = 0; i < RISC_MSP_FET_MAX_PROBES; ++i)
        if (sessions[i].device == device) return 0;

    session_slot *slot = 0;
    for (size_t i = 0; i < RISC_MSP_FET_MAX_PROBES; ++i)
        if (!sessions[i].token) { slot = &sessions[i]; break; }
    if (!slot) return 0;

    uint64_t control_claim = 0, data_claim = 0;
    if (!host->host.claim(host->host.context, device, probe->control_iface,
                          0u, &control_claim) || !control_claim)
        return 0;
    if (probe->data_iface == probe->control_iface) {
        data_claim = control_claim;
    } else if (!host->host.claim(host->host.context, device, probe->data_iface,
                                 probe->data_alt, &data_claim) || !data_claim) {
        host->host.release(host->host.context, control_claim);
        return 0;
    }

    if (!configure_debug_cdc(probe)) {
        if (data_claim != control_claim)
            host->host.release(host->host.context, data_claim);
        host->host.release(host->host.context, control_claim);
        return 0;
    }

    for (size_t i = 0; i < 4u; ++i) {
        uint8_t discard[64];
        const int32_t n = host->host.bulk_read(host->host.context, data_claim,
                                               probe->rx_ep, discard,
                                               sizeof(discard), 10u);
        if (n <= 0) break;
    }

    uint64_t token = ++serial;
    if (!token) token = ++serial;
    *slot = (session_slot){
        token, device, control_claim, data_claim,
        probe->control_iface, probe->data_iface, probe->rx_ep, probe->tx_ep,
        0u, RISC_MSP_FET_INTERFACE_NONE, 0u, {0}, 0u
    };

    uint8_t version[64] = {0};
    const uint8_t version_request = 0u;
    const int32_t version_length = communication_reset(slot) ?
        execute_internal(slot, HAL_FID_VERSION, &version_request,
                         sizeof(version_request), version, sizeof(version),
                         1000u) : -1;
    slot->protocol = version_length > 0 ?
        parse_protocol(version, (size_t)version_length) : 0u;
    uint8_t ignored[8] = {0};
    if (!slot->protocol ||
        execute_internal(slot, map_version(slot->protocol, HAL_FID_RESET_STATIC_GLOBAL_VARS),
                         0, 0, ignored, sizeof(ignored), 1000u) < 0 ||
        !start_target(slot, mode)) {
        if (slot->data_claim != slot->control_claim)
            host->host.release(host->host.context, slot->data_claim);
        host->host.release(host->host.context, slot->control_claim);
        *slot = (session_slot){0};
        return 0;
    }
    return token;
}

static bool close_probe(void *context, uint64_t token) {
    (void)context;
    session_slot *s = session_for(token);
    if (!s) return false;
    if (!stop_target(s) || !communication_reset(s)) return false;

    if (s->data_claim != s->control_claim)
        host->host.release(host->host.context, s->data_claim);
    host->host.release(host->host.context, s->control_claim);
    *s = (session_slot){0};
    return true;
}

static bool start(const risc_provider_dependency_v1 *deps, size_t count) {
    if (host || clock_api || !deps || count != 2u) return false;

    const risc_usb_host_discovery_v1 *usb = 0;
    const risc_platform_clock_api_v1 *clock = 0;
    for (size_t i = 0; i < count; ++i) {
        if (equal(deps[i].capability_id, "usb.host") &&
            deps[i].api_version == RISC_USB_HOST_API_V1 && !usb)
            usb = (const risc_usb_host_discovery_v1 *)deps[i].api;
        else if (equal(deps[i].capability_id, "platform.clock") &&
                 deps[i].api_version == RISC_PLATFORM_CLOCK_API_V1 && !clock)
            clock = (const risc_platform_clock_api_v1 *)deps[i].api;
        else
            return false;
    }

    if (!usb || usb->host.api_version != RISC_USB_HOST_API_V1 ||
        usb->host.struct_size < sizeof(*usb) ||
        !usb->host.configuration || !usb->host.claim || !usb->host.release ||
        !usb->host.control || !usb->host.bulk_read || !usb->host.bulk_write ||
        !usb->poll || !usb->devices ||
        !clock || clock->api_version != RISC_PLATFORM_CLOCK_API_V1 ||
        clock->struct_size < sizeof(*clock) || !clock->monotonic_ms)
        return false;

    host = usb;
    clock_api = clock;
    return true;
}

static bool quiesce(void) {
    for (size_t i = 0; i < RISC_MSP_FET_MAX_PROBES; ++i)
        if (sessions[i].token) return false;
    return true;
}

static void stop(void) {
    if (!quiesce()) return;
    host = 0;
    clock_api = 0;
    memset(probes, 0, sizeof(probes));
    memset(inspected, 0, sizeof(inspected));
}

static const risc_msp_fet_api_v1 capability = {
    RISC_MSP_FET_API_V1, sizeof(risc_msp_fet_api_v1), 0,
    poll_probes, snapshot_probes, open_probe, set_interface_api,
    execute_api, close_probe
};

static const risc_driver_v2 driver = {
    RISC_PROVIDER_DRIVER_ABI_V2, sizeof(risc_driver_v2),
    "usb-msp", "debug.vendor.msp", RISC_MSP_FET_API_V1,
    &capability, start, stop, quiesce
};

__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &driver : 0;
}

#include "RiscStlinkV1.h"
#include "RiscPlatformClockV1.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * ST-LINK V2/V2.1/V3 USB probe provider.
 *
 * usb.host owns enumeration, physical interface claims and bulk transfers.
 * This ELF owns STMicroelectronics ST-LINK USB matching, endpoint selection,
 * command framing and exclusive SWD/SWIM mode sessions. STM32 and STM8 target
 * algorithms belong in providers above debug.vendor.stlink.
 *
 * ST-LINK/V1 (PID 0x3744) uses a SCSI transport and is deliberately rejected
 * by this raw-bulk provider. V2/V2.1/V3 use 16-byte command packets.
 */

#define ST_VID 0x0483u
#define STLINK_V2_PID 0x3748u
#define STLINK_V2_1_PID 0x374bu
#define STLINK_V3_USBLOADER_PID 0x374du
#define STLINK_V3E_PID 0x374eu
#define STLINK_V3S_PID 0x374fu
#define STLINK_V2_1_NO_MSD_PID 0x3752u
#define STLINK_V3_2VCP_PID 0x3753u
#define STLINK_V3E_NO_MSD_PID 0x3754u
#define STLINK_V3P_USBLOADER_PID 0x3755u
#define STLINK_V3P_PID 0x3757u

#define STLINK_GET_CURRENT_MODE 0xf5u
#define STLINK_DEBUG_COMMAND 0xf2u
#define STLINK_SWIM_COMMAND 0xf4u
#define STLINK_DEBUG_APIV1_ENTER 0x20u
#define STLINK_DEBUG_APIV2_ENTER 0x30u
#define STLINK_DEBUG_EXIT 0x21u
#define STLINK_DEBUG_ENTER_SWD_NO_RESET 0xa3u
#define STLINK_SWIM_ENTER 0x00u
#define STLINK_SWIM_EXIT 0x01u

#define STLINK_DEV_DFU_MODE 0x00u
#define STLINK_DEV_MASS_MODE 0x01u
#define STLINK_DEV_DEBUG_MODE 0x02u
#define STLINK_DEV_SWIM_MODE 0x03u
#define STLINK_DEV_BOOTLOADER_MODE 0x04u

#define STLINK_DEBUG_OK 0x80u
#define STLINK_SWIM_OK 0x00u
#define STLINK_TIMEOUT_MAX_MS 5000u
#define DISCOVERY_ATTEMPTS 8u
#define DISCOVERY_DEADLINE_MS 10000u

typedef struct {
    uint64_t device;
    uint16_t vid, pid, max_packet;
    uint8_t iface, alt, rx_ep, tx_ep, variant;
} probe_slot;

typedef struct {
    uint64_t token, device, claim;
    uint8_t transport, rx_ep, tx_ep;
} session_slot;

typedef struct {
    uint64_t device, begun_ms, attempted_ms;
    uint8_t attempts;
    bool done;
} inspected_slot;

static const risc_usb_host_discovery_v1 *host;
static const risc_platform_clock_api_v1 *clock_api;
static probe_slot probes[RISC_STLINK_MAX_PROBES];
static session_slot sessions[RISC_STLINK_MAX_PROBES];
static inspected_slot inspected[RISC_USB_HOST_MAX_DEVICES];
static uint64_t serial;

static bool equal(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *b && *a == *b) { ++a; ++b; }
    return *a == *b;
}

static uint8_t variant_for_pid(uint16_t vid, uint16_t pid) {
    if (vid != ST_VID) return RISC_STLINK_VARIANT_UNKNOWN;
    if (pid == STLINK_V2_PID) return RISC_STLINK_VARIANT_V2;
    if (pid == STLINK_V2_1_PID || pid == STLINK_V2_1_NO_MSD_PID)
        return RISC_STLINK_VARIANT_V2_1;
    switch (pid) {
        case STLINK_V3_USBLOADER_PID:
        case STLINK_V3E_PID:
        case STLINK_V3S_PID:
        case STLINK_V3_2VCP_PID:
        case STLINK_V3E_NO_MSD_PID:
        case STLINK_V3P_USBLOADER_PID:
        case STLINK_V3P_PID:
            return RISC_STLINK_VARIANT_V3;
        default:
            return RISC_STLINK_VARIANT_UNKNOWN;
    }
}

static bool present(const uint64_t *devices, size_t count, uint64_t device) {
    for (size_t i = 0; i < count; ++i)
        if (devices[i] == device) return true;
    return false;
}

static probe_slot *probe_for(uint64_t device) {
    for (size_t i = 0; i < RISC_STLINK_MAX_PROBES; ++i)
        if (probes[i].device == device) return &probes[i];
    return 0;
}

static session_slot *session_for(uint64_t token) {
    if (!host || !token) return 0;
    for (size_t i = 0; i < RISC_STLINK_MAX_PROBES; ++i)
        if (sessions[i].token == token) return &sessions[i];
    return 0;
}

static bool commit_interface(probe_slot *candidate, uint8_t iface, uint8_t alt,
                             uint8_t cls, uint8_t rx, uint8_t tx,
                             uint16_t rx_mps, uint16_t tx_mps,
                             size_t *count) {
    if (cls != 0xffu || iface == 0xffu || !rx || !tx) return true;
    /* ST-LINK command responses use bulk EP1 IN. Other vendor interfaces on
     * composite probes are not the debug transport and must be ignored. */
    if ((rx & 0x0fu) != 1u ||
        ((tx & 0x0fu) != 1u && (tx & 0x0fu) != 2u))
        return true;
    if (!rx_mps || !tx_mps || rx_mps > 512u || tx_mps > 512u) return false;
    ++*count;
    candidate->iface = iface;
    candidate->alt = alt;
    candidate->rx_ep = rx;
    candidate->tx_ep = tx;
    candidate->max_packet = rx_mps > tx_mps ? rx_mps : tx_mps;
    return true;
}

static bool parse_configuration(const uint8_t *data, size_t length,
                                probe_slot *out) {
    if (!data || !out || length < 9u || length > RISC_USB_CONFIG_LIMIT ||
        data[0] < 9u || data[1] != 2u ||
        ((size_t)data[2] | ((size_t)data[3] << 8)) != length)
        return false;

    uint8_t iface = 0xffu, alt = 0, cls = 0, rx = 0, tx = 0;
    uint16_t rx_mps = 0, tx_mps = 0;
    size_t candidates = 0;
    probe_slot found = {0};

    for (size_t at = 0; at < length;) {
        if (length - at < 2u) return false;
        const uint8_t size = data[at];
        const uint8_t kind = data[at + 1u];
        if (size < 2u || size > length - at) return false;

        if (kind == 4u) {
            if (!commit_interface(&found, iface, alt, cls, rx, tx,
                                  rx_mps, tx_mps, &candidates))
                return false;
            if (size < 9u) return false;
            iface = data[at + 2u];
            alt = data[at + 3u];
            cls = data[at + 5u];
            rx = tx = 0;
            rx_mps = tx_mps = 0;
        } else if (kind == 5u && cls == 0xffu && iface != 0xffu) {
            if (size < 7u) return false;
            const uint8_t ep = data[at + 2u];
            const uint8_t transfer = data[at + 3u] & 3u;
            const uint16_t mps = (uint16_t)data[at + 4u] |
                                 ((uint16_t)data[at + 5u] << 8);
            if (transfer == 2u) {
                if (!mps || mps > 512u || !(ep & 0x0fu) || (ep & 0x70u))
                    return false;
                if (ep & 0x80u) {
                    /* EP1 IN is the ST-LINK command/data response endpoint.
                     * V2.1/V3 may also expose EP2 IN for SWO trace; ignore it. */
                    if (!rx || (ep & 0x0fu) == 1u) {
                        rx = ep; rx_mps = mps;
                    }
                } else {
                    if (tx) return false;
                    tx = ep; tx_mps = mps;
                }
            }
        }
        at += size;
    }

    if (!commit_interface(&found, iface, alt, cls, rx, tx,
                          rx_mps, tx_mps, &candidates))
        return false;
    if (candidates != 1u) return false;
    *out = found;
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

    for (size_t i = 0; i < RISC_STLINK_MAX_PROBES; ++i)
        if (probes[i].device && !present(devices, count, probes[i].device))
            probes[i] = (probe_slot){0};
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

        state->done = true;
        const uint8_t variant = variant_for_pid(vid, pid);
        if (!variant) continue;

        probe_slot candidate = {0};
        if (!parse_configuration(config, length, &candidate)) continue;

        probe_slot *slot = 0;
        for (size_t j = 0; j < RISC_STLINK_MAX_PROBES; ++j)
            if (!probes[j].device) { slot = &probes[j]; break; }
        if (!slot) continue;

        candidate.device = devices[i];
        candidate.vid = vid;
        candidate.pid = pid;
        candidate.variant = variant;
        *slot = candidate;
    }
    return true;
}

static bool snapshot_probes(void *context, risc_stlink_probe_v1 *out,
                            size_t *capacity) {
    (void)context;
    if (!host || !capacity) return false;
    size_t count = 0;
    for (size_t i = 0; i < RISC_STLINK_MAX_PROBES; ++i)
        if (probes[i].device) ++count;
    if (*capacity < count || (count && !out)) {
        *capacity = count;
        return false;
    }
    size_t n = 0;
    for (size_t i = 0; i < RISC_STLINK_MAX_PROBES; ++i) {
        const probe_slot *p = &probes[i];
        if (!p->device) continue;
        out[n++] = (risc_stlink_probe_v1){
            p->device, p->vid, p->pid, p->iface, p->alt,
            p->rx_ep, p->tx_ep, p->max_packet, p->variant, 1u, 1u
        };
    }
    *capacity = count;
    return true;
}

static int32_t exchange(session_slot *s, const uint8_t *command,
                        size_t command_length, const uint8_t *tx,
                        size_t tx_length, uint8_t *rx, size_t rx_capacity,
                        uint32_t timeout_ms) {
    if (!s || !command || !command_length ||
        command_length > RISC_STLINK_COMMAND_BYTES ||
        tx_length > RISC_STLINK_MAX_TRANSFER ||
        rx_capacity > RISC_STLINK_MAX_TRANSFER ||
        (tx_length && rx_capacity) || !timeout_ms ||
        timeout_ms > STLINK_TIMEOUT_MAX_MS ||
        (tx_length && !tx) || (rx_capacity && !rx))
        return -1;

    uint8_t packet[RISC_STLINK_COMMAND_BYTES] = {0};
    memcpy(packet, command, command_length);
    int32_t rc = host->host.bulk_write(host->host.context, s->claim, s->tx_ep,
                                       packet, sizeof(packet), timeout_ms);
    if (rc != (int32_t)sizeof(packet)) return -1;

    if (tx_length) {
        rc = host->host.bulk_write(host->host.context, s->claim, s->tx_ep,
                                   tx, tx_length, timeout_ms);
        return rc >= 0 && (size_t)rc <= tx_length ? rc : -1;
    }
    if (rx_capacity) {
        rc = host->host.bulk_read(host->host.context, s->claim, s->rx_ep,
                                  rx, rx_capacity, timeout_ms);
        return rc >= 0 && (size_t)rc <= rx_capacity ? rc : -1;
    }
    return 0;
}

static int32_t command_api(void *context, uint64_t token,
                           const uint8_t *command, size_t command_length,
                           const uint8_t *tx, size_t tx_length,
                           uint8_t *rx, size_t rx_capacity,
                           uint32_t timeout_ms) {
    (void)context;
    return exchange(session_for(token), command, command_length, tx, tx_length,
                    rx, rx_capacity, timeout_ms);
}

static bool current_mode(session_slot *s, uint8_t *mode) {
    uint8_t command[1] = {STLINK_GET_CURRENT_MODE};
    uint8_t response[2] = {0};
    const int32_t n = exchange(s, command, sizeof(command), 0, 0,
                               response, sizeof(response), 1000u);
    if (n < 1) return false;
    *mode = response[0];
    return true;
}

static bool exit_mode(session_slot *s, uint8_t mode) {
    uint8_t command[2] = {0};

    if (mode == STLINK_DEV_DEBUG_MODE) {
        command[0] = STLINK_DEBUG_COMMAND;
        command[1] = STLINK_DEBUG_EXIT;
    } else if (mode == STLINK_DEV_SWIM_MODE) {
        command[0] = STLINK_SWIM_COMMAND;
        command[1] = STLINK_SWIM_EXIT;
    } else {
        return mode == STLINK_DEV_MASS_MODE;
    }

    /* ST-LINK mode-leave commands have no reply. */
    return exchange(s, command, sizeof(command), 0, 0, 0, 0, 1000u) == 0;
}

static bool set_transport_api(void *context, uint64_t token, uint8_t transport) {
    (void)context;
    session_slot *s = session_for(token);
    if (!s || (transport != RISC_STLINK_TRANSPORT_SWD &&
               transport != RISC_STLINK_TRANSPORT_SWIM))
        return false;

    uint8_t mode = 0xffu;
    if (!current_mode(s, &mode)) return false;
    if (mode == STLINK_DEV_DFU_MODE || mode == STLINK_DEV_BOOTLOADER_MODE)
        return false;

    if (transport == RISC_STLINK_TRANSPORT_SWIM &&
        mode == STLINK_DEV_SWIM_MODE) {
        s->transport = transport;
        return true;
    }

    if ((mode == STLINK_DEV_DEBUG_MODE || mode == STLINK_DEV_SWIM_MODE) &&
        !exit_mode(s, mode))
        return false;
    else if (mode != STLINK_DEV_DEBUG_MODE && mode != STLINK_DEV_SWIM_MODE &&
             mode != STLINK_DEV_MASS_MODE)
        return false;

    uint8_t response[2] = {0};
    if (transport == RISC_STLINK_TRANSPORT_SWD) {
        uint8_t command[3] = {
            STLINK_DEBUG_COMMAND, STLINK_DEBUG_APIV2_ENTER,
            STLINK_DEBUG_ENTER_SWD_NO_RESET
        };
        int32_t n = exchange(s, command, sizeof(command), 0, 0,
                             response, sizeof(response), 1000u);
        if (n < 1 || response[0] != STLINK_DEBUG_OK) {
            command[1] = STLINK_DEBUG_APIV1_ENTER;
            n = exchange(s, command, sizeof(command), 0, 0,
                         response, sizeof(response), 1000u);
            if (n < 1 || response[0] != STLINK_DEBUG_OK) return false;
        }
    } else {
        uint8_t command[2] = {STLINK_SWIM_COMMAND, STLINK_SWIM_ENTER};
        /* SWIM enter is also a no-reply command. */
        if (exchange(s, command, sizeof(command), 0, 0, 0, 0, 1000u) != 0)
            return false;
    }

    s->transport = transport;
    return true;
}

static uint64_t open_probe(void *context, uint64_t device, uint8_t transport) {
    (void)context;
    if (!host || !device || serial == UINT64_MAX ||
        (transport != RISC_STLINK_TRANSPORT_SWD &&
         transport != RISC_STLINK_TRANSPORT_SWIM))
        return 0;

    probe_slot *probe = probe_for(device);
    if (!probe) return 0;
    for (size_t i = 0; i < RISC_STLINK_MAX_PROBES; ++i)
        if (sessions[i].device == device) return 0;

    session_slot *slot = 0;
    for (size_t i = 0; i < RISC_STLINK_MAX_PROBES; ++i)
        if (!sessions[i].token) { slot = &sessions[i]; break; }
    if (!slot) return 0;

    uint64_t claim = 0;
    if (!host->host.claim(host->host.context, device, probe->iface,
                          probe->alt, &claim) || !claim)
        return 0;

    uint64_t token = ++serial;
    if (!token) token = ++serial;
    *slot = (session_slot){token, device, claim, RISC_STLINK_TRANSPORT_NONE,
                           probe->rx_ep, probe->tx_ep};

    if (!set_transport_api(0, token, transport)) {
        host->host.release(host->host.context, claim);
        *slot = (session_slot){0};
        return 0;
    }
    return token;
}

static bool close_probe(void *context, uint64_t token) {
    (void)context;
    session_slot *s = session_for(token);
    if (!s) return false;

    uint8_t mode = 0xffu;
    if (!current_mode(s, &mode)) return false;
    if ((mode == STLINK_DEV_DEBUG_MODE || mode == STLINK_DEV_SWIM_MODE) &&
        !exit_mode(s, mode))
        return false;

    host->host.release(host->host.context, s->claim);
    *s = (session_slot){0};
    return true;
}

static bool start(const risc_provider_dependency_v1 *deps, size_t count) {
    if (host || clock_api || !deps || count != 2) return false;

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
        !usb->host.bulk_read || !usb->host.bulk_write ||
        !usb->poll || !usb->devices ||
        !clock || clock->api_version != RISC_PLATFORM_CLOCK_API_V1 ||
        clock->struct_size < sizeof(*clock) || !clock->monotonic_ms)
        return false;

    host = usb;
    clock_api = clock;
    return true;
}

static bool quiesce(void) {
    for (size_t i = 0; i < RISC_STLINK_MAX_PROBES; ++i)
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

static const risc_stlink_api_v1 capability = {
    RISC_STLINK_API_V1, sizeof(risc_stlink_api_v1), 0,
    poll_probes, snapshot_probes, open_probe, set_transport_api,
    command_api, close_probe
};

static const risc_driver_v2 driver = {
    RISC_PROVIDER_DRIVER_ABI_V2, sizeof(risc_driver_v2),
    "usb-stlink", "debug.vendor.stlink", RISC_STLINK_API_V1,
    &capability, start, stop, quiesce
};

__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &driver : 0;
}

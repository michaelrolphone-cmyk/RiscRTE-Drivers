#pragma once
/* Provider-to-provider board power API, never part of the generic RiscRTE
 * core. Board ELF arbitrates charger, external input, source current and OTG.
 * A successful release means it has verified that it is no longer sourcing;
 * external VBUS may still be present. False retains the power lease and
 * prevents the controller or board-power ELF from unmapping. */
#include "RiscProviderV2.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RISC_USB_VBUS_API_V1 1u
typedef struct {
    uint32_t api_version;
    uint32_t struct_size;
    void *context;
    /* Only one live source lease. Reject unknown electrical/role state,
     * external VBUS, and requests above the board-verified current limit.
     * False MUST set *lease to zero; a partially applied power transition
     * must retain an internal lease and make provider quiesce return false
     * until a verified recovery has completed. */
    bool (*acquire_host)(void *context, uint32_t max_milliamps, uint64_t *lease);
    /* Source-off plus restored board state must be verified by hardware
     * readback. On failure retain ownership for explicit recovery/retry. */
    bool (*release_host)(void *context, uint64_t lease);
    /* True only when no source lease, pending rail transition, DMA/callback
     * into this provider, or lower-device claim can outlive the ELF. */
    bool (*quiesce)(void *context);
} risc_usb_vbus_api_v1;
/* Additive v1 extension. Consumers must check base.struct_size before using
 * it. Status is observed by the board's power provider, never by firmware/UI
 * code. This contract requires no particular chip, register bus or detector.
 * The provider owns electrical qualification and settling delays. SOURCE is
 * our own output, not evidence of a charger/computer. EXTERNAL has priority
 * when an independent detector can observe incoming power while sourcing. */
enum {
    RISC_USB_POWER_UNKNOWN = -1,
    RISC_USB_POWER_ABSENT = 0,
    RISC_USB_POWER_EXTERNAL = 1,
    RISC_USB_POWER_SOURCE = 2,
    /* Source-off has been verified, but input detection is still settling.
     * Do not start a host or count this as a failed read. This is not evidence
     * of absence. The consumer imposes a bounded overall observation budget. */
    RISC_USB_POWER_SETTLING = 3
};
/* Set only for boards unable to distinguish incoming power while sourcing.
 * Those boards need source-off observation while the host is empty. Other
 * providers can use an independent VBUS/role detector without power probes. */
#define RISC_USB_POWER_IDLE_PROBE_REQUIRED 1u
typedef struct {
    risc_usb_vbus_api_v1 base;
    int32_t (*input_status)(void *context);
    uint32_t flags;
} risc_usb_vbus_monitor_api_v1;
#ifdef __cplusplus
}
#endif

/* HID-capable ESP32-S3 controller. Original physical hardware ownership,
 * VBUS, DMA, callbacks, control, bulk and recovery remain in driver_base.cpp.
 * This file extends the SAME ELF rather than introducing a firmware bridge. */
#include "RiscUsbHidV1.h"
#include "RiscUsbInterruptV1.h"
#include "RiscUsbDiscoveryDiagnosticsV1.h"
#include <soc/usb_dwc_struct.h>
#include <soc/usb_wrap_struct.h>
#define t5_driver_get t5_usb_controller_base_get
#include "driver_base.cpp"
#undef t5_driver_get

namespace {
#include "PendingInterrupt.h"

bool interrupt_mps(Device *d, const Claim *c, uint8_t endpoint,
                   uint16_t *out_packet) {
    const usb_config_desc_t *config = nullptr;
    if (!d || !c || !d->attached || !out_packet ||
        !(endpoint & 0x80u) || (endpoint & 0x70u) ||
        usb_host_get_active_config_descriptor(d->handle, &config) != ESP_OK ||
        !config || config->wTotalLength < 9 ||
        config->wTotalLength > RISC_USB_CONFIG_LIMIT) return false;
    const auto *bytes = reinterpret_cast<const uint8_t *>(config);
    const size_t length = config->wTotalLength;
    bool selected = false, found = false;
    for (size_t pos = 0; pos < length;) {
        if (length - pos < 2) return false;
        const uint8_t n = bytes[pos], type = bytes[pos + 1];
        if (n < 2 || n > length - pos) return false;
        if (type == 4) {
            if (selected) return found;
            if (n < 6) return false;
            selected = n >= 9 && bytes[pos + 2] == c->number &&
                       bytes[pos + 3] == c->alternate;
        } else if (selected && type == 5) {
            if (n < 7) return false;
            if (bytes[pos + 2] == endpoint &&
                (bytes[pos + 3] & 3u) == 3u) {
                const uint16_t packet = static_cast<uint16_t>(bytes[pos + 4] |
                                                               (bytes[pos + 5] << 8));
                if (found || !packet || packet > RISC_USB_HID_MAX_REPORT)
                    return false;
                *out_packet = packet;
                found = true;
            }
        }
        pos += n;
    }
    return found;
}
int32_t interrupt_read(void *, uint64_t id, uint8_t endpoint,
                       uint8_t *dst, size_t capacity, uint32_t timeout) {
    Claim *c = claim(id);
    Device *d = c ? device(c->physical_device) : nullptr;
    uint16_t packet = 0;
    if (!running || !d || !d->attached || !dst || !timeout || timeout > 100 ||
        !interrupt_mps(d, c, endpoint, &packet) || capacity < packet ||
        capacity > RISC_USB_HID_MAX_REPORT) return -1;
    return read_interrupt(id, d->handle, endpoint, packet, dst, timeout);
}
bool release_with_interrupt(void *context, uint64_t id) {
    for (auto &slot : interrupts) {
        if (slot.claim_id == id && !drain_interrupt(slot)) return false;
    }
    return release_interface(context, id);
}
bool quiesce_with_interrupt(void *context) {
    for (auto &slot : interrupts) if (!drain_interrupt(slot)) return false;
    return quiesce(context);
}
bool drain_role_interrupts() {
    for (auto &slot : interrupts) if (!drain_interrupt(slot)) return false;
    return true;
}
bool start_with_role(const risc_provider_dependency_v1 *deps, size_t count) {
    drainRoleInterrupts = drain_role_interrupts;
    return start(deps, count);
}
void stop_with_interrupt() {
    if (quiesce_with_interrupt(nullptr)) stop();
}
bool enumeration_diagnostic(void *, char *out, size_t capacity) {
    if (role.state() != UsbRoleSwitch::State::Host) {
        StartupDiagnostic detail;
        detail.text(role.diagnostic());
        return detail.copy(out, capacity);
    }
    if (!installed) return false;
    const char *phyRoute = !RTCCNTL.usb_conf.sw_hw_usb_phy_sel ? "AUTO" :
        !RTCCNTL.usb_conf.sw_usb_phy_sel ? "JTAG" : USB_WRAP.otg_conf.phy_sel ? "EXT" :
        !USB_WRAP.otg_conf.pad_enable ? "OFF" : "OTG";
    return enumerationDiagnostic.copy(USB_DWC.hprt_reg.val, out, capacity,
        phyRoute, USB_WRAP.otg_conf.pad_pull_override && USB_WRAP.otg_conf.dp_pulldown,
        USB_WRAP.otg_conf.pad_pull_override && USB_WRAP.otg_conf.dm_pulldown);
}
static const risc_usb_controller_diagnostics_v1 hid_interface = {
    {{RISC_USB_CONTROLLER_API_V1, sizeof(risc_usb_controller_diagnostics_v1), nullptr,
     next_event, configuration, claim_interface, release_with_interrupt,
     control, bulk_read, bulk_write, quiesce_with_interrupt},
    interrupt_read}, enumeration_diagnostic
};
static const risc_driver_diagnostics_v2 hid_driver = {{
    RISC_PROVIDER_DRIVER_ABI_V2, sizeof(risc_driver_diagnostics_v2),
    "usb-controller-esp32s3", "usb.controller", RISC_USB_CONTROLLER_API_V1,
    &hid_interface.base.controller, start_with_role, stop_with_interrupt,
    []() -> bool { return quiesce_with_interrupt(nullptr); }
}, startup_error};
} // namespace
extern "C" void risc_usb_enum_reset(void) { enumerationDiagnostic.clear(); }
extern "C" void risc_usb_enum_stage(const char *stage) { enumerationDiagnostic.stage(stage); }
extern "C" void risc_usb_enum_error(const char *format, ...) {
    va_list args;
    va_start(args, format);
    enumerationDiagnostic.error(format, args);
    va_end(args);
}
extern "C" __attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &hid_driver.base : nullptr;
}

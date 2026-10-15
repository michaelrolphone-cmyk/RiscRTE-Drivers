#pragma once
#include <esp_idf_version.h>
#include <esp_private/usb_phy.h>

#if ESP_IDF_VERSION == ESP_IDF_VERSION_VAL(6, 1, 0)
#include <soc/soc_caps.h>
#include <hal/usb_wrap_ll.h>
#if !CONFIG_IDF_TARGET_ESP32S3 || SOC_USB_FSLS_PHY_NUM != 1
#error "Native USB receive gate is reviewed only for ESP32-S3 IDF 6.1"
#endif
// Private provider adapter. Caller owns the internal OTG PHY, and all calls
// are serialized with host lifecycle on the provider's executor. This is the
// exact S3 register sequence used by IDF 4.4.7 usb_phy_ll_int_enable_test_mode:
// OE high, RX D+/D- low => disconnected detector, without changing VBUS or pulls.
// Do not initialize/reset WRAP here: the owned PHY and host may already be live.
static inline esp_err_t risc_usb_phy_connection(usb_phy_handle_t handle, bool allow) {
    if (!handle) return ESP_ERR_INVALID_ARG;
    if (!allow) {
        USB_WRAP.test_conf.val = 0;
        const usb_wrap_test_mode_vals_t signals = {true, false, false, false, false, false};
        usb_wrap_ll_phy_test_mode_set_signals(&USB_WRAP, &signals);
    }
    usb_wrap_ll_phy_enable_test_mode(&USB_WRAP, !allow);
    return ESP_OK;
}
#elif ESP_IDF_VERSION == ESP_IDF_VERSION_VAL(4, 4, 7)
static inline esp_err_t risc_usb_phy_connection(usb_phy_handle_t handle, bool allow) {
    return usb_phy_action(handle, allow ? USB_PHY_ACTION_HOST_ALLOW_CONN
                                        : USB_PHY_ACTION_HOST_FORCE_DISCONN);
}
#else
#error "USB provider PHY adapter requires reviewed IDF 4.4.7 or 6.1"
#endif

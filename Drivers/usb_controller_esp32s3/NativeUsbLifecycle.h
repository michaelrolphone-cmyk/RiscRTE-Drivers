#pragma once
#include <stdbool.h>
#include <esp_err.h>
#ifdef __cplusplus
extern "C" {
#endif
// Private to the native provider. No API or SDK object crosses the provider ABI.
bool risc_usb_cleanup_pending(void);
esp_err_t risc_usb_retry_cleanup(void);
esp_err_t risc_usb_hcd_retry_cleanup(void);
esp_err_t risc_usb_hub_retry_cleanup(void);
#ifdef __cplusplus
}
#endif

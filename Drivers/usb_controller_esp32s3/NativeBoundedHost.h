#pragma once
#include <usb/usb_host.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Private exact-IDF helpers. One serialized provider executor only. */
esp_err_t risc_usb_interface_claim_poll(usb_host_client_handle_t, usb_device_handle_t, uint8_t, uint8_t);
esp_err_t risc_usb_interface_release_poll(usb_host_client_handle_t, usb_device_handle_t, uint8_t);
esp_err_t risc_usb_device_close_poll(usb_host_client_handle_t, usb_device_handle_t);
esp_err_t risc_usb_client_transfer_events(usb_host_client_handle_t, TickType_t);
#ifdef __cplusplus
}
#endif

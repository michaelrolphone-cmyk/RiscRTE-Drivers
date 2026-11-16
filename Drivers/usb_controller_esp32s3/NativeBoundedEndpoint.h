#pragma once
#include <stdint.h>
#include <esp_err.h>
#include <usb/usb_host.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Provider-private helpers for the pinned USB 1.6.0 closure. Calls never wait
 * for a channel IRQ or USBH mutex. NOT_FINISHED retains all DMA ownership.
 * Endpoint zero is supported only outside enumeration. The caller retains the
 * device, claim and transfer until its normal client callback has returned. */
esp_err_t risc_usb_endpoint_halt_poll(usb_device_handle_t device, uint8_t endpoint);
esp_err_t risc_usb_endpoint_flush(usb_device_handle_t device, uint8_t endpoint);
esp_err_t risc_usb_endpoint_clear(usb_device_handle_t device, uint8_t endpoint);
/* Retire already-completed EP0 URBs without running hub/enum daemon work.
 * This only queues the ordinary client callback; it does not replace that
 * callback as the ownership boundary. Pending daemon action flags stay intact. */
esp_err_t risc_usb_device_control_dequeue(usb_device_handle_t device);
#ifdef __cplusplus
}
#endif

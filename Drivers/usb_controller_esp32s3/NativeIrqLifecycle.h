#pragma once

#include <stdbool.h>
#include <esp_intr_alloc.h>

#ifdef __cplusplus
extern "C" {
#endif

// Private native-SDK boundary. The provider serializes lifecycle calls on its
// pinned executor task; these functions do not add another allocator/IRQ lock.
// Only a disabled, nonshared, non-IRAM USB IRQ may be allocated. Provider text
// is runtime-mapped and must not run while the flash cache is unavailable.
// Failed calls preserve ownership. Only a successful free releases the handle.
__attribute__((visibility("hidden")))
esp_err_t risc_usb_intr_alloc(int source, int flags, intr_handler_t handler,
                              void *arg, intr_handle_t *ret_handle);
__attribute__((visibility("hidden")))
esp_err_t risc_usb_intr_enable(intr_handle_t handle);
__attribute__((visibility("hidden")))
esp_err_t risc_usb_intr_disable(intr_handle_t handle);
__attribute__((visibility("hidden")))
esp_err_t risc_usb_intr_free(intr_handle_t handle);

// A live handle retains its HCD/ISR context even after a failed installation.
__attribute__((visibility("hidden"))) bool risc_usb_irq_live(void);
// True only on the live allocation's original pinned task and core.
__attribute__((visibility("hidden"))) bool risc_usb_irq_owner(void);

#ifdef __cplusplus
}
#endif

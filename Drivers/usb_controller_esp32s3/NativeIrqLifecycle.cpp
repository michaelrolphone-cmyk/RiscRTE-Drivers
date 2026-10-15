#include "NativeIrqLifecycle.h"
#include <sdkconfig.h>
#include <esp_idf_version.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <soc/interrupts.h>
#include <soc/soc_caps.h>

#if !CONFIG_IDF_TARGET_ESP32S3 || ESP_IDF_VERSION != ESP_IDF_VERSION_VAL(6, 1, 0)
#error "Native USB IRQ lifecycle requires reviewed ESP32-S3 IDF 6.1"
#endif
static_assert(SOC_USB_OTG_PERIPH_NUM == 1, "USB IRQ guard owns one S3 controller");

namespace {
struct IrqOwner {
    intr_handle_t handle;
    TaskHandle_t task;
    BaseType_t core;
};
IrqOwner irq = {nullptr, nullptr, -1};

// Checking the executing core alone is insufficient: an unpinned task may
// migrate before esp_intr_alloc/free reaches its native critical section.
bool pinned_task(TaskHandle_t task, BaseType_t core) {
    return task && core >= 0 && core < SOC_CPU_CORES_NUM &&
           xTaskGetCoreID(task) == core;
}

esp_err_t check_handle(intr_handle_t handle) {
    if (!handle) return ESP_ERR_INVALID_ARG;
    if (handle != irq.handle || !risc_usb_irq_owner()) return ESP_ERR_INVALID_STATE;
    return ESP_OK;
}
} // namespace

bool risc_usb_irq_live(void) {
    return irq.handle != nullptr;
}

bool risc_usb_irq_owner(void) {
    if (!irq.handle || xPortInIsrContext()) return false;
    const TaskHandle_t task = xTaskGetCurrentTaskHandle();
    return task == irq.task && xPortGetCoreID() == irq.core &&
           pinned_task(task, irq.core);
}

esp_err_t risc_usb_intr_alloc(int source, int flags, intr_handler_t handler,
                              void *arg, intr_handle_t *ret_handle) {
    if (source != ETS_USB_INTR_SOURCE || !handler || !ret_handle)
        return ESP_ERR_INVALID_ARG;
    if (!(flags & ESP_INTR_FLAG_INTRDISABLED) ||
        (flags & (ESP_INTR_FLAG_IRAM | ESP_INTR_FLAG_SHARED | ESP_INTR_FLAG_SHARED_PRIVATE)))
        return ESP_ERR_INVALID_ARG;
    if (irq.handle || xPortInIsrContext()) return ESP_ERR_INVALID_STATE;
    const TaskHandle_t task = xTaskGetCurrentTaskHandle();
    const BaseType_t core = xPortGetCoreID();
    if (!pinned_task(task, core)) return ESP_ERR_INVALID_STATE;

    intr_handle_t handle = nullptr;
    const esp_err_t result = esp_intr_alloc(source, flags, handler, arg, &handle);
    if (result != ESP_OK) return result;
    // The pinned SDK allocates on the executing core and supplies a non-null
    // handle on success. Preserve the caller's output on allocation failure.
    irq = {handle, task, core};
    *ret_handle = handle;
    return ESP_OK;
}

esp_err_t risc_usb_intr_enable(intr_handle_t handle) {
    const esp_err_t checked = check_handle(handle);
    if (checked != ESP_OK) return checked;
    return esp_intr_enable(handle);
}

esp_err_t risc_usb_intr_disable(intr_handle_t handle) {
    const esp_err_t checked = check_handle(handle);
    if (checked != ESP_OK) return checked;
    return esp_intr_disable(handle);
}

esp_err_t risc_usb_intr_free(intr_handle_t handle) {
    const esp_err_t checked = check_handle(handle);
    if (checked != ESP_OK) return checked;
    const esp_err_t result = esp_intr_free(handle);
    if (result == ESP_OK) irq = {nullptr, nullptr, -1};
    return result;
}

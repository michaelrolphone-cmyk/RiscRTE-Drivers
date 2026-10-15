#include "../../Drivers/usb_controller_esp32s3/NativeIrqLifecycle.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <soc/interrupts.h>
#include <cassert>
#include <cstdio>
#include <type_traits>

static_assert(std::is_same<decltype(&risc_usb_intr_alloc), decltype(&esp_intr_alloc)>::value,
              "allocation must preserve the SDK signature");
static_assert(std::is_same<decltype(&risc_usb_intr_enable), decltype(&esp_intr_enable)>::value,
              "enable must preserve the SDK signature");
static_assert(std::is_same<decltype(&risc_usb_intr_disable), decltype(&esp_intr_disable)>::value,
              "disable must preserve the SDK signature");
static_assert(std::is_same<decltype(&risc_usb_intr_free), decltype(&esp_intr_free)>::value,
              "free must preserve the SDK signature");

struct tskTaskControlBlock { BaseType_t affinity; };
struct intr_handle_data_t { bool live, enabled; BaseType_t core; };
static tskTaskControlBlock executor = {0}, other_task = {0};
static TaskHandle_t current_task = &executor;
static BaseType_t current_core;
static bool in_isr;
static intr_handle_data_t native_handle, stranger;
static esp_err_t alloc_result, enable_result, disable_result, free_result;
static unsigned alloc_calls, enable_calls, disable_calls, free_calls;
static int callback_arg;
static const int disabled_flags = ESP_INTR_FLAG_LOWMED | ESP_INTR_FLAG_INTRDISABLED;
static int expected_flags = disabled_flags;

extern "C" BaseType_t xPortInIsrContext(void) { return in_isr; }
extern "C" BaseType_t xPortGetCoreID(void) { return current_core; }
extern "C" TaskHandle_t xTaskGetCurrentTaskHandle(void) {
    assert(!in_isr);
    return current_task;
}
extern "C" BaseType_t xTaskGetCoreID(TaskHandle_t task) {
    assert(!in_isr && task);
    return task->affinity;
}
static void interrupt_handler(void *) {}

extern "C" esp_err_t esp_intr_alloc(int source, int flags, intr_handler_t handler,
                                     void *arg, intr_handle_t *ret_handle) {
    ++alloc_calls;
    assert(!in_isr && current_task && current_task->affinity == current_core);
    assert(source == ETS_USB_INTR_SOURCE && flags == expected_flags);
    assert(handler == interrupt_handler && arg == &callback_arg && ret_handle);
    assert(!native_handle.live);
    if (alloc_result != ESP_OK) return alloc_result;
    native_handle = {true, !(flags & ESP_INTR_FLAG_INTRDISABLED), current_core};
    *ret_handle = &native_handle;
    return ESP_OK;
}

static void check_native_owner(intr_handle_t handle) {
    assert(!in_isr && current_task == &executor && executor.affinity == current_core);
    assert(handle == &native_handle && handle->live && handle->core == current_core);
}
extern "C" esp_err_t esp_intr_enable(intr_handle_t handle) {
    ++enable_calls;
    check_native_owner(handle);
    if (enable_result == ESP_OK) handle->enabled = true;
    return enable_result;
}
extern "C" esp_err_t esp_intr_disable(intr_handle_t handle) {
    ++disable_calls;
    check_native_owner(handle);
    if (disable_result == ESP_OK) handle->enabled = false;
    return disable_result;
}
extern "C" esp_err_t esp_intr_free(intr_handle_t handle) {
    ++free_calls;
    check_native_owner(handle);
    if (free_result == ESP_OK) handle->live = handle->enabled = false;
    return free_result;
}

static unsigned native_calls() {
    return alloc_calls + enable_calls + disable_calls + free_calls;
}
static esp_err_t allocate(intr_handle_t *out) {
    return risc_usb_intr_alloc(ETS_USB_INTR_SOURCE, expected_flags,
                              interrupt_handler, &callback_arg, out);
}
static void reject_lifecycle(intr_handle_t handle, esp_err_t result) {
    const auto before = native_calls();
    assert(risc_usb_intr_enable(handle) == result);
    assert(risc_usb_intr_disable(handle) == result);
    assert(risc_usb_intr_free(handle) == result);
    assert(native_calls() == before);
}
static void reject_allocation() {
    const auto before = native_calls();
    intr_handle_t out = &stranger;
    assert(allocate(&out) == ESP_ERR_INVALID_STATE && out == &stranger);
    assert(native_calls() == before && !risc_usb_irq_live() && !risc_usb_irq_owner());
}

static void allocation_validation() {
    assert(!risc_usb_irq_live() && !risc_usb_irq_owner());
    reject_lifecycle(nullptr, ESP_ERR_INVALID_ARG);
    reject_lifecycle(&stranger, ESP_ERR_INVALID_STATE);
    intr_handle_t out = &stranger;
    assert(risc_usb_intr_alloc(ETS_USB_INTR_SOURCE + 1, disabled_flags,
                              interrupt_handler, &callback_arg, &out) == ESP_ERR_INVALID_ARG);
    assert(risc_usb_intr_alloc(ETS_USB_INTR_SOURCE, disabled_flags,
                              nullptr, &callback_arg, &out) == ESP_ERR_INVALID_ARG);
    assert(allocate(nullptr) == ESP_ERR_INVALID_ARG);
    const int bad_flags[] = {ESP_INTR_FLAG_LOWMED,
                            disabled_flags | ESP_INTR_FLAG_IRAM,
                            disabled_flags | ESP_INTR_FLAG_SHARED,
                            disabled_flags | ESP_INTR_FLAG_SHARED_PRIVATE};
    for (int flags : bad_flags) {
        assert(risc_usb_intr_alloc(ETS_USB_INTR_SOURCE, flags,
                                  interrupt_handler, &callback_arg, &out) == ESP_ERR_INVALID_ARG);
        assert(out == &stranger && !native_calls());
    }
    assert(out == &stranger && !native_calls());

    executor.affinity = tskNO_AFFINITY;
    reject_allocation();
    executor.affinity = 1; // Pinned to a different core is equally invalid.
    reject_allocation();
    current_core = executor.affinity = 2;
    reject_allocation();
    current_core = executor.affinity = 0;
    current_task = nullptr;
    reject_allocation();
    current_task = &executor;
    in_isr = true;
    reject_allocation();
    in_isr = false;

    alloc_result = ESP_ERR_NO_MEM;
    assert(allocate(&out) == ESP_ERR_NO_MEM && out == &stranger);
    assert(alloc_calls == 1 && !native_handle.live && !risc_usb_irq_live());
    alloc_result = ESP_OK;
}

static void lifecycle_failures_and_retry(BaseType_t core) {
    current_core = executor.affinity = other_task.affinity = core;
    intr_handle_t handle = nullptr;
    assert(allocate(&handle) == ESP_OK && handle == &native_handle);
    assert(risc_usb_irq_live() && risc_usb_irq_owner() && !native_handle.enabled);

    const auto allocated = alloc_calls;
    intr_handle_t duplicate = &stranger;
    assert(allocate(&duplicate) == ESP_ERR_INVALID_STATE && duplicate == &stranger);
    assert(alloc_calls == allocated && risc_usb_irq_live());
    reject_lifecycle(&stranger, ESP_ERR_INVALID_STATE);
    reject_lifecycle(nullptr, ESP_ERR_INVALID_ARG);

    current_core = executor.affinity = 1 - core;
    assert(risc_usb_irq_live() && !risc_usb_irq_owner());
    reject_lifecycle(handle, ESP_ERR_INVALID_STATE);
    current_core = executor.affinity = core;

    current_task = &other_task; // Matching core alone does not confer ownership.
    assert(risc_usb_irq_live() && !risc_usb_irq_owner());
    reject_lifecycle(handle, ESP_ERR_INVALID_STATE);
    current_task = &executor;

    executor.affinity = tskNO_AFFINITY; // Reject migration risk on the right core.
    assert(risc_usb_irq_live() && !risc_usb_irq_owner());
    reject_lifecycle(handle, ESP_ERR_INVALID_STATE);
    executor.affinity = 1 - core;
    reject_lifecycle(handle, ESP_ERR_INVALID_STATE);
    executor.affinity = core;

    in_isr = true;
    assert(risc_usb_irq_live() && !risc_usb_irq_owner());
    reject_lifecycle(handle, ESP_ERR_INVALID_STATE);
    in_isr = false;

    enable_result = ESP_ERR_TIMEOUT;
    auto before = enable_calls;
    assert(risc_usb_intr_enable(handle) == ESP_ERR_TIMEOUT && enable_calls == before + 1);
    assert(risc_usb_irq_live() && risc_usb_irq_owner() && !native_handle.enabled);
    enable_result = ESP_OK;
    assert(risc_usb_intr_enable(handle) == ESP_OK && native_handle.enabled);

    // Even a repeated enable must report the actual native error, not cached success.
    enable_result = ESP_FAIL;
    before = enable_calls;
    assert(risc_usb_intr_enable(handle) == ESP_FAIL && enable_calls == before + 1);
    assert(risc_usb_irq_live() && native_handle.enabled);
    enable_result = ESP_OK;

    disable_result = ESP_FAIL;
    before = disable_calls;
    assert(risc_usb_intr_disable(handle) == ESP_FAIL && disable_calls == before + 1);
    assert(risc_usb_irq_live() && risc_usb_irq_owner() && native_handle.enabled);
    disable_result = ESP_OK;
    assert(risc_usb_intr_disable(handle) == ESP_OK && !native_handle.enabled);
    disable_result = ESP_ERR_TIMEOUT;
    before = disable_calls;
    assert(risc_usb_intr_disable(handle) == ESP_ERR_TIMEOUT && disable_calls == before + 1);
    assert(risc_usb_irq_live() && !native_handle.enabled);
    disable_result = ESP_OK;

    free_result = ESP_ERR_NOT_FOUND;
    before = free_calls;
    assert(risc_usb_intr_free(handle) == ESP_ERR_NOT_FOUND && free_calls == before + 1);
    assert(risc_usb_irq_live() && risc_usb_irq_owner() && native_handle.live);
    assert(allocate(&duplicate) == ESP_ERR_INVALID_STATE && duplicate == &stranger);
    executor.affinity = tskNO_AFFINITY;
    reject_lifecycle(handle, ESP_ERR_INVALID_STATE);
    executor.affinity = core;
    free_result = ESP_OK;
    assert(risc_usb_intr_free(handle) == ESP_OK);
    assert(!risc_usb_irq_live() && !risc_usb_irq_owner() && !native_handle.live);
    reject_lifecycle(handle, ESP_ERR_INVALID_STATE); // Includes double-free rejection.
}

int main() {
    allocation_validation();
    lifecycle_failures_and_retry(0);
    lifecycle_failures_and_retry(1); // Successful free permits a new pinned owner core.

    intr_handle_t handle = nullptr;
    assert(allocate(&handle) == ESP_OK && !native_handle.enabled);
    assert(risc_usb_intr_enable(handle) == ESP_OK && native_handle.enabled);
    free_result = ESP_FAIL;
    assert(risc_usb_intr_free(handle) == ESP_FAIL && risc_usb_irq_live());
    assert(native_handle.live && native_handle.enabled);
    free_result = ESP_OK;
    assert(risc_usb_intr_free(handle) == ESP_OK && !risc_usb_irq_live());
    std::puts("native USB IRQ lifecycle: PASS (both cores, ownership, failures, retries, double-free)");
}

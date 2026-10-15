// Compile with the native provider's recorded target arguments. No link needed.
#include <type_traits>
#include <cstdint>
#include <cstddef>
#include <esp_log.h>
#include <esp_rom_sys.h>
#include <esp_private/periph_ctrl.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_idf_version.h>

static_assert(ESP_IDF_VERSION == ESP_IDF_VERSION_VAL(6, 1, 0));
static_assert(sizeof(esp_log_config_t) == 4);
static_assert(sizeof(BaseType_t) == 4 && sizeof(UBaseType_t) == 4);
static_assert(sizeof(TaskHandle_t) == 4 && sizeof(eNotifyAction) == 4);
static_assert(sizeof(QueueHandle_t) == 4 && sizeof(TickType_t) == 4);
static_assert(sizeof(StackType_t) == 1 && portBYTE_ALIGNMENT == 16);
static_assert(sizeof(portMUX_TYPE) == 8 && alignof(portMUX_TYPE) == 4);
static_assert(offsetof(portMUX_TYPE, owner) == 0 && offsetof(portMUX_TYPE, count) == 4);
static_assert(portMUX_FREE_VAL == 0xB33FFFFF && portMAX_DELAY == 0xffffffffU);
static_assert(alignof(esp_log_config_t) == 4 && offsetof(esp_log_config_t, data) == 0);
static_assert(configNUMBER_OF_CORES == 2 && configUSE_16_BIT_TICKS == 0);
static_assert(configTICK_RATE_HZ == 1000 && configTASK_NOTIFICATION_ARRAY_ENTRIES == 1);
static_assert(configUSE_TASK_NOTIFICATIONS == 1 && configUSE_MUTEXES == 1);
static_assert(configSUPPORT_DYNAMIC_ALLOCATION == 1);
#if CONFIG_FREERTOS_UNICORE || CONFIG_ESP_SYSTEM_SINGLE_CORE_MODE || CONFIG_FREERTOS_SMP
#error "Unreviewed native USB scheduler cohort"
#endif
static_assert(CONFIG_LOG_VERSION == 1 && CONFIG_LOG_DEFAULT_LEVEL == 3);
static_assert(CONFIG_LOG_MAXIMUM_LEVEL == 3 && CONFIG_LOG_TIMESTAMP_SOURCE_RTOS == 1);
static_assert(std::is_same<decltype(&esp_log),
              void (*)(esp_log_config_t, const char *, const char *, ...)>::value);
static_assert(std::is_same<decltype(&esp_rom_delay_us), void (*)(uint32_t)>::value);
static_assert(std::is_same<decltype(&periph_rcc_enter), void (*)(void)>::value);
static_assert(std::is_same<decltype(&periph_rcc_exit), void (*)(void)>::value);
static_assert(std::is_same<decltype(&xTaskGenericNotify),
              BaseType_t (*)(TaskHandle_t, UBaseType_t, uint32_t,
                             eNotifyAction, uint32_t *)>::value);
static_assert(std::is_same<decltype(&xTaskGetCoreID),
              BaseType_t (*)(TaskHandle_t)>::value);

#pragma once
/* Host fixture only: SDK 6.1 public boundary types and error values. */
#ifdef __cplusplus
extern "C" {
#endif
typedef int esp_err_t;
typedef struct intr_handle_data_t *intr_handle_t;
typedef void (*intr_handler_t)(void *);
enum {
    ESP_OK = 0,
    ESP_FAIL = -1,
    ESP_ERR_NO_MEM = 0x101,
    ESP_ERR_INVALID_ARG = 0x102,
    ESP_ERR_INVALID_STATE = 0x103,
    ESP_ERR_NOT_FOUND = 0x105,
    ESP_ERR_TIMEOUT = 0x107,
    ESP_INTR_FLAG_LOWMED = 14,
    ESP_INTR_FLAG_SHARED = 1 << 8,
    ESP_INTR_FLAG_IRAM = 1 << 10,
    ESP_INTR_FLAG_INTRDISABLED = 1 << 11,
    ESP_INTR_FLAG_SHARED_PRIVATE = 1 << 12
};
esp_err_t esp_intr_alloc(int source, int flags, intr_handler_t handler,
                         void *arg, intr_handle_t *ret_handle);
esp_err_t esp_intr_enable(intr_handle_t handle);
esp_err_t esp_intr_disable(intr_handle_t handle);
esp_err_t esp_intr_free(intr_handle_t handle);
#ifdef __cplusplus
}
#endif

#pragma once
#include <stddef.h>
#include <stdint.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_idf_version.h>
#include <soc/soc_caps.h>
#include <hal/cache_ll.h>
#if !CONFIG_IDF_TARGET_ESP32S3 || ESP_IDF_VERSION != ESP_IDF_VERSION_VAL(6, 1, 0) || SOC_CACHE_INTERNAL_MEM_VIA_L1CACHE || CACHE_LL_LEVEL_INT_MEM != 0 || CACHE_LL_LEVEL_EXT_MEM != 1
#error "Native USB DMA alignment requires reviewed uncached internal S3 memory"
#endif
// Exact specialization of IDF6.1 esp_cache_get_alignment -> cache_hal_get_cache_line_size
// for this provider's DMA|INTERNAL allocations. Never reuse for PSRAM or a new SoC.
static inline esp_err_t risc_usb_internal_dma_alignment(uint32_t caps, size_t *out) {
    if (!out || (caps & MALLOC_CAP_SPIRAM) ||
        (caps & (MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA)) != (MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA))
        return ESP_ERR_INVALID_ARG;
    *out = 0;
    return ESP_OK;
}

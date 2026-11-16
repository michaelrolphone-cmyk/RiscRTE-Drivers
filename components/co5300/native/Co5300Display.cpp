// Runtime-native CO5300 selection. This component has no Arduino fallback.
#include <esp_idf_version.h>
#if ESP_IDF_VERSION_MAJOR != 6
#error "CO5300 native backend requires ESP-IDF 6; use the qualified pinned SDK"
#else
#include "Co5300DisplayIdf6.inc"
#endif

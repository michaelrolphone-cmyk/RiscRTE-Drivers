#pragma once
#define ESP_IDF_VERSION_VAL(a,b,c) (((a)<<16)|((b)<<8)|(c))
#ifndef TEST_IDF_VERSION
#define TEST_IDF_VERSION ESP_IDF_VERSION_VAL(6,1,0)
#endif
#define ESP_IDF_VERSION TEST_IDF_VERSION
#define CONFIG_IDF_TARGET_ESP32S3 1

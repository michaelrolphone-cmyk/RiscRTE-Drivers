#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define BLE_NPL_OS_ALIGNMENT __SIZEOF_POINTER__
#define BLE_NPL_TIME_FOREVER UINT32_MAX
typedef uint32_t ble_npl_time_t;
typedef int32_t ble_npl_stime_t;
struct ble_npl_event {
    struct ble_npl_event *next;
    struct ble_npl_eventq *queue;
    void (*fn)(struct ble_npl_event *);
    void *arg;
};
struct ble_npl_eventq {
    struct ble_npl_event *head, *tail;
    unsigned count;
};
struct ble_npl_callout {
    struct ble_npl_event event;
    struct ble_npl_eventq *queue;
    uint32_t ticks;
    bool active;
};
struct ble_npl_mutex {
    unsigned depth;
};
struct ble_npl_sem {
    uint16_t count;
};

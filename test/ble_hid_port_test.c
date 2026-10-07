#include "ble_port.h"
#include "nimble/nimble_npl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "check failed line %d: %s\n", __LINE__, #x);                           \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
static uint64_t now_ms;
static unsigned callbacks;
static uint64_t now(void *c) {
    (void)c;
    return now_ms;
}
static void sleep_ms(void *c, uint32_t n) {
    (void)c;
    now_ms += n;
}
static void call(struct ble_npl_event *e) {
    CHECK(ble_npl_event_get_arg(e) == &callbacks);
    callbacks++;
}
int main(void) {
    risc_platform_clock_api_v1 clock = {1, sizeof(clock), NULL, now, sleep_ms};
    hid_port_bind(NULL, &clock, 0);
    struct ble_npl_eventq q;
    struct ble_npl_event a, b;
    ble_npl_eventq_init(&q);
    ble_npl_event_init(&a, call, &callbacks);
    ble_npl_event_init(&b, call, &callbacks);
    ble_npl_eventq_put(&q, &a);
    ble_npl_eventq_put(&q, &a);
    ble_npl_eventq_put(&q, &b);
    CHECK(q.count == 2);
    CHECK(ble_npl_eventq_get(&q, 0) == &a);
    CHECK(!ble_npl_event_is_queued(&a));
    ble_npl_event_run(&a);
    CHECK(callbacks == 1);
    ble_npl_eventq_remove(&q, &b);
    CHECK(ble_npl_eventq_is_empty(&q));
    struct ble_npl_callout timer;
    ble_npl_callout_init(&timer, &q, call, &callbacks);
    now_ms = UINT32_MAX - 10u;
    CHECK(ble_npl_callout_reset(&timer, 20) == BLE_NPL_OK);
    now_ms += 19;
    hid_port_timers();
    CHECK(!q.count);
    now_ms++;
    hid_port_timers();
    CHECK(q.count == 1);
    ble_npl_event_run(ble_npl_eventq_get(&q, 0));
    CHECK(callbacks == 2);
    CHECK(ble_npl_callout_reset(&timer, 0) == BLE_NPL_OK);
    hid_port_timers();
    CHECK(q.count == 1);
    ble_npl_callout_stop(&timer);
    CHECK(!q.count);
    struct ble_npl_mutex lock;
    CHECK(ble_npl_mutex_init(&lock) == BLE_NPL_OK);
    CHECK(ble_npl_mutex_pend(&lock, 0) == BLE_NPL_OK);
    CHECK(ble_npl_mutex_pend(&lock, 0) == BLE_NPL_OK);
    CHECK(ble_npl_mutex_release(&lock) == BLE_NPL_OK);
    CHECK(ble_npl_mutex_release(&lock) == BLE_NPL_OK);
    CHECK(ble_npl_mutex_release(&lock) == BLE_NPL_BAD_MUTEX);
    struct ble_npl_sem sem;
    CHECK(ble_npl_sem_init(&sem, 1) == BLE_NPL_OK);
    CHECK(ble_npl_sem_pend(&sem, 0) == BLE_NPL_OK);
    CHECK(ble_npl_sem_pend(&sem, 0) == BLE_NPL_ERROR);
    unsigned char *p = hid_malloc(100), *r = hid_calloc(200, 1);
    CHECK(p && r);
    memset(p, 0xa5, 100);
    for (unsigned i = 0; i < 200; i++)
        CHECK(r[i] == 0);
    unsigned char *t = hid_realloc(p, 300);
    CHECK(t);
    for (unsigned i = 0; i < 100; i++)
        CHECK(t[i] == 0xa5);
    hid_free(r);
    hid_free(t);
    p = hid_malloc(24000);
    CHECK(p);
    hid_free(p);
    CHECK(!hid_malloc(SIZE_MAX));
    CHECK(!hid_calloc(SIZE_MAX, 2));
    struct ble_npl_event many[65];
    for (unsigned i = 0; i < 65; i++) {
        ble_npl_event_init(&many[i], call, &callbacks);
        ble_npl_eventq_put(&q, &many[i]);
    }
    CHECK(q.count == 64 && hid_port_faulted());
    hid_port_clear();
    puts("Cooperative NPL FIFO, timer wrap/cancel, locks, arena reuse/overflow and bounded queues: "
         "PASS");
    return 0;
}

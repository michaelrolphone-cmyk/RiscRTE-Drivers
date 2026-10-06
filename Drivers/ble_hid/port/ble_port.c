/* Original cooperative NimBLE NPL and raw-HCI lease transport. All entry points
 * execute on the Runtime owner task. Synchronous HCI waits service RX only;
 * they never recursively dispatch host events or invoke application callbacks. */
#include "ble_port.h"
#include "nimble/nimble_npl.h"
// Upstream transport declarations require the OS mbuf types first.
// clang-format off
#include "os/os.h"
#include "nimble/transport.h"
// clang-format on
#include <limits.h>
#include <string.h>
#ifdef HID_HOST_TEST
#include <stdlib.h>
#endif
#define CALLOUT_LIMIT 16u
#define QUEUE_LIMIT 64u
#define ARENA_BYTES 24576u
static const portable_bluetooth_host_v1 *host;
static const risc_platform_clock_api_v1 *clock_api;
static uint64_t lease;
static bool fault;
static unsigned critical_depth;
static struct ble_npl_callout *timers[CALLOUT_LIMIT];
static unsigned timer_count;
typedef union {
    struct {
        size_t size;
        bool used;
    } m;
    max_align_t align;
} block;
static union {
    max_align_t align;
    uint8_t bytes[ARENA_BYTES];
} arena;
static bool arena_started;
static uint8_t packet[1028];
void hid_port_bind(const portable_bluetooth_host_v1 *h, const risc_platform_clock_api_v1 *c,
                   uint64_t t) {
    host = h;
    clock_api = c;
    lease = t;
    fault = false;
}
bool hid_port_faulted(void) { return fault; }
void hid_port_fault(void) { fault = true; }
void hid_port_clear(void) {
    memset(timers, 0, sizeof(timers));
    timer_count = critical_depth = 0;
    lease = 0;
    host = NULL;
    clock_api = NULL;
    fault = false;
    memset(arena.bytes, 0, sizeof(arena.bytes));
    arena_started = false;
}
void hid_panic(void) {
    fault = true;
#ifdef HID_HOST_TEST
    abort();
#else
    /* No possibly-corrupt stack unwinding. Retain the invocation and module;
     * close only through the established lease, then yield until reset. */
    if (host && lease)
        (void)host->release(host->controls.context, lease);
    for (;;)
        if (clock_api)
            clock_api->sleep_ms(clock_api->context, 50);
#endif
}
void *hid_malloc(size_t n) {
    if (!n || n > ARENA_BYTES - sizeof(block))
        return NULL;
    n = (n + sizeof(block) - 1u) / sizeof(block) * sizeof(block);
    if (!arena_started) {
        ((block *)arena.bytes)->m.size = ARENA_BYTES - sizeof(block);
        arena_started = true;
    }
    size_t at = 0;
    while (at + sizeof(block) <= ARENA_BYTES) {
        block *b = (block *)(arena.bytes + at);
        if (!b->m.used && b->m.size >= n) {
            if (b->m.size >= n + 2 * sizeof(block)) {
                block *next = (block *)(arena.bytes + at + sizeof(block) + n);
                next->m.size = b->m.size - n - sizeof(block);
                next->m.used = false;
                b->m.size = n;
            }
            b->m.used = true;
            return b + 1;
        }
        at += sizeof(block) + b->m.size;
    }
    return NULL;
}
void hid_free(void *p) {
    if (!p)
        return;
    size_t at = 0;
    bool found = false;
    while (at + sizeof(block) <= ARENA_BYTES) {
        block *b = (block *)(arena.bytes + at);
        if ((void *)(b + 1) == p) {
            if (!b->m.used)
                hid_panic();
            memset(p, 0, b->m.size);
            b->m.used = false;
            found = true;
            break;
        }
        at += sizeof(block) + b->m.size;
    }
    if (!found)
        hid_panic();
    at = 0;
    while (at + sizeof(block) <= ARENA_BYTES) {
        block *b = (block *)(arena.bytes + at);
        size_t next = at + sizeof(block) + b->m.size;
        if (next + sizeof(block) > ARENA_BYTES)
            break;
        block *c = (block *)(arena.bytes + next);
        if (!b->m.used && !c->m.used)
            b->m.size += sizeof(block) + c->m.size;
        else
            at = next;
    }
}
void *hid_calloc(size_t n, size_t size) {
    if (size && n > SIZE_MAX / size)
        return NULL;
    void *p = hid_malloc(n * size);
    if (p)
        memset(p, 0, n * size);
    return p;
}
void *hid_realloc(void *p, size_t n) {
    if (!p)
        return hid_malloc(n);
    if (!n) {
        hid_free(p);
        return NULL;
    }
    block *b = (block *)p - 1;
    if (n <= b->m.size)
        return p;
    void *q = hid_malloc(n);
    if (q) {
        memcpy(q, p, b->m.size);
        hid_free(p);
    }
    return q;
}
bool ble_npl_os_started(void) { return true; }
void *ble_npl_get_current_task_id(void) { return &critical_depth; }
void ble_npl_eventq_init(struct ble_npl_eventq *q) { memset(q, 0, sizeof(*q)); }
void ble_npl_eventq_put(struct ble_npl_eventq *q, struct ble_npl_event *e) {
    if (!q || !e)
        hid_panic();
    if (e->queue)
        return;
    if (q->count >= QUEUE_LIMIT) {
        fault = true;
        return;
    }
    e->next = NULL;
    e->queue = q;
    if (q->tail)
        q->tail->next = e;
    else
        q->head = e;
    q->tail = e;
    q->count++;
}
void ble_npl_eventq_remove(struct ble_npl_eventq *q, struct ble_npl_event *e) {
    struct ble_npl_event *prev = NULL, *p = q->head;
    while (p) {
        if (p == e) {
            if (prev)
                prev->next = p->next;
            else
                q->head = p->next;
            if (q->tail == p)
                q->tail = prev;
            p->queue = NULL;
            p->next = NULL;
            q->count--;
            return;
        }
        prev = p;
        p = p->next;
    }
}
struct ble_npl_event *ble_npl_eventq_get(struct ble_npl_eventq *q, ble_npl_time_t timeout) {
    (void)timeout;
    struct ble_npl_event *e = q->head;
    if (e)
        ble_npl_eventq_remove(q, e);
    return e;
}
bool ble_npl_eventq_is_empty(struct ble_npl_eventq *q) { return q->head == NULL; }
void ble_npl_event_init(struct ble_npl_event *e, ble_npl_event_fn *fn, void *arg) {
    memset(e, 0, sizeof(*e));
    e->fn = fn;
    e->arg = arg;
}
bool ble_npl_event_is_queued(struct ble_npl_event *e) { return e->queue != NULL; }
void *ble_npl_event_get_arg(struct ble_npl_event *e) { return e->arg; }
void ble_npl_event_set_arg(struct ble_npl_event *e, void *arg) { e->arg = arg; }
void ble_npl_event_run(struct ble_npl_event *e) {
    if (e && e->fn)
        e->fn(e);
}
ble_npl_error_t ble_npl_mutex_init(struct ble_npl_mutex *m) {
    m->depth = 0;
    return BLE_NPL_OK;
}
ble_npl_error_t ble_npl_mutex_pend(struct ble_npl_mutex *m, ble_npl_time_t t) {
    (void)t;
    if (m->depth == UINT_MAX)
        return BLE_NPL_EBUSY;
    m->depth++;
    return BLE_NPL_OK;
}
ble_npl_error_t ble_npl_mutex_release(struct ble_npl_mutex *m) {
    if (!m->depth)
        return BLE_NPL_BAD_MUTEX;
    m->depth--;
    return BLE_NPL_OK;
}
ble_npl_error_t ble_npl_sem_init(struct ble_npl_sem *s, uint16_t n) {
    s->count = n;
    return BLE_NPL_OK;
}
ble_npl_error_t ble_npl_sem_release(struct ble_npl_sem *s) {
    if (s->count == UINT16_MAX)
        return BLE_NPL_EBUSY;
    s->count++;
    return BLE_NPL_OK;
}
uint16_t ble_npl_sem_get_count(struct ble_npl_sem *s) { return s->count; }
ble_npl_error_t ble_npl_sem_pend(struct ble_npl_sem *s, ble_npl_time_t t) {
    uint32_t begin = ble_npl_time_get();
    if (t > 500)
        t = 500;
    while (!s->count) {
        if (fault || !lease)
            return BLE_NPL_ERROR;
        if ((uint32_t)(ble_npl_time_get() - begin) >= t)
            return BLE_NPL_TIMEOUT;
        if (!hid_port_receive())
            return BLE_NPL_ERROR;
        if (!s->count)
            clock_api->sleep_ms(clock_api->context, 1);
    }
    s->count--;
    return BLE_NPL_OK;
}
void ble_npl_callout_init(struct ble_npl_callout *c, struct ble_npl_eventq *q, ble_npl_event_fn *fn,
                          void *arg) {
    unsigned i;
    for (i = 0; i < timer_count; i++)
        if (timers[i] == c)
            break;
    if (i == timer_count) {
        if (timer_count == CALLOUT_LIMIT)
            hid_panic();
        timers[timer_count++] = c;
    } else
        ble_npl_callout_stop(c);
    memset(c, 0, sizeof(*c));
    c->queue = q;
    ble_npl_event_init(&c->event, fn, arg);
}
ble_npl_error_t ble_npl_callout_reset(struct ble_npl_callout *c, ble_npl_time_t t) {
    if (t > INT32_MAX)
        return BLE_NPL_EINVAL;
    ble_npl_callout_stop(c);
    c->ticks = ble_npl_time_get() + t;
    c->active = true;
    return BLE_NPL_OK;
}
void ble_npl_callout_stop(struct ble_npl_callout *c) {
    c->active = false;
    if (c->event.queue)
        ble_npl_eventq_remove(c->event.queue, &c->event);
}
bool ble_npl_callout_is_active(struct ble_npl_callout *c) { return c->active; }
ble_npl_time_t ble_npl_callout_get_ticks(struct ble_npl_callout *c) { return c->ticks; }
ble_npl_time_t ble_npl_callout_remaining_ticks(struct ble_npl_callout *c, ble_npl_time_t now) {
    return c->active && (int32_t)(c->ticks - now) > 0 ? c->ticks - now : 0;
}
void ble_npl_callout_set_arg(struct ble_npl_callout *c, void *a) { c->event.arg = a; }
void hid_port_timers(void) {
    uint32_t now = ble_npl_time_get();
    for (unsigned i = 0; i < timer_count; i++) {
        struct ble_npl_callout *c = timers[i];
        if (c->active && (int32_t)(now - c->ticks) >= 0) {
            c->active = false;
            if (c->queue)
                ble_npl_eventq_put(c->queue, &c->event);
            else
                ble_npl_event_run(&c->event);
        }
    }
}
ble_npl_time_t ble_npl_time_get(void) {
    return clock_api ? (uint32_t)clock_api->monotonic_ms(clock_api->context) : 0;
}
ble_npl_error_t ble_npl_time_ms_to_ticks(uint32_t n, ble_npl_time_t *out) {
    if (!out)
        return BLE_NPL_EINVAL;
    *out = n;
    return BLE_NPL_OK;
}
ble_npl_error_t ble_npl_time_ticks_to_ms(ble_npl_time_t n, uint32_t *out) {
    return ble_npl_time_ms_to_ticks(n, out);
}
ble_npl_time_t ble_npl_time_ms_to_ticks32(uint32_t n) { return n; }
uint32_t ble_npl_time_ticks_to_ms32(ble_npl_time_t n) { return n; }
void ble_npl_time_delay(ble_npl_time_t n) {
    if (clock_api)
        clock_api->sleep_ms(clock_api->context, n > 500 ? 500 : n);
}
uint32_t ble_npl_hw_enter_critical(void) { return critical_depth++; }
void ble_npl_hw_exit_critical(uint32_t n) {
    if (critical_depth != n + 1)
        hid_panic();
    critical_depth = n;
}
bool ble_npl_hw_is_in_critical(void) { return critical_depth != 0; }
void ble_transport_ll_init(void) {}
int ble_transport_to_ll_cmd_impl(void *p) {
    uint8_t *b = p;
    bool ok = host && lease && !fault &&
              host->send_owned(host->controls.context, lease, 1, b, (size_t)b[2] + 3);
    ble_transport_free(p);
    if (!ok)
        fault = true;
    return ok ? 0 : 1;
}
int ble_transport_to_ll_acl_impl(struct os_mbuf *om) {
    size_t n = OS_MBUF_PKTLEN(om);
    uint8_t tx[1028];
    bool ok = n >= 4 && n <= sizeof(tx) && os_mbuf_copydata(om, 0, (int)n, tx) == 0;
    if (ok)
        ok = host && lease && !fault && host->send_owned(host->controls.context, lease, 2, tx, n);
    os_mbuf_free_chain(om);
    if (!ok)
        fault = true;
    return ok ? 0 : 1;
}
int ble_transport_to_ll_iso_impl(struct os_mbuf *om) {
    os_mbuf_free_chain(om);
    return 1;
}
bool hid_port_receive(void) {
    if (!host || !lease || fault)
        return false;
    uint8_t type = 0;
    size_t n = 0;
    int32_t rc = host->next_owned(host->controls.context, lease, &type, packet, sizeof(packet), &n);
    if (rc < 0) {
        fault = true;
        return false;
    }
    if (!rc)
        return true;
    if (type == 4 && n >= 2 && n <= 257 && n == (size_t)packet[1] + 2) {
        void *p = ble_transport_alloc_evt(0);
        if (!p) {
            fault = true;
            return false;
        }
        memcpy(p, packet, n);
        if (ble_transport_to_hs_evt(p))
            fault = true;
    } else if (type == 2 && n >= 4 && n <= sizeof(packet) &&
               n == 4u + packet[2] + ((size_t)packet[3] << 8)) {
        struct os_mbuf *om = ble_transport_alloc_acl_from_ll();
        if (!om) {
            fault = true;
            return false;
        }
        if (os_mbuf_append(om, packet, (uint16_t)n)) {
            os_mbuf_free_chain(om);
            fault = true;
            return false;
        }
        if (ble_transport_to_hs_acl(om))
            fault = true;
    } else
        fault = true;
    return !fault;
}

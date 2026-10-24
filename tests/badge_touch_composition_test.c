/* Host composition only: link the production board accessor, common prepare,
 * product touch adapter and CST9217 as separate translation units against the
 * real Drivers SDK. Only the bus, clock and reset boundary are modeled here.
 * GPIO1 is represented by an independent native-owner sentinel, not a mock
 * implementation of the display backend. No firmware boot/hardware claim.
 *
 * The production helper has no consumer-lifecycle hook. finish_after_touch()
 * below is explicitly a test caller obeying its stop-before-finish contract.
 */
#include "touch-startup.h"
#include "startup.h"
#include "RiscTouchV1.h"
#include "RiscPlatformClockV1.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { POWER_ADDRESS = 0x34, TOUCH_ADDRESS = 0x5a, DISPLAY_RESET = 1, TOUCH_RESET = 2 };
enum operation {
    POWER_CLAIM, POWER_READ, POWER_RELEASE, RESET_CLAIM, RESET_READ,
    RESET_DELAY, RESET_WRITE, RESET_RELEASE, TOUCH_CLAIM, TOUCH_TRANSFER,
    TOUCH_SLEEP, TOUCH_RELEASE, OP_COUNT
};
enum fault_kind { NO_FAULT, FAIL, FALSE_RETAINED, TRUE_ZERO, WRONG_VALUE };
typedef struct { enum operation op; uint64_t token, at_ms; unsigned value; } event;
typedef struct {
    uint64_t serial, time_ms, bus_owners[128], gpio_owners[49], native_owner;
    bool gpio_levels[49], reset_recovered;
    uint8_t pmic[256], before_pmic[256], report[10];
    uint16_t selected_register;
    uint64_t selected_at;
    bool selection_pending;
    enum operation fail_op;
    enum fault_kind fault;
    unsigned fail_nth, counts[OP_COUNT];
    bool fail_power_release, fail_touch_release, fail_reset_release;
    event events[1024];
    size_t event_count;
    risc_board_prepare_state preparation;
    risc_i2c_bus_api_v1 bus;
    risc_board_prepare_io io;
    risc_platform_clock_api_v1 clock;
    risc_provider_dependency_v1 dependencies[2];
} model;

extern const risc_driver_v2 *t5_driver_get(uint32_t abi);
static const risc_driver_v2 *driver;
static const risc_touch_api_v1 *touch;

static enum fault_kind record(model *m, enum operation op, uint64_t token, unsigned value) {
    assert(m->event_count < sizeof(m->events) / sizeof(m->events[0]));
    m->events[m->event_count++] = (event){op, token, m->time_ms, value};
    ++m->counts[op];
    return m->fault != NO_FAULT && m->fail_op == op && m->counts[op] == m->fail_nth
           ? m->fault : NO_FAULT;
}

static void invariant(const model *m) {
    assert(m->gpio_owners[DISPLAY_RESET] == m->native_owner);
    assert(m->gpio_levels[DISPLAY_RESET]);
    assert(!memcmp(m->pmic, m->before_pmic, sizeof(m->pmic)));
    if (m->preparation.power_claim)
        assert(m->preparation.power_claim == m->bus_owners[POWER_ADDRESS]);
    if (m->preparation.reset_claims[0])
        assert(m->preparation.reset_claims[0] == m->gpio_owners[TOUCH_RESET]);
    for (size_t i = 1; i < RISC_BOARD_RESET_MAX; ++i)
        assert(!m->preparation.reset_claims[i]);
}

static void touch_reset_owned(const model *m) {
    invariant(m);
    assert(m->preparation.ready && !m->preparation.power_claim);
    assert(m->preparation.reset_claims[0] && m->reset_recovered);
    assert(m->gpio_levels[TOUCH_RESET]);
}

static bool claim_device(void *context, uint8_t address, uint64_t *out) {
    model *m = context;
    assert(address == POWER_ADDRESS || address == TOUCH_ADDRESS);
    *out = 0;
    enum operation op = address == POWER_ADDRESS ? POWER_CLAIM : TOUCH_CLAIM;
    enum fault_kind fault = record(m, op, 0, address);
    if (address == TOUCH_ADDRESS) touch_reset_owned(m);
    if (m->bus_owners[address] || fault == FAIL) return false;
    if (fault == TRUE_ZERO) return true;
    *out = m->bus_owners[address] = ++m->serial;
    return fault != FALSE_RETAINED;
}

static bool release_device(void *context, uint64_t token) {
    model *m = context;
    assert(token);
    const bool power = token == m->bus_owners[POWER_ADDRESS];
    assert(power || token == m->bus_owners[TOUCH_ADDRESS]);
    enum operation op = power ? POWER_RELEASE : TOUCH_RELEASE;
    enum fault_kind fault = record(m, op, token, 0);
    if (!power) touch_reset_owned(m);
    if (fault != NO_FAULT || (power ? m->fail_power_release : m->fail_touch_release))
        return false;
    m->bus_owners[power ? POWER_ADDRESS : TOUCH_ADDRESS] = 0;
    if (!power) m->selection_pending = false;
    return true;
}

static bool transact(void *context, uint64_t token, const uint8_t *tx, size_t tx_size,
                     uint8_t *rx, size_t rx_size, uint32_t timeout) {
    model *m = context;
    assert(token && timeout == 20);
    if (token == m->bus_owners[POWER_ADDRESS]) {
        /* The only accepted PMIC traffic is a one-byte selector/read, never a
         * register write, mode change, supply gate or key/interrupt command. */
        assert(tx && tx_size == 1 && rx && rx_size == 1);
        assert(tx[0] == 0x03 || tx[0] == 0x80);
        assert(!m->gpio_owners[TOUCH_RESET]);
        enum fault_kind fault = record(m, POWER_READ, token, tx[0]);
        if (fault == FAIL) return false;
        *rx = fault == WRONG_VALUE ? 0 : m->pmic[tx[0]];
        return true;
    }
    assert(token == m->bus_owners[TOUCH_ADDRESS]);
    touch_reset_owned(m);
    assert(!m->bus_owners[POWER_ADDRESS]);
    assert((tx_size != 0) != (rx_size != 0)); /* STOP-separated, not repeated START. */
    enum fault_kind fault = record(m, TOUCH_TRANSFER, token, (unsigned)tx_size);
    if (fault == FAIL) return false;
    if (tx_size) {
        assert(tx && tx_size == 2 && !rx);
        const uint16_t reg = (uint16_t)(((uint16_t)tx[0] << 8) | tx[1]);
        assert(reg == 0xd101 || reg == 0xd1fc || reg == 0xd1f8 || reg == 0xd204 || reg == 0xd000);
        m->selected_register = reg;
        m->selected_at = m->time_ms;
        m->selection_pending = true;
        return true;
    }
    assert(!tx && rx && m->selection_pending && m->time_ms - m->selected_at >= 2);
    m->selection_pending = false;
    if (m->selected_register == 0xd000) {
        assert(rx_size == sizeof(m->report));
        memcpy(rx, m->report, rx_size);
    } else {
        assert(rx_size == 4);
        memset(rx, 0, rx_size);
        if (m->selected_register == 0xd1f8) {
            rx[0] = rx[2] = (uint8_t)466;
            rx[1] = rx[3] = (uint8_t)(466 >> 8);
        } else assert(m->selected_register == 0xd1fc || m->selected_register == 0xd204);
    }
    return true;
}

static bool claim_reset(void *context, uint8_t pin, bool initial_level, uint64_t *out) {
    model *m = context;
    assert(pin == TOUCH_RESET && !initial_level); /* No helper claim of GPIO1. */
    assert(!m->bus_owners[POWER_ADDRESS] && !m->bus_owners[TOUCH_ADDRESS]);
    assert(m->counts[POWER_READ] == 2);
    *out = 0;
    enum fault_kind fault = record(m, RESET_CLAIM, 0, pin);
    if (m->gpio_owners[pin] || fault == FAIL) return false;
    if (fault == TRUE_ZERO) return true;
    *out = m->gpio_owners[pin] = ++m->serial;
    m->gpio_levels[pin] = initial_level;
    m->reset_recovered = false;
    return fault != FALSE_RETAINED;
}

static bool read_reset(void *context, uint64_t token, bool *level) {
    model *m = context;
    assert(token && token == m->gpio_owners[TOUCH_RESET]);
    enum fault_kind fault = record(m, RESET_READ, token, m->gpio_levels[TOUCH_RESET]);
    if (fault == FAIL) return false;
    *level = m->gpio_levels[TOUCH_RESET] ^ (fault == WRONG_VALUE);
    return true;
}

static bool write_reset(void *context, uint64_t token, bool level) {
    model *m = context;
    assert(token && token == m->gpio_owners[TOUCH_RESET] && level);
    assert(!m->gpio_levels[TOUCH_RESET]);
    enum fault_kind fault = record(m, RESET_WRITE, token, level);
    if (fault == FAIL) return false;
    m->gpio_levels[TOUCH_RESET] = level;
    return true;
}

static bool reset_delay(void *context, uint32_t milliseconds) {
    model *m = context;
    assert(m->gpio_owners[TOUCH_RESET]);
    assert(milliseconds == (m->gpio_levels[TOUCH_RESET] ? 50u : 10u));
    enum fault_kind fault = record(m, RESET_DELAY, m->gpio_owners[TOUCH_RESET], milliseconds);
    if (fault == FAIL) return false;
    m->time_ms += milliseconds;
    if (milliseconds == 50) m->reset_recovered = true;
    return true;
}

static bool release_reset(void *context, uint64_t token) {
    model *m = context;
    assert(token && token == m->gpio_owners[TOUCH_RESET]);
    /* An oracle for the caller's contract, NOT a production interlock. */
    assert(!m->bus_owners[TOUCH_ADDRESS]);
    enum fault_kind fault = record(m, RESET_RELEASE, token, TOUCH_RESET);
    if (fault != NO_FAULT || m->fail_reset_release) return false;
    m->gpio_owners[TOUCH_RESET] = 0;
    m->reset_recovered = false;
    return true;
}

static uint64_t monotonic_ms(void *context) { return ((model *)context)->time_ms; }
static void sleep_ms(void *context, uint32_t milliseconds) {
    model *m = context;
    touch_reset_owned(m);
    assert(milliseconds == 2 || milliseconds == 10);
    (void)record(m, TOUCH_SLEEP, m->bus_owners[TOUCH_ADDRESS], milliseconds);
    m->time_ms += milliseconds;
}

static void report(model *m, unsigned x, unsigned y, bool down) {
    assert(x < 466 && y < 466);
    memset(m->report, 0, sizeof(m->report));
    m->report[0] = down ? 6 : 0;
    m->report[1] = (uint8_t)(x >> 4);
    m->report[2] = (uint8_t)(y >> 4);
    m->report[3] = (uint8_t)(((x & 15) << 4) | (y & 15));
    m->report[5] = down;
    m->report[6] = 0xab;
}

static void setup(model *m) {
    /* Do not hide leaks by resetting a live driver or its ownership model. */
    assert(driver->quiesce());
    driver->stop();
    memset(m, 0, sizeof(*m));
    m->time_ms = 100;
    m->native_owner = m->gpio_owners[DISPLAY_RESET] = ++m->serial;
    m->gpio_levels[DISPLAY_RESET] = true;
    for (size_t i = 0; i < sizeof(m->pmic); ++i) m->pmic[i] = (uint8_t)(i ^ 0xa5);
    m->pmic[0x03] = 0x4a;
    m->pmic[0x80] = 0xa5; /* Enabled DCDC1 plus unrelated preserved bits. */
    memcpy(m->before_pmic, m->pmic, sizeof(m->pmic));
    m->bus = (risc_i2c_bus_api_v1){1, sizeof(m->bus), m, claim_device, transact, release_device};
    m->io = (risc_board_prepare_io){m, claim_reset, write_reset, read_reset, release_reset, reset_delay};
    m->clock = (risc_platform_clock_api_v1){1, sizeof(m->clock), m, monotonic_ms, sleep_ms};
    m->dependencies[0] = (risc_provider_dependency_v1){"i2c.bus", 1, &m->bus};
    m->dependencies[1] = (risc_provider_dependency_v1){"platform.clock", 1, &m->clock};
    report(m, 0, 0, false);
}

static bool prepare(model *m) { return risc_badge_prepare_touch(&m->preparation, 101, &m->bus, &m->io); }
static bool start_touch(model *m) { return driver->start(m->dependencies, 2); }

static bool finish_after_touch(model *m) {
    /* Test-side ownership discipline. The helper itself cannot inspect CST. */
    if (!driver->quiesce()) return false;
    driver->stop();
    return risc_board_prepare_finish(&m->preparation);
}

static void clean(const model *m) {
    invariant(m);
    assert(!m->preparation.ready && !m->preparation.bus && !m->preparation.io);
    assert(!m->preparation.power_claim && !m->gpio_owners[TOUCH_RESET]);
    for (size_t i = 0; i < sizeof(m->bus_owners) / sizeof(m->bus_owners[0]); ++i)
        assert(!m->bus_owners[i]);
    assert(!touch->subscribe(NULL));
}

static void fail_at(model *m, enum operation op, unsigned relative, enum fault_kind kind) {
    m->fail_op = op;
    m->fail_nth = m->counts[op] + relative;
    m->fault = kind;
}

static void prepare_prefix(const model *m) {
    static const enum operation expected[] = {
        POWER_CLAIM, POWER_READ, POWER_READ, POWER_RELEASE,
        RESET_CLAIM, RESET_READ, RESET_DELAY, RESET_WRITE, RESET_READ, RESET_DELAY
    };
    assert(m->event_count == sizeof(expected) / sizeof(expected[0]));
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i)
        assert(m->events[i].op == expected[i]);
    assert(m->events[1].value == 3 && m->events[2].value == 0x80);
    assert(m->events[4].value == TOUCH_RESET);
    assert(m->events[5].value == 0 && m->events[8].value == 1);
    assert(m->events[6].value == 10 && m->events[9].value == 50);
    assert(m->events[7].at_ms - m->events[4].at_ms == 10);
    assert(m->time_ms - m->events[7].at_ms == 50);
    touch_reset_owned(m);
}

static void reject_prepare_retry(model *m) {
    size_t before = m->event_count;
    assert(!prepare(m) && m->event_count == before);
    invariant(m);
}

static void retry_full_cycle(model *m) {
    /* Reuse this very state and callback model after checked cleanup. */
    m->fault = NO_FAULT;
    memset(m->counts, 0, sizeof(m->counts));
    m->event_count = 0;
    assert(prepare(m));
    prepare_prefix(m);
    assert(start_touch(m));
    assert(touch->poll(NULL, 1));
    assert(finish_after_touch(m));
    clean(m);
}

static void lifecycle(void) {
    model m;
    setup(&m);
    assert(prepare(&m));
    prepare_prefix(&m);
    const uint64_t reset_owner = m.gpio_owners[TOUCH_RESET];
    reject_prepare_retry(&m);
    assert(start_touch(&m) && m.counts[TOUCH_TRANSFER] == 8);
    assert(m.events[10].op == TOUCH_CLAIM && m.events[10].at_ms == 160);
    const uint64_t token = touch->subscribe(NULL);
    assert(token);
    risc_touch_snapshot_v1 snapshot;
    assert(touch->snapshot(NULL, &snapshot) && snapshot.width == 466 && snapshot.height == 466);
    assert(!snapshot.contact_count);
    size_t events = m.event_count;
    assert(!finish_after_touch(&m)); /* Subscriber prevents CST quiescence. */
    driver->stop();
    assert(events == m.event_count && !m.counts[RESET_RELEASE]);
    assert(m.gpio_owners[TOUCH_RESET] == reset_owner && m.preparation.ready);
    uint64_t sequence = 0;
    for (unsigned i = 0; i < 12; ++i) {
        unsigned x = (i * 43) % 466, y = (465 - i * 29) % 466;
        report(&m, x, y, true);
        assert(touch->poll(NULL, 1));
        risc_touch_event_v1 event_value;
        assert(touch->next(NULL, token, &event_value) == 1);
        assert(event_value.kind == (i ? RISC_TOUCH_EVENT_MOVE : RISC_TOUCH_EVENT_DOWN));
        assert(event_value.x == x && event_value.y == y && event_value.sequence > sequence);
        sequence = event_value.sequence;
        assert(touch->snapshot(NULL, &snapshot) && snapshot.contact_count == 1);
        assert(snapshot.contacts[0].x == x && snapshot.contacts[0].y == y);
        assert(m.gpio_owners[TOUCH_RESET] == reset_owner && !m.counts[RESET_RELEASE]);
    }
    report(&m, 0, 0, false);
    assert(touch->poll(NULL, 1));
    risc_touch_event_v1 event_value;
    assert(touch->next(NULL, token, &event_value) == 1 && event_value.kind == RISC_TOUCH_EVENT_UP);
    assert(touch->unsubscribe(NULL, token));

    m.fail_touch_release = true;
    assert(!finish_after_touch(&m) && !m.counts[RESET_RELEASE]);
    driver->stop();
    assert(m.bus_owners[TOUCH_ADDRESS] && m.gpio_owners[TOUCH_RESET] == reset_owner);
    assert(touch->snapshot(NULL, &snapshot));
    m.fail_touch_release = false;
    m.fail_reset_release = true;
    assert(!finish_after_touch(&m));
    assert(!m.bus_owners[TOUCH_ADDRESS] && !touch->snapshot(NULL, &snapshot));
    assert(m.preparation.bus == &m.bus && m.preparation.io == &m.io);
    assert(!m.preparation.ready && m.preparation.reset_claims[0] == reset_owner);
    reject_prepare_retry(&m);
    const unsigned releases = m.counts[TOUCH_RELEASE];
    m.fail_reset_release = false;
    assert(finish_after_touch(&m) && m.counts[TOUCH_RELEASE] == releases);
    clean(&m);
    events = m.event_count;
    assert(finish_after_touch(&m) && m.event_count == events);
    retry_full_cycle(&m);
}

static void prepare_faults(void) {
    static const struct { enum operation op; unsigned nth; enum fault_kind kind; } cases[] = {
        {POWER_CLAIM, 1, FAIL}, {POWER_CLAIM, 1, FALSE_RETAINED}, {POWER_CLAIM, 1, TRUE_ZERO},
        {POWER_READ, 1, FAIL}, {POWER_READ, 2, FAIL},
        {POWER_READ, 1, WRONG_VALUE}, {POWER_READ, 2, WRONG_VALUE},
        {POWER_RELEASE, 1, FAIL},
        {RESET_CLAIM, 1, FAIL}, {RESET_CLAIM, 1, FALSE_RETAINED}, {RESET_CLAIM, 1, TRUE_ZERO},
        {RESET_READ, 1, FAIL}, {RESET_READ, 2, FAIL},
        {RESET_READ, 1, WRONG_VALUE}, {RESET_READ, 2, WRONG_VALUE},
        {RESET_DELAY, 1, FAIL}, {RESET_DELAY, 2, FAIL}, {RESET_WRITE, 1, FAIL}
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        model m;
        setup(&m);
        fail_at(&m, cases[i].op, cases[i].nth, cases[i].kind);
        assert(!prepare(&m) && !m.preparation.ready);
        assert(m.preparation.bus == &m.bus && m.preparation.io == &m.io);
        assert(!m.counts[TOUCH_CLAIM] && !m.counts[RESET_RELEASE]);
        assert(m.preparation.power_claim == m.bus_owners[POWER_ADDRESS]);
        assert(m.preparation.reset_claims[0] == m.gpio_owners[TOUCH_RESET]);
        if (cases[i].op <= POWER_RELEASE) assert(!m.counts[RESET_CLAIM]);
        else assert(!m.bus_owners[POWER_ADDRESS]);
        reject_prepare_retry(&m);
        const uint64_t power_owner = m.preparation.power_claim;
        const uint64_t reset_owner = m.preparation.reset_claims[0];
        m.fail_power_release = m.fail_reset_release = true;
        if (power_owner || reset_owner) {
            assert(!finish_after_touch(&m));
            assert(m.preparation.power_claim == power_owner);
            assert(m.preparation.reset_claims[0] == reset_owner);
            assert(m.preparation.bus == &m.bus && m.preparation.io == &m.io);
            reject_prepare_retry(&m);
        }
        m.fail_power_release = m.fail_reset_release = false;
        assert(finish_after_touch(&m));
        clean(&m);
        retry_full_cycle(&m);
    }
    puts("  prepare: 18 acquisition/readback/reset/release faults retain only owned tokens; checked cleanup/retry PASS");
}

static void cst_start_faults(void) {
    for (unsigned fault = 0; fault < 11; ++fault) {
        for (unsigned retain = 0; retain < 2; ++retain) {
            model m;
            setup(&m);
            assert(prepare(&m));
            const uint64_t reset_owner = m.preparation.reset_claims[0];
            if (fault < 3)
                fail_at(&m, TOUCH_CLAIM, 1, fault == 0 ? FAIL : fault == 1 ? FALSE_RETAINED : TRUE_ZERO);
            else fail_at(&m, TOUCH_TRANSFER, fault - 2, FAIL);
            m.fail_touch_release = retain;
            assert(!start_touch(&m));
            assert(!touch->subscribe(NULL));
            assert(m.preparation.ready && m.preparation.reset_claims[0] == reset_owner);
            assert(!m.counts[RESET_RELEASE]);
            if (retain && fault != 0 && fault != 2) {
                assert(m.bus_owners[TOUCH_ADDRESS]);
                const size_t events = m.event_count;
                assert(!start_touch(&m) && events == m.event_count);
                assert(!finish_after_touch(&m));
                driver->stop();
                assert(m.bus_owners[TOUCH_ADDRESS] && !m.counts[RESET_RELEASE]);
                assert(m.preparation.reset_claims[0] == reset_owner);
            } else assert(!m.bus_owners[TOUCH_ADDRESS]);
            m.fail_touch_release = false;
            m.fault = NO_FAULT;
            assert(driver->quiesce());
            driver->stop();
            /* Retry the consumer with the SAME retained GPIO2 owner, no pulse
             * or release/reclaim of a still-prepared consumer's reset. */
            const unsigned reset_claims = m.counts[RESET_CLAIM];
            assert(start_touch(&m) && touch->poll(NULL, 1));
            assert(m.preparation.reset_claims[0] == reset_owner);
            assert(m.counts[RESET_CLAIM] == reset_claims && !m.counts[RESET_RELEASE]);
            assert(finish_after_touch(&m));
            clean(&m);
        }
    }
    puts("  CST startup: 3 claim outcomes and all 8 probe transfers, checked release failure/retry PASS");
}

static void cst_poll_faults(void) {
    for (unsigned failed_phase = 1; failed_phase <= 2; ++failed_phase) {
        model m;
        setup(&m);
        assert(prepare(&m) && start_touch(&m));
        const uint64_t reset_owner = m.preparation.reset_claims[0];
        const uint64_t subscription = touch->subscribe(NULL);
        assert(subscription);
        fail_at(&m, TOUCH_TRANSFER, failed_phase, FAIL);
        report(&m, 465, 465, true);
        assert(!touch->poll(NULL, 1));
        risc_touch_snapshot_v1 snapshot;
        risc_touch_event_v1 event_value;
        assert(!touch->snapshot(NULL, &snapshot));
        assert(touch->next(NULL, subscription, &event_value) == -1);
        assert(m.bus_owners[TOUCH_ADDRESS] && m.preparation.reset_claims[0] == reset_owner);
        assert(!finish_after_touch(&m) && !m.counts[RESET_RELEASE]);
        m.fault = NO_FAULT;
        assert(touch->poll(NULL, 1));
        assert(touch->snapshot(NULL, &snapshot) && snapshot.contacts[0].x == 465);
        assert(touch->next(NULL, subscription, &event_value) == 1 && event_value.kind == RISC_TOUCH_EVENT_DOWN);
        assert(touch->unsubscribe(NULL, subscription));
        assert(finish_after_touch(&m));
        clean(&m);
    }
    puts("  CST poll: selector/read failures signal a gap without surrendering reset custody PASS");
}

static void admission(void) {
    model m;
    setup(&m);
    const risc_board_prepare_config *config = risc_badge_prepare_config();
    assert(config && config->reset_count == 2 && config->bus_instance_id == 101);
    assert(config->resets[0].pin == DISPLAY_RESET && config->resets[1].pin == TOUCH_RESET);
    assert(!risc_badge_prepare_touch(NULL, 101, &m.bus, &m.io));
    assert(!risc_badge_prepare_touch(&m.preparation, 102, &m.bus, &m.io));
    assert(!risc_badge_prepare_touch(&m.preparation, 101, NULL, &m.io));
    assert(!risc_badge_prepare_touch(&m.preparation, 101, &m.bus, NULL));
    assert(!m.event_count);
    clean(&m);
    /* The shared address owner rejects conflicting PMIC acquisition. Cleanup
     * must not release another owner's token after a failed/zero claim. */
    const uint64_t other_owner = m.bus_owners[POWER_ADDRESS] = ++m.serial;
    assert(!prepare(&m) && !m.preparation.power_claim);
    assert(finish_after_touch(&m));
    assert(m.bus_owners[POWER_ADDRESS] == other_owner && !m.counts[POWER_RELEASE]);
    m.bus_owners[POWER_ADDRESS] = 0; /* Independent owner ends its own claim. */
    clean(&m);
    retry_full_cycle(&m);
}

int main(void) {
    driver = t5_driver_get(RISC_PROVIDER_DRIVER_ABI_V2);
    assert(driver && !t5_driver_get(1));
    touch = driver->capability;
    assert(touch && touch->api_version == RISC_TOUCH_API_V1);
    admission();
    lifecycle();
    prepare_faults();
    cst_start_faults();
    cst_poll_faults();
    puts("Badge touch host composition: real prepare/board/adapter/CST; PMIC read-only first; GPIO2 10/50 ms custody; GPIO1 untouched; caller-ordered teardown PASS");
    return 0;
}

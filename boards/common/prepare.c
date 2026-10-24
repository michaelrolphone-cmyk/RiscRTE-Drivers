#include "prepare.h"

static bool valid(const risc_board_prepare_config *c, uint64_t instance,
                  const risc_i2c_bus_api_v1 *bus, const risc_board_prepare_io *io) {
    if (!c || !instance || instance != c->bus_instance_id ||
        c->power_address < 8 || c->power_address > 0x77 ||
        !c->identity_mask || (c->identity_value & ~c->identity_mask) ||
        !c->enable_mask || (c->enable_value & ~c->enable_mask) ||
        !c->transaction_timeout_ms || c->transaction_timeout_ms > 100 ||
        !c->reset_count || c->reset_count > RISC_BOARD_RESET_MAX ||
        !bus || bus->api_version != 1 || bus->struct_size < sizeof(*bus) ||
        !bus->claim_device || !bus->transact || !bus->release_device ||
        !io || !io->claim_reset || !io->write_reset || !io->read_reset ||
        !io->release_reset || !io->delay_ms) return false;
    for (size_t i = 0; i < c->reset_count; ++i) {
        const risc_board_reset *r = &c->resets[i];
        if (r->pin > 48 || !r->assert_ms || r->assert_ms > 500 ||
            !r->recovery_ms || r->recovery_ms > 500) return false;
        for (size_t j = 0; j < i; ++j)
            if (r->pin == c->resets[j].pin) return false;
    }
    return true;
}
static bool level_is(const risc_board_prepare_state *s, uint64_t token, bool expected) {
    bool level = !expected;
    return s->io->read_reset(s->io->context, token, &level) && level == expected;
}
static bool read_power(const risc_board_prepare_state *s, uint8_t reg,
                       uint8_t mask, uint8_t expected, uint32_t timeout) {
    uint8_t value = 0;
    return s->bus->transact(s->bus->context, s->power_claim, &reg, 1,
                            &value, 1, timeout) && (value & mask) == expected;
}
bool risc_board_prepare(risc_board_prepare_state *s, const risc_board_prepare_config *c,
                        uint64_t instance, const risc_i2c_bus_api_v1 *bus,
                        const risc_board_prepare_io *io) {
    if (!s || s->bus || s->io || s->power_claim || s->ready) return false;
    for (size_t i = 0; i < RISC_BOARD_RESET_MAX; ++i)
        if (s->reset_claims[i]) return false;
    if (!valid(c, instance, bus, io)) return false;
    s->bus = bus;
    s->io = io;
    bool claimed = bus->claim_device(bus->context, c->power_address, &s->power_claim);
    if (!claimed || !s->power_claim) return false;
    if (!read_power(s, c->identity_register, c->identity_mask, c->identity_value,
                    c->transaction_timeout_ms) ||
        !read_power(s, c->enable_register, c->enable_mask, c->enable_value,
                    c->transaction_timeout_ms)) return false;
    if (!bus->release_device(bus->context, s->power_claim)) return false;
    s->power_claim = 0;
    /* Each controller's reset sequence is explicit and separately timed. No
     * controller is probed or started before all required reset steps succeed. */
    for (size_t i = 0; i < c->reset_count; ++i) {
        const risc_board_reset *r = &c->resets[i];
        claimed = io->claim_reset(io->context, r->pin, r->active_high, &s->reset_claims[i]);
        uint64_t token = s->reset_claims[i];
        if (!claimed || !token || !level_is(s, token, r->active_high) ||
            !io->delay_ms(io->context, r->assert_ms) ||
            !io->write_reset(io->context, token, !r->active_high) ||
            !level_is(s, token, !r->active_high) ||
            !io->delay_ms(io->context, r->recovery_ms)) return false;
    }
    s->ready = true;
    return true;
}
bool risc_board_prepare_finish(risc_board_prepare_state *s) {
    if (!s) return false;
    s->ready = false;
    bool ok = true;
    for (size_t i = RISC_BOARD_RESET_MAX; i-- > 0;) {
        if (!s->reset_claims[i]) continue;
        if (s->io && s->io->release_reset &&
            s->io->release_reset(s->io->context, s->reset_claims[i]))
            s->reset_claims[i] = 0;
        else ok = false;
    }
    if (s->power_claim) {
        if (s->bus && s->bus->release_device &&
            s->bus->release_device(s->bus->context, s->power_claim)) s->power_claim = 0;
        else ok = false;
    }
    if (ok) { s->bus = NULL; s->io = NULL; }
    return ok;
}

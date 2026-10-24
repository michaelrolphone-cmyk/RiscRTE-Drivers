#include "prepare.h"
#include "startup-config.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    unsigned calls, fail_at, zero_token_at, power_reads, power_claims, reset_claims, delays;
    bool mismatch, supply_off, bad_id, retained, fail_releases;
    bool power_live, pins[49], levels[49];
    uint32_t elapsed;
    uint64_t serial, power_token, pin_tokens[49];
} fake;
static bool pass(fake *f) { return ++f->calls != f->fail_at; }
static bool claim_power(void *ctx, uint8_t address, uint64_t *token) {
    fake *f = ctx; assert(address == 0x34); *token = 0;
    bool ok = pass(f); ++f->power_claims;
    if (ok && f->calls == f->zero_token_at) return true;
    if (ok || f->retained) { assert(!f->power_live); f->power_live = true;
        *token = f->power_token = ++f->serial; }
    return ok;
}
static bool read_power(void *ctx, uint64_t token, const uint8_t *w, size_t wn,
                       uint8_t *r, size_t rn, uint32_t timeout) {
    fake *f = ctx; assert(f->power_live && token == f->power_token);
    /* Every power operation must remain a bounded, read-only transaction. */
    assert(w && wn == 1 && r && rn == 1 && timeout == 20);
    assert(*w == 3 || *w == 0x80); ++f->power_reads;
    if (!pass(f)) return false;
    *r = *w == 3 ? (f->bad_id ? 0 : 0x4a) : (f->supply_off ? 0xfe : 0xff);
    return true;
}
static bool release_power(void *ctx, uint64_t token) {
    fake *f = ctx; assert(f->power_live && token == f->power_token);
    if (!pass(f) || f->fail_releases) return false;
    f->power_live = false; return true;
}
static bool claim_reset(void *ctx, uint8_t pin, bool initial, uint64_t *token) {
    fake *f = ctx; assert(!f->power_live); assert(pin < 49 && !f->pins[pin]);
    *token = 0; bool ok = pass(f); ++f->reset_claims;
    if (ok && f->calls == f->zero_token_at) return true;
    if (ok || f->retained) {
        f->pins[pin] = true; f->levels[pin] = initial;
        *token = f->pin_tokens[pin] = ++f->serial;
    }
    return ok;
}
static unsigned pin_for(fake *f, uint64_t token) {
    for (unsigned i=0; i<49; ++i) if (f->pins[i] && f->pin_tokens[i] == token) return i;
    assert(!"stale or unknown token"); return 0;
}
static bool write_reset(void *ctx, uint64_t token, bool level) {
    fake *f = ctx; unsigned pin = pin_for(f, token);
    if (!pass(f)) return false;
    f->levels[pin] = level; return true;
}
static bool read_reset(void *ctx, uint64_t token, bool *level) {
    fake *f = ctx; unsigned pin = pin_for(f, token);
    if (!pass(f)) return false;
    *level = f->levels[pin] ^ f->mismatch; return true;
}
static bool release_reset(void *ctx, uint64_t token) {
    fake *f = ctx; unsigned pin = pin_for(f, token);
    if (!pass(f) || f->fail_releases) return false;
    f->pins[pin] = false; return true;
}
static bool delay_ms(void *ctx, uint32_t ms) {
    fake *f = ctx; ++f->delays;
    if (!pass(f)) return false;
    f->elapsed += ms; return true;
}
static risc_i2c_bus_api_v1 bus(fake *f) {
    return (risc_i2c_bus_api_v1){1,sizeof(risc_i2c_bus_api_v1),f,claim_power,read_power,release_power};
}
static risc_board_prepare_io io(fake *f) {
    return (risc_board_prepare_io){f,claim_reset,write_reset,read_reset,release_reset,delay_ms};
}
static void no_owners(fake *f) {
    assert(!f->power_live);
    for (unsigned i=0;i<49;++i) assert(!f->pins[i]);
}
static void successful(void) {
    fake f = {0}; risc_i2c_bus_api_v1 b=bus(&f); risc_board_prepare_io g=io(&f);
    risc_board_prepare_state s={0};
    assert(risc_board_prepare(&s,&board_prepare_config,101,&b,&g));
    assert(s.ready && !f.power_live && f.pins[1] && f.pins[2]);
    assert(f.levels[1] && f.levels[2] && f.elapsed == 220);
    assert(f.power_reads==2 && f.power_claims==1 && f.reset_claims==2 && f.delays==4);
    unsigned calls=f.calls;
    assert(!risc_board_prepare(&s,&board_prepare_config,101,&b,&g));
    assert(f.calls==calls && s.ready); /* No duplicate reset of live consumer. */
    assert(risc_board_prepare_finish(&s)); no_owners(&f);
    assert(!s.bus && !s.io && !s.ready);
    assert(risc_board_prepare_finish(&s));
    assert(risc_board_prepare(&s,&board_prepare_config,101,&b,&g));
    assert(risc_board_prepare_finish(&s)); no_owners(&f);
}
static void failures(void) {
    /* Four power calls and six calls per reset: exhaust every startup fault. */
    for (unsigned retained=0;retained<2;++retained) {
        for (unsigned failure=1;failure<=16;++failure) {
            fake f={.fail_at=failure,.retained=retained};
            risc_i2c_bus_api_v1 b=bus(&f); risc_board_prepare_io g=io(&f);
            risc_board_prepare_state s={0};
            assert(!risc_board_prepare(&s,&board_prepare_config,101,&b,&g));
            assert(!s.ready && f.calls==failure);
            unsigned calls=f.calls;
            assert(!risc_board_prepare(&s,&board_prepare_config,101,&b,&g));
            assert(f.calls==calls);
            /* Make releases fail once. All live custody and API references stay. */
            bool owners=f.power_live || f.pins[1] || f.pins[2];
            f.fail_at=0; f.fail_releases=true;
            bool ended=risc_board_prepare_finish(&s);
            assert(ended==!owners);
            if (owners) assert(s.bus==&b && s.io==&g);
            f.fail_releases=false;
            assert(risc_board_prepare_finish(&s)); no_owners(&f);
            assert(risc_board_prepare(&s,&board_prepare_config,101,&b,&g));
            assert(risc_board_prepare_finish(&s)); no_owners(&f);
        }
    }
}
static void rejected(void) {
    for (unsigned mode=0;mode<3;++mode) {
        fake f={.bad_id=mode==0,.supply_off=mode==1,.mismatch=mode==2};
        risc_i2c_bus_api_v1 b=bus(&f); risc_board_prepare_io g=io(&f);
        risc_board_prepare_state s={0};
        assert(!risc_board_prepare(&s,&board_prepare_config,101,&b,&g));
        if (mode<2) assert(f.reset_claims==0); /* Power failed: no GPIO disturbance. */
        assert(risc_board_prepare_finish(&s)); no_owners(&f);
    }
    for (unsigned mode=0;mode<11;++mode) {
        fake f={0}; risc_i2c_bus_api_v1 b=bus(&f); risc_board_prepare_io g=io(&f);
        risc_board_prepare_state s={0}; risc_board_prepare_config c=board_prepare_config;
        if(mode==0)c.bus_instance_id=999;
        if(mode==1)c.reset_count=RISC_BOARD_RESET_MAX+1;
        if(mode==2)c.resets[1].pin=c.resets[0].pin;
        if(mode==3)c.resets[0].pin=49;
        if(mode==4)c.resets[0].assert_ms=0;
        if(mode==5)c.resets[0].recovery_ms=501;
        if(mode==6)c.enable_mask=0;
        if(mode==7)c.power_address=0x78;
        if(mode==8)c.identity_value=0xff,c.identity_mask=1;
        if(mode==9)b.api_version=2;
        if(mode==10)g.read_reset=NULL;
        assert(!risc_board_prepare(&s,&c,101,&b,&g)); assert(!f.calls);
        assert(risc_board_prepare_finish(&s)); no_owners(&f);
    }
}
static void zero_and_mixed_cleanup(void) {
    const unsigned claim_calls[]={1,5,11};
    for (size_t i=0;i<3;++i) {
        fake f={.zero_token_at=claim_calls[i]};
        risc_i2c_bus_api_v1 b=bus(&f); risc_board_prepare_io g=io(&f);
        risc_board_prepare_state s={0};
        assert(!risc_board_prepare(&s,&board_prepare_config,101,&b,&g));
        assert(!s.ready && f.calls==claim_calls[i]);
        assert(risc_board_prepare_finish(&s)); no_owners(&f);
    }
    for (unsigned release_failure=17;release_failure<=18;++release_failure) {
        fake f={.fail_at=release_failure};
        risc_i2c_bus_api_v1 b=bus(&f); risc_board_prepare_io g=io(&f);
        risc_board_prepare_state s={0};
        assert(risc_board_prepare(&s,&board_prepare_config,101,&b,&g));
        assert(!risc_board_prepare_finish(&s));
        assert(f.calls==18 && f.pins[1]!=f.pins[2]);
        assert((s.reset_claims[0]!=0)==f.pins[1]);
        assert((s.reset_claims[1]!=0)==f.pins[2]);
        assert(s.bus==&b && s.io==&g && !s.ready);
        assert(!risc_board_prepare(&s,&board_prepare_config,101,&b,&g));
        assert(f.calls==18);
        assert(risc_board_prepare_finish(&s)); no_owners(&f);
        assert(f.calls==19); /* Only the still-owned token was retried. */
    }
}
static void generalized(void) {
    /* Data-driven reset polarity, pin selection and timing, not a two-pin hack. */
    fake f={0}; risc_i2c_bus_api_v1 b=bus(&f); risc_board_prepare_io g=io(&f);
    risc_board_prepare_state s={0}; risc_board_prepare_config c=board_prepare_config;
    c.resets[0]=(risc_board_reset){17,true,7,8};
    c.resets[1]=(risc_board_reset){18,false,9,10};
    c.resets[2]=(risc_board_reset){21,true,11,12}; c.reset_count=3;
    assert(risc_board_prepare(&s,&c,101,&b,&g));
    assert(f.elapsed==57 && !f.levels[17] && f.levels[18] && !f.levels[21]);
    assert(risc_board_prepare_finish(&s)); no_owners(&f);
}
int main(void) {
    successful(); failures(); rejected(); zero_and_mixed_cleanup(); generalized();
    puts("Board preparation: startup, 32 fault paths, retained cleanup, invalid inputs and generalized reset controls passed");
}

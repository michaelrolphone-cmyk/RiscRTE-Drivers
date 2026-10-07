/* Receive-only ESP32-S3 IQ burst provider for radio.iq@1.
 *
 * The whole path lives in this ELF. The catalog app acquires the capability
 * and copies pairs. It does not call modem, regi2c, or PHY symbols.
 * This file does not import those symbols either. ROM entry points are
 * absolute addresses from the published ESP32-S3 ROM map and are called
 * only from here.
 *
 * Sequence and radio registers:
 *   h0m3us3r/eSpDR f279bf823eee41796dfd1ac21f13e1ed9b418c82 (0BSD)
 *   esp32s3/src/board.h
 *   esp32s3/src/radio.c power_up_modem, tune_pll, configure_receiver,
 *     radio_init defaults (2440 MHz, 80 Msps, width 40, gain selector 24,
 *     automatic RF/BB/DC/IQ). TX groups are written 0.
 *   esp32s3/src/capture.c start window: DUMP_CTRL, bank bit 0, RUN, then stop.
 *   The FPGA GPIO stream (transmit.S / lane loops) is not included.
 *   esp32s3/src/lo_plan.h (copied beside this file).
 *
 * Clock and power registers:
 *   espressif/esp-idf v5.1.4
 *   components/soc/esp32s3/include/soc/rtc_cntl_reg.h
 *   components/soc/esp32s3/include/soc/syscon_reg.h
 *   components/hal/esp32s3/include/hal/clk_gate_ll.h
 *   Bases DR_REG_RTCCNTL_BASE 0x60008000 and DR_REG_SYSCON_BASE 0x60026000
 *   from components/soc/esp32s3/include/soc/soc.h (v4.4.7; same in v5.1).
 *
 * ROM map, espressif/esp-idf v5.4.1
 *   components/esp_rom/esp32s3/ld/esp32s3.rom.ld
 *   components/esp_rom/esp32s3/ld/esp32s3.rom.api.ld
 *   esp_rom_regi2c_read  = rom_i2c_readReg  = 0x40005d48
 *   esp_rom_regi2c_write = rom_i2c_writeReg = 0x40005d60
 *   rom_pbus_rd = 0x40005df0
 *   ets_delay_us = 0x40000600
 * Prototypes: components/esp_rom/include/esp_rom_regi2c.h and
 *   components/esp_rom/include/esp32s3/rom/ets_sys.h.
 * rom_pbus_rd(block, index) is the declaration in eSpDR radio.c.
 *
 * Vendor PHY calibration is not linked and is not called.
 */
#include "RiscRadioIqV1.h"
#include "lo_plan.h"
#include "RiscRadioIqResourceV1.h"
#include <string.h>

#ifdef RISC_IQ_HOST_TEST
#include "s3_radio_iq_mock.h"
#define REG(address) (*iq_test_register(address))
#else
#define REG(address) (*(volatile uint32_t *)(uintptr_t)(address))
#endif

/* eSpDR board.h */
#define CAPTURE_BANK_BASE 0x3FCB0000u
#define CAPTURE_BANK_BYTES 0x10000u
#define DUMP_CTRL_REG 0x60033D5Cu
#define DUMP_WRITE_INDEX_REG 0x60033D60u
#define DUMP_CONFIG_REG 0x60033D90u
#define DUMP_BANK_SELECT_REG 0x600C101Cu
#define DUMP_CTRL_RUN 0x80000000u
#define DUMP_CTRL_CIRCULAR 0x00024000u
#define DUMP_CONFIG_IQ 0x000C2040u
#define BB_ENABLE_REG 0x6002600Cu
#define AGC_CTRL_REG 0x6001C01Cu
#define AGC_GAIN_FORCE_REG 0x6001C02Cu
#define AGC_DISABLE_REG 0x6001C034u
#define AGC_RX_FORCE_REG 0x6001C080u
#define IQ_CORRECTION_REG 0x6000607Cu
#define FE_WIDTH_REG 0x60006100u
#define PBUS_CTRL_REG 0x60006104u
#define PBUS_MODE_REG 0x6000610Cu
#define PBUS_STATUS_REG 0x60006110u
#define PBUS_BB_GAIN_REG 0x60006118u
#define PBUS_RF_GAIN_REG 0x6000611Cu
#define RFPLL_OWNER_REG 0x6000E0C4u
#define I2C_RFPLL 0x62
#define I2C_SDM 0x63
#define I2C_BB_FILTER 0x67

/* One 64 KiB bank holds 16384 pairs (board.h bank size and circular comment). */
#define RING_PAIRS (CAPTURE_BANK_BYTES / 4u)
#define RING_MASK (RING_PAIRS - 1u)

/* ESP-IDF rtc_cntl_reg.h / syscon_reg.h / clk_gate_ll.h */
#define DR_REG_RTCCNTL_BASE 0x60008000u
#define DR_REG_SYSCON_BASE 0x60026000u
#define RTC_CNTL_DIG_PWC_REG (DR_REG_RTCCNTL_BASE + 0x90u)
#define RTC_CNTL_WIFI_FORCE_PD (1u << 17)
#define RTC_CNTL_DIG_ISO_REG (DR_REG_RTCCNTL_BASE + 0x94u)
#define RTC_CNTL_WIFI_FORCE_ISO (1u << 28)
#define SYSTEM_WIFI_CLK_EN_REG (DR_REG_SYSCON_BASE + 0x14u)
#define SYSTEM_WIFI_RST_EN_REG (DR_REG_SYSCON_BASE + 0x18u)
#define SYSTEM_WIFI_CLK_WIFI_BT_COMMON_M 0x78078Fu
#define SYSTEM_WIFI_CLK_RNG_EN (1u << 15)
#define SYSTEM_WIFIBB_RST (1u << 0)
#define SYSTEM_FE_RST (1u << 1)
#define SYSTEM_WIFIMAC_RST (1u << 2)
#define SYSTEM_BTBB_RST (1u << 3)
#define SYSTEM_BTMAC_RST (1u << 4)
#define SYSTEM_RW_BTMAC_RST (1u << 9)
#define SYSTEM_RW_BTMAC_REG_RST (1u << 11)
#define SYSTEM_BTBB_REG_RST (1u << 13)
#define MODEM_RESET_FIELD_WHEN_PU                                          \
    (SYSTEM_WIFIBB_RST | SYSTEM_FE_RST | SYSTEM_WIFIMAC_RST |              \
     SYSTEM_BTBB_RST | SYSTEM_BTMAC_RST | SYSTEM_RW_BTMAC_RST |            \
     SYSTEM_RW_BTMAC_REG_RST | SYSTEM_BTBB_REG_RST)
/* eSpDR radio.c: the dump engine needs Wi-Fi MAC clock bit 6, which the
 * published SYSTEM_WIFI_CLK_WIFI_EN mask leaves at 0. */
#define WIFI_MAC_CLK_BIT6 (1u << 6)

#define PBUS_TIMEOUT_CYCLES 24000u
#define RADIO_LO_HZ 2440000000u
#define RADIO_GAIN 24u

#ifndef RISC_IQ_HOST_TEST
#define ROM_I2C_READ ((uint8_t (*)(uint8_t, uint8_t, uint8_t))0x40005d48u)
#define ROM_I2C_WRITE ((void (*)(uint8_t, uint8_t, uint8_t, uint8_t))0x40005d60u)
#define ROM_PBUS_RD ((unsigned (*)(unsigned, unsigned))0x40005df0u)
static void rom_delay_us(uint32_t us) {
    void (*volatile fn)(uint32_t) = (void (*)(uint32_t))0x40000600u;
    fn(us);
}
#define ROM_DELAY_US rom_delay_us
#endif

static const uint8_t dc_block[4] = {3, 3, 2, 2};
static const uint8_t dc_index[4] = {1, 2, 1, 2};

static bool running;
static risc_radio_iq_diagnostics_v1 diagnostic_state;
static const risc_radio_iq_resource_v1 *resource;
static uint64_t lease;
static bool restored;
/* Every changed digital register is restored; power/clock controls are last.
 * Dump RUN/bank bits must be clear on admission and are never restored live. */
static const uint32_t saved_addresses[] = {
    DUMP_CTRL_REG, DUMP_CONFIG_REG, DUMP_BANK_SELECT_REG,
    BB_ENABLE_REG, AGC_CTRL_REG, AGC_GAIN_FORCE_REG, AGC_DISABLE_REG,
    AGC_RX_FORCE_REG, IQ_CORRECTION_REG, FE_WIDTH_REG, PBUS_CTRL_REG,
    PBUS_MODE_REG, PBUS_STATUS_REG, RFPLL_OWNER_REG,
    SYSTEM_WIFI_RST_EN_REG, RTC_CNTL_DIG_ISO_REG,
    SYSTEM_WIFI_CLK_EN_REG, RTC_CNTL_DIG_PWC_REG
};
static uint32_t saved_registers[sizeof(saved_addresses)/sizeof(saved_addresses[0])];
static const uint8_t analog_addresses[][2] = {
    {I2C_RFPLL,0},{I2C_RFPLL,1},{I2C_RFPLL,2},{I2C_RFPLL,11},
    {I2C_SDM,0},{I2C_SDM,3},{I2C_SDM,4},{I2C_SDM,5},
    {I2C_BB_FILTER,6},{I2C_BB_FILTER,7},{ESP32S3_CKGEN_BLOCK,ESP32S3_CKGEN_REG}
};
static uint8_t saved_analog[sizeof(analog_addresses)/sizeof(analog_addresses[0])];
static bool analog_saved;
static void barrier(void) {
#ifndef RISC_IQ_HOST_TEST
    __asm__ volatile("memw" ::: "memory");
#endif
}

static uint32_t cpu_cycles(void) {
#ifdef RISC_IQ_HOST_TEST
    return iq_test_cycles();
#else
    uint32_t cycles;
    __asm__ volatile("rsr.ccount %0" : "=a"(cycles));
    return cycles;
#endif
}

static uint8_t analog_read(uint8_t block, uint8_t reg) {
    return ROM_I2C_READ(block, 1, reg);
}

static void analog_write(uint8_t block, uint8_t reg, uint8_t value) {
    ROM_I2C_WRITE(block, 1, reg, value);
}

static void analog_write_bits(uint8_t block, uint8_t reg, uint8_t mask, uint8_t value) {
    uint8_t old = analog_read(block, reg);
    analog_write(block, reg, (uint8_t)((old & (uint8_t)~mask) | (value & mask)));
}

/* The Runtime-owned platform.radio.iq.resource lease now enters with the
 * ESP32-S3 PHY calibrated and enabled. This ELF must not reset the modem after
 * that calibration; it owns only the tuning/capture state below. */

static void set_pll_capacitor(unsigned cap) {
    analog_write(I2C_RFPLL, 1, (uint8_t)cap);
    analog_write_bits(I2C_RFPLL, 2, 0x10, (uint8_t)((cap >> 8) << 4));
}

static void set_pll_manual_capacitor(bool manual) {
    analog_write_bits(I2C_RFPLL, 11, 0x40, manual ? 0x40 : 0);
}

static bool tune_pll(void) {
    struct esp32s3_lo_plan plan;
    if (!esp32s3_plan_lo(RADIO_LO_HZ, ESP32S3_LO_AUTO, &plan)) return false;
    analog_write_bits(ESP32S3_CKGEN_BLOCK, ESP32S3_CKGEN_REG, ESP32S3_CKGEN_5_6_BIT, 0);
    REG(RFPLL_OWNER_REG) |= 1u << 25;
    set_pll_manual_capacitor(false);
    analog_write(I2C_SDM, 0, 0x07);
    analog_write(I2C_SDM, 3, (uint8_t)(plan.sdm_word >> 16));
    analog_write(I2C_SDM, 4, (uint8_t)(plan.sdm_word >> 8));
    analog_write(I2C_SDM, 5, (uint8_t)plan.sdm_word);
    analog_write(I2C_SDM, 0, 0x17);
    analog_write_bits(I2C_RFPLL, 0, 0x40, 0x40);
    analog_write_bits(I2C_RFPLL, 0, 0x20, 0x00);
    analog_write_bits(I2C_RFPLL, 0, 0x20, 0x20);
    analog_write_bits(I2C_RFPLL, 0, 0x40, 0x00);
    bool calibrated = false;
    for (unsigned poll = 0; poll < 100 && !calibrated; poll++) {
        ROM_DELAY_US(20);
        calibrated = (analog_read(I2C_RFPLL, 7) & 2) != 0;
    }
    if (!calibrated) return false;
    ROM_DELAY_US(5);
    uint8_t saved_low = analog_read(I2C_RFPLL, 1);
    uint8_t saved_high = analog_read(I2C_RFPLL, 2);
    uint8_t saved_mode = analog_read(I2C_RFPLL, 11);
    set_pll_manual_capacitor(true);
    unsigned run_start = 0, run_length = 0, best_start = 0, best_length = 0;
    for (unsigned cap = 0; cap < 512; cap++) {
        set_pll_capacitor(cap);
        ROM_DELAY_US(20);
        bool locked = ((analog_read(I2C_RFPLL, 12) >> 2) & 3) == 0;
        if (!locked) {
            run_length = 0;
            continue;
        }
        if (run_length++ == 0) run_start = cap;
        if (run_length > best_length) {
            best_start = run_start;
            best_length = run_length;
        }
    }
    analog_write(I2C_RFPLL, 1, saved_low);
    analog_write_bits(I2C_RFPLL, 2, 0x10, saved_high);
    analog_write_bits(I2C_RFPLL, 11, 0x40, saved_mode);
    if (!best_length) return false;
    set_pll_capacitor(best_start + (best_length - 1) / 2);
    set_pll_manual_capacitor(true);
    uint8_t mode = plan.mode == ESP32S3_LO_5_6 ? ESP32S3_CKGEN_5_6_BIT : 0;
    analog_write_bits(ESP32S3_CKGEN_BLOCK, ESP32S3_CKGEN_REG, ESP32S3_CKGEN_5_6_BIT, mode);
    return true;
}

static bool pbus_write(unsigned block, unsigned index, unsigned value) {
    uint32_t fields = ((value & 511u) << 6) | ((block & 15u) << 2) | ((index & 3u) << 15);
    REG(PBUS_CTRL_REG) = (REG(PBUS_CTRL_REG) & 0xFFFE0001u) | (fields & 0x1FFFCu) | 2u;
    uint32_t start = cpu_cycles();
    while (REG(PBUS_STATUS_REG) & 0x80000000u) {
        if (cpu_cycles() - start > PBUS_TIMEOUT_CYCLES) {
            REG(PBUS_CTRL_REG) &= ~2u;
            return false;
        }
    }
    REG(PBUS_CTRL_REG) &= ~2u;
    return true;
}

static void park_receiver(void) {
    REG(DUMP_CTRL_REG) &= ~DUMP_CTRL_RUN;
    REG(DUMP_BANK_SELECT_REG) &= ~15u;
    REG(AGC_RX_FORCE_REG) &= ~0xC1u;
    REG(PBUS_STATUS_REG) &= ~0xCF00u;
    REG(BB_ENABLE_REG) &= ~2u;
}

static bool release_receiver(void) {
    if (!lease) return true;
    if (!restored) {
        park_receiver();
        barrier();
        ROM_DELAY_US(1); /* settle the stopped ADC pipeline before releasing SRAM */
        if ((REG(DUMP_CTRL_REG) & DUMP_CTRL_RUN) || (REG(DUMP_BANK_SELECT_REG) & 15u)) return false;
        bool ok = true;
        /* Do not short-circuit: both transmit groups are always explicitly off. */
        ok = pbus_write(4, 1, 0) && ok;
        ok = pbus_write(5, 1, 0) && ok;
        ok = pbus_write(0, 1, 0) && ok;
        ok = pbus_write(1, 1, 0) && ok;
        ok = pbus_write(1, 2, 0) && ok;
        if (!ok) return false;
        if (analog_saved)
            for (unsigned i = 0; i < sizeof(saved_analog); ++i)
                analog_write(analog_addresses[i][0], analog_addresses[i][1], saved_analog[i]);
        for (unsigned i = 0; i < sizeof(saved_registers)/sizeof(saved_registers[0]); ++i)
            REG(saved_addresses[i]) = saved_registers[i];
        barrier();
        restored = true;
    }
    if (!resource->release(resource->context, lease)) return false;
    lease = 0;
    restored = analog_saved = false;
    return true;
}

static int configure_receiver(void) {
    park_receiver();
    REG(FE_WIDTH_REG) = (REG(FE_WIDTH_REG) & ~0x003F0000u) | 0x00120000u;
    REG(BB_ENABLE_REG) = (REG(BB_ENABLE_REG) & ~0xCu) | 0x4u;
    REG(BB_ENABLE_REG) |= 0x10000000u;
    REG(BB_ENABLE_REG) &= ~2u;
    ROM_DELAY_US(1);
    REG(BB_ENABLE_REG) |= 2u;
    REG(AGC_CTRL_REG) = (REG(AGC_CTRL_REG) & 0xFF00FFFFu) | 0x007F0000u;
    REG(AGC_DISABLE_REG) |= 0x80u;
    REG(AGC_RX_FORCE_REG) |= 1u;
    REG(AGC_GAIN_FORCE_REG) = (REG(AGC_GAIN_FORCE_REG) & 0x007FFFFFu) | (RADIO_GAIN << 24) | 0x00800000u;
    REG(PBUS_STATUS_REG) |= 0xC000u;
    analog_write(I2C_BB_FILTER, 6, 0);
    analog_write(I2C_BB_FILTER, 7, 0);
    ROM_DELAY_US(100);
    unsigned bb = (REG(PBUS_BB_GAIN_REG) >> 9) & 511u;
    unsigned rf = (REG(PBUS_RF_GAIN_REG) >> 18) & 511u;
    REG(PBUS_MODE_REG) &= ~0x08000000u;
    REG(PBUS_CTRL_REG) |= 1u;
    REG(BB_ENABLE_REG) &= ~2u;
    /* Receive only. Both TX groups stay off. */
    bool tx4_off = pbus_write(4, 1, 0);
    bool tx5_off = pbus_write(5, 1, 0);
    bool ok = tx4_off && tx5_off && pbus_write(0, 1, 0x184) &&
              pbus_write(1, 1, 0x189) && pbus_write(1, 2, rf) && pbus_write(0, 1, bb);
    for (unsigned r = 0; r < 4 && ok; r++)
        (void)(ROM_PBUS_RD(dc_block[r], dc_index[r]) & 511u);
    if (!ok) return RISC_RADIO_IQ_PBUS_FAILED;
    REG(DUMP_CONFIG_REG) = DUMP_CONFIG_IQ;
    ROM_DELAY_US(100);
    REG(DUMP_CTRL_REG) = DUMP_CTRL_CIRCULAR;
    /* Keep a strict CPU-owned initialization window: no MAC bank selection
     * until sentinel initialization is complete. */
    REG(DUMP_BANK_SELECT_REG) &= ~15u;
    uint8_t mode = 0; /* 2440 MHz plans as normal conversion. */
    analog_write_bits(ESP32S3_CKGEN_BLOCK, ESP32S3_CKGEN_REG, ESP32S3_CKGEN_5_6_BIT, mode);
    ROM_DELAY_US(3000);
    if ((analog_read(ESP32S3_CKGEN_BLOCK, ESP32S3_CKGEN_REG) & ESP32S3_CKGEN_5_6_BIT) != mode)
        return RISC_RADIO_IQ_PLL_FAILED;
    return RISC_RADIO_IQ_OK;
}

static void trace_stage(risc_radio_iq_trace_v1 trace,void *context,const char *stage) {
    if (trace) (void)trace(context,stage);
}
static void trace_registers(risc_radio_iq_trace_v1 trace,void *context) {
    if (!trace) return;
    char line[]="regs clk=00000000 rst=00000000 pd=00000000 iso=00000000";
    const unsigned offsets[]={9,22,34,47};
    const uint32_t addresses[]={SYSTEM_WIFI_CLK_EN_REG,SYSTEM_WIFI_RST_EN_REG,RTC_CNTL_DIG_PWC_REG,RTC_CNTL_DIG_ISO_REG};
    for(unsigned field=0;field<4;++field){
        uint32_t value=0;
        for(unsigned i=0;i<sizeof(saved_addresses)/sizeof(saved_addresses[0]);++i)
            if(saved_addresses[i]==addresses[field])value=saved_registers[i];
        for(unsigned i=0;i<8;++i)line[offsets[field]+i]="0123456789abcdef"[(value>>(28-4*i))&15u];
    }
    trace_stage(trace,context,line);
}
static int bring_up(risc_radio_iq_trace_v1 trace,void *trace_context) {
    trace_stage(trace,trace_context,"register-snapshot");
    for (unsigned i = 0; i < sizeof(saved_registers)/sizeof(saved_registers[0]); ++i)
        saved_registers[i] = REG(saved_addresses[i]);
    for (unsigned i = 0; i < sizeof(saved_analog); ++i)
        saved_analog[i] = analog_read(analog_addresses[i][0], analog_addresses[i][1]);
    analog_saved = true;
    trace_registers(trace,trace_context);
    /* IDF PHY calibration does not enable the dump engine's MAC clock bit.
     * Preserve calibrated modem state and restore this one owned bit on exit. */
    trace_stage(trace,trace_context,"dump-clock-enable");
    REG(SYSTEM_WIFI_CLK_EN_REG) |= WIFI_MAC_CLK_BIT6;
    barrier();
    diagnostic_state.clock_mask = REG(SYSTEM_WIFI_CLK_EN_REG);
    trace_stage(trace,trace_context,"pll-tune");
    diagnostic_state.stage = RISC_RADIO_IQ_STAGE_PLL;
    if (!tune_pll()) return RISC_RADIO_IQ_PLL_FAILED;
    trace_stage(trace,trace_context,"receiver-configure");
    diagnostic_state.stage = RISC_RADIO_IQ_STAGE_RECEIVER;
    return configure_receiver();
}

static int copy_burst(uint32_t *pairs,uint32_t count,risc_radio_iq_trace_v1 trace,void *trace_context) {
#ifdef RISC_IQ_HOST_TEST
    volatile uint32_t *bank = iq_test_bank();
#else
    volatile uint32_t *bank = (volatile uint32_t *)(uintptr_t)resource->bank_base;
#endif
    diagnostic_state.stage = RISC_RADIO_IQ_STAGE_DUMP;
    diagnostic_state.dump_before = REG(DUMP_WRITE_INDEX_REG) & RING_MASK;
    trace_stage(trace,trace_context,"bank-initialize");
    /* A dump can reset its index and may wrap while the task is preempted.
     * Sentinel initialization and the final stopped index select only samples
     * written by this invocation, never the stale pre-start cursor. */
    const uint32_t sentinel = 0xa5c33c5au; /* impossible in the 20-bit pair layout */
    for (unsigned i = 0; i < RING_PAIRS; ++i) bank[i] = sentinel;
    barrier();
    trace_stage(trace,trace_context,"dump-start");
    REG(DUMP_CTRL_REG) = DUMP_CTRL_CIRCULAR;
    REG(DUMP_BANK_SELECT_REG) = (REG(DUMP_BANK_SELECT_REG) & ~15u) | 1u;
    barrier();
    REG(DUMP_CTRL_REG) = DUMP_CTRL_CIRCULAR | DUMP_CTRL_RUN;
    uint32_t began = cpu_cycles();
    bool ready = false, observed_progress = false;
    uint32_t previous = REG(DUMP_WRITE_INDEX_REG) & RING_MASK, produced = 0;
    /* The ADC owns the selected SRAM bank while RUN is set. Never read that
     * bank to test progress: a CPU access can stall on the dump-owned bank.
     * Like upstream capture.c, observe only the MMIO index until stopped.
     * Discard the first transition because RUN may reset a stale cursor. */
    for (;;) {
        uint32_t end = REG(DUMP_WRITE_INDEX_REG) & RING_MASK;
        if (end != previous) {
            if (observed_progress) produced += (end - previous) & RING_MASK;
            observed_progress = true;
            previous = end;
            if (produced >= count) { ready = true; break; }
        }
        if (cpu_cycles() - began > PBUS_TIMEOUT_CYCLES * 100u) break;
    }
    diagnostic_state.elapsed_cycles = cpu_cycles() - began;
    diagnostic_state.dump_ready = ready;
    REG(DUMP_CTRL_REG) = DUMP_CTRL_CIRCULAR;
    barrier();
    REG(DUMP_BANK_SELECT_REG) &= ~15u;
    ROM_DELAY_US(1);
    barrier();
    trace_stage(trace,trace_context,"dump-stopped");
    diagnostic_state.dump_after = REG(DUMP_WRITE_INDEX_REG) & RING_MASK;
    if (!ready) return RISC_RADIO_IQ_DUMP_TIMEOUT;
    trace_stage(trace,trace_context,"bank-copy");
    diagnostic_state.stage = RISC_RADIO_IQ_STAGE_COPY;
    uint32_t at = (REG(DUMP_WRITE_INDEX_REG) - count) & RING_MASK;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t sample = bank[(at + i) & RING_MASK];
        if (sample == sentinel) return RISC_RADIO_IQ_DUMP_TIMEOUT;
        pairs[i] = sample & 0x000fffffu;
    }
    return RISC_RADIO_IQ_OK;
}

static bool suspend_receiver(void *context) {
    (void)context;
    return release_receiver();
}

static int capture_result(int result) {
    diagnostic_state.result = result;
    return result;
}
static int capture_burst_traced(void *context,uint32_t *pairs,uint32_t count,
                                risc_radio_iq_trace_v1 trace,void *trace_context) {
    (void)context;
    diagnostic_state = (risc_radio_iq_diagnostics_v1){.struct_size=sizeof(diagnostic_state),
        .stage=RISC_RADIO_IQ_STAGE_IDLE,.requested_pairs=count};
    if (!pairs || count == 0 || count > RISC_RADIO_IQ_PAIRS) return capture_result(RISC_RADIO_IQ_BAD_ARGUMENT);
    if (!running) return capture_result(RISC_RADIO_IQ_NOT_RUNNING);
    diagnostic_state.stage = RISC_RADIO_IQ_STAGE_CLEANUP;
    if (lease && !release_receiver()) return capture_result(RISC_RADIO_IQ_CLEANUP_RETAINED);
    diagnostic_state.stage = RISC_RADIO_IQ_STAGE_CLAIM;
    trace_stage(trace,trace_context,"native-claim");
    if (!resource->claim(resource->context, &lease)) {
        /* Failed admission cannot authorize any modem/ROM/SRAM access. */
        if (lease) { restored = true; return capture_result(RISC_RADIO_IQ_CLEANUP_RETAINED); }
        return capture_result(RISC_RADIO_IQ_BUSY);
    }
    if (!lease) return capture_result(RISC_RADIO_IQ_BUSY);
    trace_stage(trace,trace_context,"native-ready");
    int status = bring_up(trace,trace_context);
    if (status == RISC_RADIO_IQ_OK) status = copy_burst(pairs,count,trace,trace_context);
    trace_stage(trace,trace_context,"cleanup-begin");
    diagnostic_state.cleanup_ok = release_receiver();
    trace_stage(trace,trace_context,diagnostic_state.cleanup_ok?"cleanup-complete":"cleanup-retained");
    if (!diagnostic_state.cleanup_ok) return capture_result(RISC_RADIO_IQ_CLEANUP_RETAINED);
    if (status == RISC_RADIO_IQ_OK) diagnostic_state.stage = RISC_RADIO_IQ_STAGE_COMPLETE;
    return capture_result(status);
}
static int capture_burst(void *context,uint32_t *pairs,uint32_t count) {
    return capture_burst_traced(context,pairs,count,NULL,NULL);
}
static bool diagnostics(void *context, risc_radio_iq_diagnostics_v1 *out) {
    (void)context;
    if (!out || out->struct_size < sizeof(*out)) return false;
    *out = diagnostic_state;
    return true;
}

static bool start(const risc_provider_dependency_v1 *deps, size_t count) {
    if (running || lease || !deps || count != 1 || !deps[0].capability_id ||
        strcmp(deps[0].capability_id,RISC_RADIO_IQ_RESOURCE_CAPABILITY) ||
        deps[0].api_version != 1 || !deps[0].api) return false;
    const risc_radio_iq_resource_v1 *candidate = deps[0].api;
    if (candidate->api_version != 1 || candidate->struct_size < sizeof(*candidate) ||
        !candidate->claim || !candidate->release || candidate->bank_base != CAPTURE_BANK_BASE ||
        candidate->bank_bytes != CAPTURE_BANK_BYTES) return false;
    /* The opt-in CPU resource owns SoC/ROM/SRAM admission. There are no board
     * pins or external radio resources to map, and no raw authority for apps. */
    resource = candidate;
    running = true;
    return true;
}

static void stop(void) {
    if (!release_receiver()) return;
    running = false;
    resource = NULL;
}

static bool quiesce(void) {
    if (!release_receiver()) return false;
    running = false;
    return true;
}

static const risc_radio_iq_diagnostics_api_v1 api = {
    {RISC_RADIO_IQ_API_V1, sizeof(risc_radio_iq_diagnostics_api_v1), 0, capture_burst, suspend_receiver}, diagnostics, capture_burst_traced
};

static const risc_driver_v2 driver = {
    RISC_PROVIDER_DRIVER_ABI_V2, sizeof(risc_driver_v2),
    "s3-radio-iq-v1", "radio.iq", RISC_RADIO_IQ_API_V1,
    &api.base, start, stop, quiesce
};

__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &driver : 0;
}

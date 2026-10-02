/* X4 Pro 800x480 panel. Default controller is the hardware-confirmed SSD1677
 * sequence recovered by CrossPoint/FreeInk from app1. UC8179/UC8279 share the
 * pinout but their probe bytes are not copied here; an inconclusive probe
 * stays on SSD1677. Not executed on device in this tree. */
#include "RiscDisplayOutputV1.h"
#include "RiscGpioBankV1.h"
#include "RiscPlatformClockV1.h"
#include "x4pro_pins.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define FRAME_BYTES ((X4PRO_PANEL_WIDTH / 8u) * X4PRO_PANEL_HEIGHT)
static const risc_gpio_bank_api_v1 *gpio_api;
static const risc_platform_clock_api_v1 *clock_api;
static uint64_t cs, dc, rst, sclk, mosi, busy;
static uint8_t frame[FRAME_BYTES];
static bool started, held, present_done;
static uint64_t frame_serial, token_serial, pending_token;

static bool equal(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static void sleep_ms(uint32_t ms) {
    if (clock_api && clock_api->sleep_ms) clock_api->sleep_ms(clock_api->context, ms);
}
static bool level(uint64_t claim, bool value) {
    return gpio_api->write(gpio_api->context, claim, value);
}
static void spi_byte(uint8_t value) {
    for (int bit = 7; bit >= 0; --bit) {
        (void)level(mosi, (value >> bit) & 1);
        (void)level(sclk, true);
        (void)level(sclk, false);
    }
}
static void command(uint8_t cmd) {
    (void)level(dc, false);
    (void)level(cs, false);
    spi_byte(cmd);
    (void)level(cs, true);
}
static void data(const uint8_t *bytes, size_t count) {
    (void)level(dc, true);
    (void)level(cs, false);
    for (size_t i = 0; i < count; ++i) spi_byte(bytes[i]);
    (void)level(cs, true);
}
static void data1(uint8_t value) { data(&value, 1); }
static bool wait_idle(void) {
    for (uint32_t i = 0; i < 2000u; ++i) {
        bool high = true;
        if (!gpio_api->read(gpio_api->context, busy, &high)) return false;
        if (!high) return true;
        sleep_ms(1);
    }
    return false;
}
static bool reset_panel(void) {
    return level(rst, true) && (sleep_ms(20), level(rst, false)) &&
           (sleep_ms(2), level(rst, true)) && (sleep_ms(20), true);
}
static bool init_ssd1677(void) {
    static const uint8_t booster[] = {0xAE, 0xC7, 0xC3, 0xC0, 0x80};
    static const uint8_t gate[] = {0xDF, 0x01, 0x02};
    static const uint8_t x_window[] = {0x00, 0x00, 0x1F, 0x03};
    static const uint8_t y_window[] = {0x00, 0x00, 0xDF, 0x01};
    static const uint8_t origin[] = {0x00, 0x00};
    if (!reset_panel()) return false;
    command(0x12);
    sleep_ms(10);
    command(0x18); data1(0x80);
    command(0x0C); data(booster, sizeof(booster));
    command(0x01); data(gate, sizeof(gate));
    command(0x3C); data1(0x80);
    command(0x11); data1(0x01);
    command(0x44); data(x_window, sizeof(x_window));
    command(0x45); data(y_window, sizeof(y_window));
    command(0x4E); data(origin, sizeof(origin));
    command(0x4F); data(origin, sizeof(origin));
    return true;
}
static bool present_frame(void) {
    static const uint8_t origin[] = {0x00, 0x00};
    command(0x4E); data(origin, sizeof(origin));
    command(0x4F); data(origin, sizeof(origin));
    command(0x24);
    (void)level(dc, true);
    (void)level(cs, false);
    for (size_t i = 0; i < FRAME_BYTES; ++i) spi_byte((uint8_t)~frame[i]);
    (void)level(cs, true);
    command(0x3C); data1(0xC0);
    command(0x22); data1(0xF7);
    command(0x20);
    return wait_idle();
}
static bool get_info(void *context, risc_display_info_v1 *out) {
    (void)context;
    if (!out) return false;
    *out = (risc_display_info_v1){0};
    out->api_version = RISC_DISPLAY_OUTPUT_API_V1;
    out->struct_size = sizeof(*out);
    out->width = X4PRO_PANEL_WIDTH;
    out->height = X4PRO_PANEL_HEIGHT;
    out->physical_width_um = 88500;
    out->physical_height_um = 53100;
    out->supported_formats = RISC_DISPLAY_FORMAT_BIT(RISC_DISPLAY_FORMAT_MONO1);
    out->preferred_format = RISC_DISPLAY_FORMAT_MONO1;
    out->supported_rotations = RISC_DISPLAY_ROTATION_0;
    out->flags = RISC_DISPLAY_INFO_RETAINS_IMAGE;
    out->damage_x_alignment = 8;
    out->damage_width_alignment = 8;
    out->damage_y_alignment = 1;
    out->damage_height_alignment = 1;
    out->nominal_refresh_millihz = 500;
    out->typical_present_latency_us = 1600000;
    return true;
}
static bool acquire(void *context, uint32_t format, risc_display_surface_v1 *out) {
    (void)context;
    if (!started || held || !out || format != RISC_DISPLAY_FORMAT_MONO1) return false;
    if (++frame_serial == 0) ++frame_serial;
    held = true;
    *out = (risc_display_surface_v1){
        frame_serial, frame, X4PRO_PANEL_WIDTH, X4PRO_PANEL_HEIGHT,
        X4PRO_PANEL_WIDTH / 8u, FRAME_BYTES, RISC_DISPLAY_FORMAT_MONO1
    };
    return true;
}
static void release(void *context, risc_display_frame_v1 frame_id) {
    (void)context;
    if (held && frame_id == frame_serial) held = false;
}
static bool submit(void *context, risc_display_frame_v1 frame_id,
                   const risc_display_rect_v1 *damage, size_t count,
                   const risc_display_present_options_v1 *options,
                   risc_display_present_token_v1 *token_out) {
    (void)context;
    (void)damage;
    (void)count;
    (void)options;
    if (!started || !held || frame_id != frame_serial) return false;
    held = false;
    present_done = present_frame();
    if (++token_serial == 0) ++token_serial;
    pending_token = token_serial;
    if (token_out) *token_out = pending_token;
    return present_done;
}
static bool present_status(void *context, risc_display_present_token_v1 token,
                           risc_display_present_status_v1 *out) {
    (void)context;
    if (!out || !token || token != pending_token) return false;
    out->state = present_done ? RISC_DISPLAY_PRESENT_COMPLETE : RISC_DISPLAY_PRESENT_FAILED;
    out->reserved[0] = out->reserved[1] = out->reserved[2] = 0;
    return true;
}
static bool wait_present(void *context, risc_display_present_token_v1 token,
                         uint32_t timeout_ms, risc_display_present_status_v1 *out) {
    (void)timeout_ms;
    return present_status(context, token, out);
}
static bool set_brightness(void *context, uint16_t level, uint16_t maximum) {
    (void)context; (void)level; (void)maximum;
    return false;
}
static bool release_pins(void) {
    const uint64_t pins[] = {cs, dc, rst, sclk, mosi, busy};
    bool ok = true;
    for (size_t i = 0; i < 6; ++i)
        if (pins[i] && gpio_api && !gpio_api->release(gpio_api->context, pins[i])) ok = false;
    cs = dc = rst = sclk = mosi = busy = 0;
    return ok;
}
static bool quiesce(void) {
    if (held) return false;
    if (!release_pins()) return false;
    gpio_api = NULL;
    clock_api = NULL;
    started = false;
    return true;
}
static bool claim_out(uint8_t pin, uint64_t *out) {
    return gpio_api->claim(gpio_api->context, pin, RISC_GPIO_OUTPUT, out);
}
static bool start(const risc_provider_dependency_v1 *deps, size_t count) {
    if (started || !deps || count != 2u) return false;
    for (size_t i = 0; i < count; ++i) {
        if (equal(deps[i].capability_id, RISC_GPIO_BANK_CAPABILITY)) gpio_api = deps[i].api;
        else if (equal(deps[i].capability_id, "platform.clock")) clock_api = deps[i].api;
    }
    if (!gpio_api || !gpio_api->claim || !gpio_api->write || !gpio_api->read ||
        !clock_api || !clock_api->sleep_ms) return false;
    if (!claim_out(X4PRO_PIN_EPD_CS, &cs) || !claim_out(X4PRO_PIN_EPD_DC, &dc) ||
        !claim_out(X4PRO_PIN_EPD_RST, &rst) || !claim_out(X4PRO_PIN_EPD_SCLK, &sclk) ||
        !claim_out(X4PRO_PIN_EPD_MOSI, &mosi) ||
        !gpio_api->claim(gpio_api->context, X4PRO_PIN_EPD_BUSY, RISC_GPIO_INPUT, &busy)) {
        (void)quiesce();
        return false;
    }
    (void)level(cs, true);
    (void)level(sclk, false);
    memset(frame, 0, sizeof(frame));
    if (!init_ssd1677()) { (void)quiesce(); return false; }
    started = true;
    return true;
}
static void stop(void) { (void)quiesce(); }
static const risc_display_output_api_v1 output_api = {
    RISC_DISPLAY_OUTPUT_API_V1, sizeof(output_api), NULL,
    get_info, acquire, release, submit, present_status, wait_present, set_brightness
};
static const risc_driver_v2 driver = {
    RISC_PROVIDER_DRIVER_ABI_V2, sizeof(driver),
    "x4pro-panel", RISC_DISPLAY_OUTPUT_CAPABILITY, RISC_DISPLAY_OUTPUT_API_V1,
    &output_api, start, stop, quiesce
};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &driver : NULL;
}

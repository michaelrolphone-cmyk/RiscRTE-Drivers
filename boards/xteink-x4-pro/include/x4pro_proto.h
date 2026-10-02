#pragma once
/* Host-testable X4 Pro protocol facts. Pin and command choices are derived
 * from CrossPoint/FreeInk app1 notes. Nothing here talks to a device. */
#include "x4pro_pins.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static inline uint16_t x4pro_cw2017_millivolts(uint8_t hi, uint8_t lo) {
    uint16_t raw = (uint16_t)(((uint16_t)hi << 8) | lo) & 0x3fffu;
    return (uint16_t)(((uint32_t)raw * 5u + 8u) >> 4);
}

/* FreeInk: X at byte 0, portrait controller on an 800x480 landscape panel. */
static inline bool x4pro_gt911_map(const uint8_t raw[8], uint16_t *x, uint16_t *y, uint8_t *id) {
    uint16_t raw_x = (uint16_t)(raw[0] | ((uint16_t)raw[1] << 8));
    uint16_t raw_y = (uint16_t)(raw[2] | ((uint16_t)raw[3] << 8));
    if (raw_x >= 480u || raw_y >= 800u) return false;
    *x = raw_y;
    *y = raw_x;
    *id = raw[7] ? raw[7] : 1u;
    return *x < X4PRO_PANEL_WIDTH && *y < X4PRO_PANEL_HEIGHT;
}

static inline uint8_t x4pro_crc7(const uint8_t *data, size_t length) {
    uint8_t crc = 0;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (uint8_t)((crc & 0x80u) ? (uint8_t)((crc << 1) ^ 0x12) : (uint8_t)(crc << 1));
    }
    return (uint8_t)(crc >> 1);
}

/* 6-byte SD command frame: start, host direction, index, argument, crc, end. */
static inline void x4pro_sd_command(uint8_t index, uint32_t arg, uint8_t out[6]) {
    uint8_t body[5] = {
        (uint8_t)(0x40u | (index & 0x3fu)),
        (uint8_t)(arg >> 24), (uint8_t)(arg >> 16), (uint8_t)(arg >> 8), (uint8_t)arg
    };
    out[0] = body[0];
    out[1] = body[1];
    out[2] = body[2];
    out[3] = body[3];
    out[4] = body[4];
    out[5] = (uint8_t)((x4pro_crc7(body, 5) << 1) | 1u);
}

/* Recovered SSD1677 full-update tail. Panel stays on this path. */
static inline const uint8_t *x4pro_ssd1677_full_update(size_t *length) {
    static const uint8_t seq[] = {0x3C, 0xC0, 0x22, 0xF7, 0x20};
    *length = sizeof(seq);
    return seq;
}

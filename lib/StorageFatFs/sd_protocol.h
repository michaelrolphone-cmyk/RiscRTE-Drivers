#pragma once
#include <stddef.h>
#include <stdint.h>
static inline uint8_t risc_sd_crc7(const uint8_t *data, size_t length) {
    uint8_t crc = 0;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (uint8_t)((crc & 0x80u) ? (uint8_t)((crc << 1) ^ 0x12) : (uint8_t)(crc << 1));
    }
    return (uint8_t)(crc >> 1);
}

/* 6-byte SD command frame: start, host direction, index, argument, crc, end. */
static inline void risc_sd_command(uint8_t index, uint32_t arg, uint8_t out[6]) {
    uint8_t body[5] = {
        (uint8_t)(0x40u | (index & 0x3fu)),
        (uint8_t)(arg >> 24), (uint8_t)(arg >> 16), (uint8_t)(arg >> 8), (uint8_t)arg
    };
    out[0] = body[0];
    out[1] = body[1];
    out[2] = body[2];
    out[3] = body[3];
    out[4] = body[4];
    out[5] = (uint8_t)((risc_sd_crc7(body, 5) << 1) | 1u);
}

/* SD data CRC, transmitted most-significant bit first. */
static inline uint16_t risc_sd_crc16(const uint8_t *data, size_t length) {
    uint16_t crc = 0;
    for (size_t i = 0; i < length; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for (unsigned bit = 0; bit < 8u; ++bit)
            crc = (uint16_t)((crc & 0x8000u) ? ((uint32_t)crc << 1) ^ 0x1021u
                                           : (uint32_t)crc << 1);
    }
    return crc;
}

#pragma once
/* Transitional, privileged-only firmware transport for i2c-esp32s3-v2.
 * Upstream providers depend exclusively on RiscI2cBusV1.h. This symbol is
 * NOT a normal application export or part of the generic OS/CPU ABI.
 * Firmware owns and serializes the physical I2C0 controller and its pins.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define RISC_FW_I2C_COMPAT_V1_MAX_BYTES 128u
#define RISC_FW_I2C_COMPAT_V1_MAX_TIMEOUT_MS 3000u
bool risc_fw_i2c_transact_v1(uint8_t address,
                             const uint8_t *write_bytes, size_t write_length,
                             uint8_t *read_bytes, size_t read_length,
                             uint32_t timeout_ms);
#ifdef __cplusplus
}
#endif

#pragma once
#include <stdint.h>
#define RISC_TPS65185_PROFILE_CAPABILITY "board.display.tps65185.profile"
#define RISC_TPS65185_PROFILE_API_V1 1u
/* Explicit selected electrical profile, not a board-name branch in the chip
 * driver or a reinterpretation of hardware.device. GPIO-expander dependency
 * selection belongs to the validated board graph. This profile covers active
 * HIGH power-control outputs with safe LOW states and active HIGH power-good.
 * Chip-specific ordering stays in the TPS provider, not generic Runtime. */
typedef struct {
 uint32_t api_version,struct_size;
 uint16_t outputs,inputs,wakeup,power_good;
 uint16_t vcom_millivolts,reserved; /* Magnitude of negative VCOM, 0..5110/10mV. */
} risc_tps65185_profile_api_v1;

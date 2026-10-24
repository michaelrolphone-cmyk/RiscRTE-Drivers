/* Generated from validated preparation data. Not a runtime ABI. */
#include "prepare.h"
static const risc_board_prepare_config board_prepare_config = {
    .bus_instance_id = 101u,
    .power_address = 52u,
    .identity_register = 3u,
    .identity_mask = 255u,
    .identity_value = 74u,
    .enable_register = 128u,
    .enable_mask = 1u,
    .enable_value = 1u,
    .transaction_timeout_ms = 20u,
    .reset_count = 2u,
    .resets = {
        {1u, false, 10u, 150u},
        {2u, false, 10u, 50u},
    }
};

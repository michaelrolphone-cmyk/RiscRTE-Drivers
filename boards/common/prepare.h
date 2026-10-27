#ifndef RISC_BOARD_PREPARE_H
#define RISC_BOARD_PREPARE_H
/* Internal board-integration helper, NOT a new runtime capability or driver ABI.
 * Caller serializes prepare/finish, validates the board graph before I/O, and
 * supplies the admitted I2C bus matching config.bus_instance_id. All arguments
 * and callback contexts remain alive until finish succeeds. A successful prepare
 * retains reset GPIO custody for the consumer lifetime. Consumers must stop
 * before finish. Never release and re-claim a live consumer's reset pin. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "RiscI2cBusV1.h"
#define RISC_BOARD_RESET_MAX 8u

typedef struct {
    uint8_t pin;
    bool active_high;
    uint16_t assert_ms, recovery_ms;
} risc_board_reset;
typedef struct {
    uint64_t bus_instance_id;
    uint8_t power_address, identity_register, identity_mask, identity_value;
    uint8_t enable_register, enable_mask, enable_value;
    uint32_t transaction_timeout_ms;
    size_t reset_count;
    risc_board_reset resets[RISC_BOARD_RESET_MAX];
} risc_board_prepare_config;
typedef struct {
    void *context;
    /* Claim stages initial_level before enabling output. False/nonzero token
     * is retained ownership, not permission to forget it. */
    bool (*claim_reset)(void *, uint8_t pin, bool initial_level, uint64_t *token);
    bool (*write_reset)(void *, uint64_t token, bool level);
    bool (*read_reset)(void *, uint64_t token, bool *level);
    bool (*release_reset)(void *, uint64_t token);
    /* True means at least the requested duration elapsed. */
    bool (*delay_ms)(void *, uint32_t milliseconds);
} risc_board_prepare_io;
typedef struct {
    const risc_i2c_bus_api_v1 *bus;
    const risc_board_prepare_io *io;
    uint64_t power_claim, reset_claims[RISC_BOARD_RESET_MAX];
    bool ready;
} risc_board_prepare_state;
/* Initialize state to zero exactly once. Invalid configuration causes no I/O.
 * Any false result requires finish before retry, even when no token is visible.
 * Calling prepare again on a live state returns false without touching hardware.
 * PMIC traffic is READ ONLY: one register selector plus one received byte.
 * No supply, charging, cutoff, interrupt or power-key register is written. */
bool risc_board_prepare(risc_board_prepare_state *, const risc_board_prepare_config *,
                        uint64_t admitted_bus_instance_id,
                        const risc_i2c_bus_api_v1 *, const risc_board_prepare_io *);
/* Cleanup retries every retained claim; clears references only after all owners
 * have confirmed release. Does not power off, sleep, reset or restore a board.
 * Electrical pin state after release follows the admitted GPIO owner's policy. */
bool risc_board_prepare_finish(risc_board_prepare_state *);
#endif

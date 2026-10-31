#ifndef RISC_BQ_SHUTDOWN_POLICY_H
#define RISC_BQ_SHUTDOWN_POLICY_H

#include "BqChargerProfile.h"

/* An accepted BATFET_DIS command may remove the very power needed to confirm
 * its register state. Never claim verified power-off from an I2C ACK, and
 * never release the chip claim or re-enable charging once BATFET_DIS may have
 * reached the PMIC. The owner must remain pinned through device shutdown. */
typedef enum {
    RISC_BQ_SHUTDOWN_REJECTED = 0,
    RISC_BQ_SHUTDOWN_CHARGE_UNCERTAIN,
    RISC_BQ_SHUTDOWN_BATFET_UNCERTAIN,
    RISC_BQ_SHUTDOWN_COMMAND_ACCEPTED
} risc_bq_shutdown_result;

/* The caller must first prove no live USB VBUS source lease, pending power
 * transition, previous uncertain shutdown or charge profile. No second I2C
 * address claim is made here. The board's historical shutdown sequence is
 * charge off, then BATFET_DIS; OTG must already be verified off. */
static inline risc_bq_shutdown_result risc_bq_request_shutdown(
    const risc_bq_charger_io *io) {
    if (!io || !io->read || !io->write) return RISC_BQ_SHUTDOWN_REJECTED;
    uint8_t id = 0, status = 0, vbus = 0, power = 0, batfet = 0;
    if (!io->read(io->context, 0x14u, &id) ||
        !io->read(io->context, 0x0bu, &status) ||
        !io->read(io->context, 0x11u, &vbus) ||
        !io->read(io->context, 0x03u, &power) ||
        !io->read(io->context, 0x09u, &batfet))
        return RISC_BQ_SHUTDOWN_REJECTED;
    /* PN[5:3] BQ25896=0; VBUS_STAT, PG_STAT and REG11 VBUS_GD all
     * independently veto shutdown on external power or ambiguous input. */
    if ((id & 0x38u) || (status & 0xe4u) || (vbus & 0x80u) ||
        (power & 0x20u) || (batfet & 0x20u))
        return RISC_BQ_SHUTDOWN_REJECTED;

    if (power & 0x10u) {
        const uint8_t no_charge = (uint8_t)(power & (uint8_t)~0x10u);
        /* NACK does not prove the charge-disable write was not applied. */
        if (!io->write(io->context, 0x03u, no_charge))
            return RISC_BQ_SHUTDOWN_CHARGE_UNCERTAIN;
        uint8_t actual = 0;
        if (!io->read(io->context, 0x03u, &actual) ||
            (actual & 0x30u) != 0u)
            return RISC_BQ_SHUTDOWN_CHARGE_UNCERTAIN;
    }
    /* No readback: BATFET_DIS can terminate I2C power immediately.
     * Even a failed bus write may have applied and is non-retryable here. */
    return io->write(io->context, 0x09u, (uint8_t)(batfet | 0x20u)) ?
        RISC_BQ_SHUTDOWN_COMMAND_ACCEPTED : RISC_BQ_SHUTDOWN_BATFET_UNCERTAIN;
}

#endif /* RISC_BQ_SHUTDOWN_POLICY_H */

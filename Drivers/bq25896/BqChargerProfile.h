#ifndef RISC_BQ_CHARGER_PROFILE_H
#define RISC_BQ_CHARGER_PROFILE_H

/* Owner-private BQ25896 policy, derived from BoardT5S3::kBatteryProfile and
 * lib/bq25896/include/bq25896_reg.h. No firmware charger HAL, Wire or
 * competing I2C claim. The caller owns and serializes the 0x6B bus lease.
 * The old firmware driver reset the entire register file on initialization;
 * doing so here could interrupt OTG and is intentionally prohibited. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    void *context;
    bool (*read)(void *context, uint8_t reg, uint8_t *value);
    bool (*write)(void *context, uint8_t reg, uint8_t value);
} risc_bq_charger_io;

typedef struct {
    uint8_t reg;
    uint8_t mask;
    uint8_t value;
} risc_bq_profile_field;

/* Board profile: 1000mA input, 512mA charge, 64mA precharge/termination,
 * 4208mV regulation, 3300mV SYS_MIN, continuous ADC and disabled watchdog.
 * Keep EN_ILIM/ICO enabled as the existing library's default does. The
 * BATFET_DIS bit is cleared so the battery system rail remains available. */
static const risc_bq_profile_field risc_bq_t5s3_charge_profile[] = {
    {0x09u, 0x20u, 0x00u}, /* enable battery power path */
    {0x00u, 0xffu, 0x52u}, /* EN_HIZ=0, EN_ILIM=1, IINLIM=(1000-100)/50 */
    {0x02u, 0x50u, 0x50u}, /* ICO=1, ADC continuous */
    {0x04u, 0x7fu, 0x08u}, /* 512mA / 64mA */
    {0x05u, 0xffu, 0x00u}, /* (64-64)/64 for precharge and termination */
    {0x06u, 0xfcu, 0x5cu}, /* ((4208-3840)/16)<<2 */
    {0x07u, 0x30u, 0x00u}, /* watchdog disabled */
    {0x03u, 0x3eu, 0x16u}, /* OTG=0, charge=1, SYS_MIN=(3300-3000)/100 */
};

/* Report uncertainty only once a configuration write may have affected the
 * device. An uncertain result requires the provider to retain its bus claim,
 * quarantine USB sourcing/unload and surface a failure to its caller. Never
 * declare success based on an ACK without a matching register readback. */
static inline bool risc_bq_apply_charge_profile(const risc_bq_charger_io *io,
                                                 bool *uncertain) {
    if (uncertain) *uncertain = false;
    if (!io || !io->read || !io->write || !uncertain) return false;
    uint8_t status = 0, power = 0, device_id = 0;
    if (!io->read(io->context, 0x0bu, &status) ||
        !io->read(io->context, 0x03u, &power) ||
        !io->read(io->context, 0x14u, &device_id)) return false;
    /* REG14 PN[5:3]=0 is BQ25896. A sourcing or externally OTG-reported
     * device is never reconfigured while a USB host may be active. */
    if ((device_id & 0x38u) != 0u || (power & 0x20u) != 0u ||
        (status & 0xe0u) == 0xe0u) return false;

    enum { FIELD_COUNT = sizeof(risc_bq_t5s3_charge_profile) /
                         sizeof(risc_bq_t5s3_charge_profile[0]) };
    uint8_t previous[FIELD_COUNT];
    uint8_t desired[FIELD_COUNT];
    /* Pre-read every register so an unavailable chip never gets a partial
     * profile simply because a later register was unreadable. */
    for (size_t i = 0; i < FIELD_COUNT; ++i) {
        const risc_bq_profile_field *field = &risc_bq_t5s3_charge_profile[i];
        if (!io->read(io->context, field->reg, &previous[i])) return false;
        desired[i] = (uint8_t)((previous[i] & (uint8_t)~field->mask) |
                               (field->value & field->mask));
    }
    for (size_t i = 0; i < FIELD_COUNT; ++i) {
        const risc_bq_profile_field *field = &risc_bq_t5s3_charge_profile[i];
        if (desired[i] != previous[i]) {
            /* A NACK is not evidence that a write did not reach the chip. */
            *uncertain = true;
            if (!io->write(io->context, field->reg, desired[i])) return false;
        }
        uint8_t actual = 0;
        if (!io->read(io->context, field->reg, &actual) ||
            (actual & field->mask) != (desired[i] & field->mask)) {
            *uncertain = true;
            return false;
        }
    }
    *uncertain = false;
    return true;
}

#endif /* RISC_BQ_CHARGER_PROFILE_H */

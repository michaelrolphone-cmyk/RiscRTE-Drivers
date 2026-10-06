#pragma once
#include <stdint.h>
volatile uint32_t *iq_test_register(uint32_t address);
volatile uint32_t *iq_test_bank(void);
uint32_t iq_test_cycles(void);
uint8_t iq_test_analog_read(uint8_t block, uint8_t host, uint8_t reg);
void iq_test_analog_write(uint8_t block, uint8_t host, uint8_t reg, uint8_t value);
unsigned iq_test_pbus_read(unsigned block, unsigned index);
void iq_test_delay(uint32_t us);
#define ROM_I2C_READ iq_test_analog_read
#define ROM_I2C_WRITE iq_test_analog_write
#define ROM_PBUS_RD iq_test_pbus_read
#define ROM_DELAY_US iq_test_delay

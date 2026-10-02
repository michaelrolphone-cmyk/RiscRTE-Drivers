/* Bit-banged I2C on X4 Pro GPIO39/GPIO38. The T5S3 i2c-esp32s3-v2 ELF delegates
 * to firmware Wire and is not the X4 Pro pin pair. */
#include "RiscGpioBankV1.h"
#include "RiscI2cBusV1.h"
#include "RiscPlatformClockV1.h"
#include "x4pro_pins.h"
#include <stddef.h>

#define MAX_CLAIMS 8u
static const risc_gpio_bank_api_v1 *gpio_api;
static const risc_platform_clock_api_v1 *clock_api;
static uint64_t sda_claim, scl_claim;
static bool started;
typedef struct { uint64_t token; uint8_t address; } dev_t;
static dev_t devices[MAX_CLAIMS];
static uint64_t next_token = 1;

static void delay(void) {
    if (clock_api && clock_api->sleep_ms) clock_api->sleep_ms(clock_api->context, 0);
    for (volatile int i = 0; i < 40; ++i) { }
}
static bool sda(bool level) { return gpio_api->write(gpio_api->context, sda_claim, level); }
static bool scl(bool level) { return gpio_api->write(gpio_api->context, scl_claim, level); }
static bool sda_read(bool *level) { return gpio_api->read(gpio_api->context, sda_claim, level); }
static bool start_cond(void) {
    return sda(true) && scl(true) && (delay(), sda(false)) && (delay(), scl(false));
}
static bool stop_cond(void) {
    return sda(false) && scl(true) && (delay(), sda(true));
}
static bool write_bit(bool bit) {
    bool scl_high = false;
    if (!sda(bit) || !scl(true)) return false;
    delay();
    if (!sda_read(&scl_high) && bit) return false;
    return scl(false);
}
static bool read_bit(bool *bit) {
    bool level = false;
    if (!sda(true) || !scl(true) || !sda_read(&level)) return false;
    delay();
    *bit = level;
    return scl(false);
}
static bool write_byte(uint8_t value, bool *ack) {
    for (int i = 7; i >= 0; --i)
        if (!write_bit((value >> i) & 1)) return false;
    return read_bit(ack) && (*ack = !*ack, true);
}
static bool read_byte(uint8_t *out, bool ack) {
    uint8_t value = 0;
    for (int i = 7; i >= 0; --i) {
        bool bit = false;
        if (!read_bit(&bit)) return false;
        value = (uint8_t)((value << 1) | (bit ? 1u : 0u));
    }
    *out = value;
    return write_bit(!ack);
}
static bool equal(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static bool claim_device(void *context, uint8_t address, uint64_t *out) {
    (void)context;
    if (out) *out = 0;
    if (!started || !out || address < 0x08u || address > 0x77u || next_token == UINT64_MAX)
        return false;
    dev_t *slot = NULL;
    for (size_t i = 0; i < MAX_CLAIMS; ++i) {
        if (devices[i].token && devices[i].address == address) return false;
        if (!devices[i].token && !slot) slot = &devices[i];
    }
    if (!slot) return false;
    slot->address = address;
    slot->token = next_token++;
    *out = slot->token;
    return true;
}
static bool transact(void *context, uint64_t token, const uint8_t *write_bytes,
                     size_t write_length, uint8_t *read_bytes, size_t read_length,
                     uint32_t timeout_ms) {
    (void)context;
    (void)timeout_ms;
    uint8_t address = 0;
    for (size_t i = 0; i < MAX_CLAIMS; ++i)
        if (devices[i].token == token) address = devices[i].address;
    if (!started || !address || (!write_length && !read_length) ||
        (write_length && !write_bytes) || (read_length && !read_bytes) ||
        write_length > 64u || read_length > 64u)
        return false;
    if (!start_cond()) return false;
    bool ack = false;
    if (write_length) {
        if (!write_byte((uint8_t)(address << 1), &ack) || !ack) { (void)stop_cond(); return false; }
        for (size_t i = 0; i < write_length; ++i)
            if (!write_byte(write_bytes[i], &ack) || !ack) { (void)stop_cond(); return false; }
        if (read_length && !start_cond()) { (void)stop_cond(); return false; }
    }
    if (read_length) {
        if (!write_byte((uint8_t)((address << 1) | 1u), &ack) || !ack) {
            (void)stop_cond();
            return false;
        }
        for (size_t i = 0; i < read_length; ++i)
            if (!read_byte(&read_bytes[i], i + 1u < read_length)) { (void)stop_cond(); return false; }
    }
    return stop_cond();
}
static bool release_device(void *context, uint64_t token) {
    (void)context;
    for (size_t i = 0; i < MAX_CLAIMS; ++i)
        if (devices[i].token == token) { devices[i] = (dev_t){0}; return true; }
    return false;
}
static bool quiesce(void) {
    for (size_t i = 0; i < MAX_CLAIMS; ++i)
        if (devices[i].token) return false;
    if (sda_claim && gpio_api && !gpio_api->release(gpio_api->context, sda_claim)) return false;
    if (scl_claim && gpio_api && !gpio_api->release(gpio_api->context, scl_claim)) return false;
    sda_claim = scl_claim = 0;
    gpio_api = NULL;
    clock_api = NULL;
    started = false;
    return true;
}
static bool start(const risc_provider_dependency_v1 *deps, size_t count) {
    if (started || !deps || count != 2u) return false;
    for (size_t i = 0; i < count; ++i) {
        if (equal(deps[i].capability_id, RISC_GPIO_BANK_CAPABILITY))
            gpio_api = deps[i].api;
        else if (equal(deps[i].capability_id, "platform.clock"))
            clock_api = deps[i].api;
    }
    if (!gpio_api || gpio_api->api_version != RISC_GPIO_BANK_API_V1 ||
        !gpio_api->claim || !gpio_api->write || !gpio_api->read || !gpio_api->release ||
        !clock_api || !clock_api->sleep_ms)
        return false;
    if (!gpio_api->claim(gpio_api->context, X4PRO_PIN_I2C_SDA,
                         RISC_GPIO_INPUT | RISC_GPIO_OUTPUT | RISC_GPIO_PULLUP, &sda_claim) ||
        !gpio_api->claim(gpio_api->context, X4PRO_PIN_I2C_SCL,
                         RISC_GPIO_INPUT | RISC_GPIO_OUTPUT | RISC_GPIO_PULLUP, &scl_claim)) {
        (void)quiesce();
        return false;
    }
    (void)sda(true);
    (void)scl(true);
    started = true;
    return true;
}
static void stop(void) { (void)quiesce(); }
static const risc_i2c_bus_api_v1 api = {
    RISC_I2C_BUS_API_V1, sizeof(api), NULL, claim_device, transact, release_device
};
static const risc_driver_v2 driver = {
    RISC_PROVIDER_DRIVER_ABI_V2, sizeof(driver),
    "x4pro-i2c", "i2c.bus", RISC_I2C_BUS_API_V1, &api, start, stop, quiesce
};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &driver : NULL;
}

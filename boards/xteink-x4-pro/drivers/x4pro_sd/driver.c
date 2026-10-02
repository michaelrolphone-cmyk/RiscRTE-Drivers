/* 1-bit SD on the confirmed SDMMC pins. Stock SPI CMD0 is silent, so this ELF
 * bit-bangs the SD command path on CLK/CMD/DAT0. GPIO5 is the active-low power
 * gate. Filesystem services fail closed until a FAT boot sector is parsed. */
#include "RiscGpioBankV1.h"
#include "RiscPlatformClockV1.h"
#include "RiscStorageVolumeV1.h"
#include "x4pro_pins.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static const risc_gpio_bank_api_v1 *gpio_api;
static const risc_platform_clock_api_v1 *clock_api;
static uint64_t pwr, clk, cmd, dat0;
static bool started, card_ready, fat_ready;
static uint16_t rca;
static uint8_t sector[512];
static uint32_t root_cluster, sectors_per_cluster, fat_start, data_start;
static char error[80];
static bool file_open;

static bool equal(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static void fail(const char *text) {
    size_t i = 0;
    while (text[i] && i + 1u < sizeof(error)) { error[i] = text[i]; ++i; }
    error[i] = 0;
}
static void tick(void) {
    (void)gpio_api->write(gpio_api->context, clk, true);
    (void)gpio_api->write(gpio_api->context, clk, false);
}
static bool cmd_bit(bool bit) {
    return gpio_api->write(gpio_api->context, cmd, bit) && (tick(), true);
}
static bool dat_bit(bool *bit) {
    bool level = true;
    if (!gpio_api->read(gpio_api->context, dat0, &level)) return false;
    tick();
    *bit = level;
    return true;
}
static uint8_t crc7(const uint8_t *data, size_t length) {
    uint8_t crc = 0;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 0x80u) ? (uint8_t)((crc << 1) ^ 0x12) : (uint8_t)(crc << 1);
    }
    return crc >> 1;
}
static bool command(uint8_t index, uint32_t arg, uint8_t *response, size_t length) {
    uint8_t raw[5] = { (uint8_t)(0x40u | index), (uint8_t)(arg >> 24), (uint8_t)(arg >> 16),
                       (uint8_t)(arg >> 8), (uint8_t)arg };
    uint8_t crc = (uint8_t)((crc7(raw, 5) << 1) | 1u);
    for (int i = 0; i < 8; ++i) tick();
    if (!cmd_bit(false)) return false;
    if (!cmd_bit(index >= 40)) return false; /* host to card */
    for (int bit = 5; bit >= 0; --bit) if (!cmd_bit((index >> bit) & 1)) return false;
    for (int bit = 31; bit >= 0; --bit) if (!cmd_bit((arg >> bit) & 1)) return false;
    for (int bit = 6; bit >= 0; --bit) if (!cmd_bit((crc >> bit) & 1)) return false;
    (void)gpio_api->write(gpio_api->context, cmd, true);
    bool seen = false;
    for (int i = 0; i < 64 && !seen; ++i) {
        bool bit = true;
        if (!gpio_api->read(gpio_api->context, cmd, &bit)) return false;
        tick();
        seen = !bit;
    }
    if (!seen) return false;
    memset(response, 0, length);
    for (size_t byte = 0; byte < length; ++byte) {
        uint8_t value = 0;
        for (int bit = 0; bit < 8; ++bit) {
            bool level = true;
            if (!gpio_api->read(gpio_api->context, cmd, &level)) return false;
            tick();
            value = (uint8_t)((value << 1) | (level ? 1u : 0u));
        }
        response[byte] = value;
    }
    return true;
}
static bool read_block(uint32_t lba) {
    uint8_t response[6] = {0};
    if (!command(17, lba, response, 1) || (response[0] & 0xfeu)) { fail("CMD17 rejected"); return false; }
    bool start = true;
    for (int i = 0; i < 100000 && start; ++i)
        if (!dat_bit(&start)) return false;
    if (start) { fail("data token timeout"); return false; }
    for (size_t i = 0; i < 512; ++i) {
        uint8_t value = 0;
        for (int bit = 0; bit < 8; ++bit) {
            bool level = true;
            if (!dat_bit(&level)) return false;
            value = (uint8_t)((value << 1) | (level ? 1u : 0u));
        }
        sector[i] = value;
    }
    bool ignore = false;
    for (int i = 0; i < 16; ++i) (void)dat_bit(&ignore);
    return true;
}
static bool init_card(void) {
    uint8_t response[6] = {0};
    (void)gpio_api->write(gpio_api->context, pwr, true);
    clock_api->sleep_ms(clock_api->context, 5);
    (void)gpio_api->write(gpio_api->context, pwr, false);
    clock_api->sleep_ms(clock_api->context, 20);
    (void)gpio_api->write(gpio_api->context, cmd, true);
    for (int i = 0; i < 80; ++i) tick();
    if (!command(0, 0, response, 1)) { fail("CMD0 no response"); return false; }
    if (!command(8, 0x1AAu, response, 5)) { fail("CMD8 no response"); return false; }
    for (int i = 0; i < 200; ++i) {
        if (!command(55, 0, response, 1) || !command(41, 0x40100000u, response, 1)) {
            fail("ACMD41 failed");
            return false;
        }
        if (response[0] & 0x80u) break;
        clock_api->sleep_ms(clock_api->context, 10);
        if (i == 199) { fail("card idle"); return false; }
    }
    if (!command(2, 0, response, 5) || !command(3, 0, response, 2)) { fail("identify failed"); return false; }
    rca = (uint16_t)((response[0] << 8) | response[1]);
    if (!command(7, (uint32_t)rca << 16, response, 1) || !command(16, 512, response, 1)) {
        fail("select failed");
        return false;
    }
    card_ready = true;
    if (!read_block(0)) return false;
    if (sector[510] != 0x55 || sector[511] != 0xAA) { fail("no FAT boot"); return true; }
    sectors_per_cluster = sector[13] ? sector[13] : 1;
    uint16_t reserved = (uint16_t)(sector[14] | (sector[15] << 8));
    uint8_t fats = sector[16] ? sector[16] : 1;
    uint32_t fat_sectors = sector[22] | ((uint32_t)sector[23] << 8);
    if (!fat_sectors) fat_sectors = sector[36] | ((uint32_t)sector[37] << 8) |
                                    ((uint32_t)sector[38] << 16) | ((uint32_t)sector[39] << 24);
    fat_start = reserved;
    data_start = reserved + fats * fat_sectors;
    root_cluster = sector[44] | ((uint32_t)sector[45] << 8) |
                   ((uint32_t)sector[46] << 16) | ((uint32_t)sector[47] << 24);
    fat_ready = root_cluster != 0;
    if (!fat_ready) fail("FAT root missing");
    return true;
}
static bool refresh(void *context) {
    (void)context;
    card_ready = fat_ready = false;
    error[0] = 0;
    return started && init_card();
}
static bool ready(void *context) { (void)context; return started && card_ready; }
static bool label(void *context, char *out, size_t capacity) {
    (void)context;
    if (!out || capacity < 6) return false;
    memcpy(out, "X4PRO", 6);
    return card_ready;
}
static bool stat(void *context, const char *path, uint64_t *size, bool *is_dir) {
    (void)context; (void)path; (void)size; (void)is_dir;
    fail("directory walk not published");
    return false;
}
static risc_storage_dir_t dir_open(void *context, const char *path) {
    (void)context; (void)path; return RISC_STORAGE_DIR_INVALID;
}
static bool dir_next(void *context, risc_storage_dir_t dir, risc_storage_dirent_v1 *entry) {
    (void)context; (void)dir; (void)entry; return false;
}
static void dir_close(void *context, risc_storage_dir_t dir) { (void)context; (void)dir; }
static risc_storage_file_t file_open_read(void *context, const char *path, uint64_t *size) {
    (void)context; (void)path; (void)size; fail("file open not published"); return RISC_STORAGE_FILE_INVALID;
}
static size_t file_read(void *context, risc_storage_file_t file, void *buffer, size_t capacity) {
    (void)context; (void)file; (void)buffer; (void)capacity; return 0;
}
static risc_storage_file_t file_open_write(void *context, const char *path) {
    (void)context; (void)path; return RISC_STORAGE_FILE_INVALID;
}
static size_t file_write(void *context, risc_storage_file_t file, const void *buffer, size_t size) {
    (void)context; (void)file; (void)buffer; (void)size; return 0;
}
static bool file_close(void *context, risc_storage_file_t file, bool commit) {
    (void)context; (void)file; (void)commit; return false;
}
static bool remove_path(void *context, const char *path) { (void)context; (void)path; return false; }
static bool last_error(void *context, char *out, size_t capacity) {
    (void)context;
    if (!out || !capacity) return false;
    size_t i = 0;
    while (error[i] && i + 1u < capacity) { out[i] = error[i]; ++i; }
    out[i] = 0;
    return error[0] != 0;
}
static bool quiesce(void) {
    if (file_open) return false;
    bool ok = true;
    if (pwr && gpio_api) {
        (void)gpio_api->write(gpio_api->context, pwr, true);
        if (!gpio_api->release(gpio_api->context, pwr)) ok = false;
    }
    const uint64_t pins[] = {clk, cmd, dat0};
    for (size_t i = 0; i < 3; ++i)
        if (pins[i] && gpio_api && !gpio_api->release(gpio_api->context, pins[i])) ok = false;
    pwr = clk = cmd = dat0 = 0;
    gpio_api = NULL;
    clock_api = NULL;
    started = card_ready = fat_ready = false;
    return ok;
}
static bool start(const risc_provider_dependency_v1 *deps, size_t count) {
    if (started || !deps || count != 2u) return false;
    for (size_t i = 0; i < count; ++i) {
        if (equal(deps[i].capability_id, RISC_GPIO_BANK_CAPABILITY)) gpio_api = deps[i].api;
        else if (equal(deps[i].capability_id, "platform.clock")) clock_api = deps[i].api;
    }
    if (!gpio_api || !clock_api || !clock_api->sleep_ms) return false;
    if (!gpio_api->claim(gpio_api->context, X4PRO_PIN_SD_PWR, RISC_GPIO_OUTPUT, &pwr) ||
        !gpio_api->claim(gpio_api->context, X4PRO_PIN_SD_CLK, RISC_GPIO_OUTPUT, &clk) ||
        !gpio_api->claim(gpio_api->context, X4PRO_PIN_SD_CMD,
                         RISC_GPIO_INPUT | RISC_GPIO_OUTPUT | RISC_GPIO_PULLUP, &cmd) ||
        !gpio_api->claim(gpio_api->context, X4PRO_PIN_SD_DAT0,
                         RISC_GPIO_INPUT | RISC_GPIO_PULLUP, &dat0)) {
        (void)quiesce();
        return false;
    }
    started = true;
    return refresh(NULL);
}
static void stop(void) { (void)quiesce(); }
static const risc_storage_volume_api_v1 api = {
    RISC_STORAGE_VOLUME_API_V1, sizeof(api), NULL,
    refresh, ready, label, stat, dir_open, dir_next, dir_close,
    file_open_read, file_read, file_open_write, file_write, file_close,
    remove_path, last_error
};
static const risc_driver_v2 driver = {
    RISC_PROVIDER_DRIVER_ABI_V2, sizeof(driver),
    "x4pro-sd", "storage.volume", RISC_STORAGE_VOLUME_API_V1,
    &api, start, stop, quiesce
};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &driver : NULL;
}

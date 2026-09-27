#include "T5DriverApi.h"
#include "T5GnssProvider.h"
#include <string.h>

// Bounded, allocation-free NMEA 0183 GGA/RMC provider. UART, power, and time
// primitives are injected by the runtime; the ELF imports no hardware symbols.
static const t5_kernel_io_v1 *io;
static t5_gps_state_t fix;
static char sentence[128];
static size_t length;
static bool collecting, locked, have_fix;
static uint8_t baud_index;
static uint32_t baud_started, fix_at;
static const uint32_t bauds[] = {9600, 38400};

static uint32_t now(void) { return io->millis(io->context); }
static int hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}
static bool number(const char *s, double *out) {
    if (!s || !*s) return false;
    bool negative = *s == '-';
    if (*s == '-' || *s == '+') ++s;
    bool digit = false, point = false;
    double value = 0, scale = 1;
    for (; *s; ++s) {
        if (*s == '.' && !point) { point = true; continue; }
        if (*s < '0' || *s > '9') return false;
        digit = true;
        value = value * 10 + (*s - '0');
        if (point) scale *= 10;
        if (value > 1e12 || scale > 1e12) return false;
    }
    *out = (negative ? -value : value) / scale;
    return digit;
}
static bool coordinate(const char *text, const char *hemisphere, bool latitude, double *out) {
    double value;
    if (!number(text, &value) || value < 0 || value > 18000 || !hemisphere || !hemisphere[0] || hemisphere[1] != 0) return false;
    if (latitude ? (*hemisphere != 'N' && *hemisphere != 'S') : (*hemisphere != 'E' && *hemisphere != 'W')) return false;
    unsigned degrees = (unsigned)(value / 100);
    double minutes = value - degrees * 100;
    if (minutes >= 60 || degrees > (latitude ? 90u : 180u)) return false;
    value = degrees + minutes / 60;
    if (value > (latitude ? 90 : 180)) return false;
    *out = (*hemisphere == 'S' || *hemisphere == 'W') ? -value : value;
    return true;
}
static void parse(void) {
    char *star = strchr(sentence, '*');
    if (!star || strlen(star) != 3 || hex(star[1]) < 0 || hex(star[2]) < 0) return;
    uint8_t sum = 0;
    for (char *p = sentence; p < star; ++p) sum ^= (uint8_t)*p;
    if (sum != (uint8_t)((hex(star[1]) << 4) | hex(star[2]))) return;
    locked = true;
    fix.receiver_detected = 1;
    *star = 0;
    char *fields[20];
    unsigned count = 1;
    fields[0] = sentence;
    for (char *p = sentence; *p; ++p) {
        if (*p == ',') {
            *p = 0;
            if (count == 20) return;
            fields[count++] = p + 1;
        }
    }
    if (strlen(fields[0]) != 5) return;
    bool gga = strcmp(fields[0] + 2, "GGA") == 0;
    bool rmc = strcmp(fields[0] + 2, "RMC") == 0;
    if ((!gga && !rmc) || count < (gga ? 11u : 10u)) return;
    double lat, lon, value;
    bool valid;
    if (gga) {
        valid = number(fields[6], &value) && value >= 1 && value <= 8;
        if (number(fields[7], &value) && value >= 0) fix.satellites = value > 255 ? 255 : (uint8_t)value;
        if (number(fields[8], &value) && value >= 0) fix.hdop = (float)value;
        if (number(fields[9], &value) && strcmp(fields[10], "M") == 0) fix.altitude_m = (float)value;
    } else {
        valid = strcmp(fields[2], "A") == 0;
        if (number(fields[7], &value) && value >= 0) fix.speed_kph = (float)(value * 1.852);
        if (number(fields[8], &value) && value >= 0 && value < 360) fix.course_deg = (float)value;
    }
    if (!valid) { have_fix = false; return; }
    unsigned lat_field = gga ? 2 : 3;
    unsigned lon_field = gga ? 4 : 5;
    if (!coordinate(fields[lat_field], fields[lat_field + 1], true, &lat) ||
        !coordinate(fields[lon_field], fields[lon_field + 1], false, &lon)) return;
    fix.latitude = lat;
    fix.longitude = lon;
    fix_at = now();
    have_fix = true;
}
static void feed(uint8_t c) {
    ++fix.chars_processed;
    if (c == '$') { collecting = true; length = 0; return; }
    if (!collecting) return;
    if (c == '\r' || c == '\n') {
        sentence[length] = 0;
        collecting = false;
        parse();
    } else if (length + 1 < sizeof(sentence)) {
        sentence[length++] = (char)c;
    } else collecting = false;
}
static bool begin_baud(uint8_t index) {
    io->serial_close(io->context);
    io->sleep_ms(io->context, 10);
    baud_index = index;
    length = 0;
    collecting = false;
    fix.baud = bauds[index];
    baud_started = now();
    return io->serial_open(io->context, fix.baud);
}
static void stop(void) {
    if (io) {
        io->serial_close(io->context);
        io->power_release(io->context);
    }
    io = 0;
    have_fix = locked = collecting = false;
}
static bool start(const t5_kernel_io_v1 *host) {
    if (io) return true;
    if (!host || host->api_version != T5_KERNEL_IO_API_VERSION || host->struct_size < sizeof(*host) ||
        !host->millis || !host->sleep_ms || !host->power_acquire || !host->power_release ||
        !host->serial_open || !host->serial_close || !host->serial_read) return false;
    memset(&fix, 0, sizeof(fix));
    fix.hdop = -1;
    have_fix = locked = collecting = false;
    io = host;
    if (!io->power_acquire(io->context)) { io = 0; return false; }
    io->sleep_ms(io->context, 20);
    if (!begin_baud(0)) { stop(); return false; }
    return true;
}
static bool read_state(t5_gps_state_t *state) {
    if (!state) return false;
    memset(state, 0, sizeof(*state));
    if (!io) { state->status = T5_GPS_STATUS_OFF; return true; }
    uint8_t buffer[128];
    // Bound work so a noisy serial device cannot monopolize the app task.
    for (unsigned budget = 0; budget < 8; ++budget) {
        size_t count = io->serial_read(io->context, buffer, sizeof(buffer));
        if (count > sizeof(buffer)) { stop(); return false; }
        for (size_t i = 0; i < count; ++i) feed(buffer[i]);
        if (count < sizeof(buffer)) break;
    }
    if (!locked && now() - baud_started >= 1600 && !begin_baud(baud_index ^ 1u)) {
        stop();
        return false;
    }
    *state = fix;
    state->age_ms = have_fix ? now() - fix_at : UINT32_MAX;
    state->fix_valid = have_fix && state->age_ms <= 5000;
    state->status = state->fix_valid ? T5_GPS_STATUS_FIX : T5_GPS_STATUS_SEARCHING;
    if (!state->fix_valid) state->latitude = state->longitude = 0;
    return true;
}
static const t5_gnss_api_v1 capability = {T5_GNSS_API_VERSION, sizeof(t5_gnss_api_v1), read_state};
static const t5_driver_v1 driver = {T5_DRIVER_ABI_VERSION, sizeof(t5_driver_v1), "gps-nmea",
                                   T5_GNSS_CAPABILITY, T5_GNSS_API_VERSION, &capability, start, stop};
__attribute__((visibility("default")))
const t5_driver_v1 *t5_driver_get(uint32_t abi) { return abi == T5_DRIVER_ABI_VERSION ? &driver : 0; }

#include "x4pro_proto.h"
#include <stdio.h>
#include <string.h>

static int failures;

static void expect(int cond, const char *message) {
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", message);
        ++failures;
    }
}

int main(void) {
    expect(x4pro_cw2017_millivolts(0x0F, 0xFF) == (uint16_t)(((0x0FFFu * 5u + 8u) >> 4)), "vcell");
    expect(x4pro_cw2017_millivolts(0xFF, 0xFF) == (uint16_t)(((0x3FFFu * 5u + 8u) >> 4)), "vcell mask");

    uint16_t x = 0, y = 0;
    uint8_t id = 0;
    uint8_t point[8] = {10, 0, 20, 0, 0, 0, 0, 3};
    expect(x4pro_gt911_map(point, &x, &y, &id) && x == 20 && y == 10 && id == 3, "swapxy");
    point[0] = 224;
    point[1] = 1;
    expect(!x4pro_gt911_map(point, &x, &y, &id), "x range");

    uint8_t frame[6];
    x4pro_sd_command(0, 0, frame);
    expect(frame[0] == 0x40, "cmd0 direction and index");
    expect(frame[5] & 1u, "cmd end bit");
    x4pro_sd_command(8, 0x1AAu, frame);
    expect(frame[0] == 0x48 && frame[4] == 0xAA, "cmd8");

    size_t length = 0;
    const uint8_t *update = x4pro_ssd1677_full_update(&length);
    expect(length == 5 && update[2] == 0x22 && update[3] == 0xF7 && update[4] == 0x20, "full update");
    if (failures) return 1;
    puts("x4pro protocol: PASS");
    return 0;
}

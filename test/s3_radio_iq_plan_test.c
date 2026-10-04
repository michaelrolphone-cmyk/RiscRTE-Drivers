#include <assert.h>
#include <stdio.h>
#include "../Drivers/s3_radio_iq_v1/lo_plan.h"

int main(void) {
    struct esp32s3_lo_plan plan;
    assert(esp32s3_plan_lo(2440000000u, ESP32S3_LO_AUTO, &plan));
    assert(plan.mode == ESP32S3_LO_NORMAL);
    assert(plan.sdm_word != 0);
    assert(!esp32s3_plan_lo(1000u, ESP32S3_LO_AUTO, &plan));
    assert(esp32s3_plan_lo(2000000000u, ESP32S3_LO_AUTO, &plan));
    assert(plan.mode == ESP32S3_LO_5_6);
    printf("s3 radio iq plan ok\n");
    return 0;
}

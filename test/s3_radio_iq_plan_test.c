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
    assert(!esp32s3_plan_lo(2440000000u, ESP32S3_LO_AUTO, NULL));
    assert(!esp32s3_plan_lo(2440000000u, (enum esp32s3_lo_mode)3, &plan));
    const uint32_t bounds[]={ESP32S3_LO_MIN_HZ,2000000000u,ESP32S3_PLL_MIN_HZ-1,
        ESP32S3_PLL_MIN_HZ,2325000000u,2440000000u,ESP32S3_PLL_MAX_HZ};
    for(unsigned i=0;i<sizeof(bounds)/sizeof(bounds[0]);++i){
        uint32_t hz=bounds[i];assert(esp32s3_plan_lo(hz,ESP32S3_LO_AUTO,&plan));
        assert(plan.mode==(hz<ESP32S3_PLL_MIN_HZ?ESP32S3_LO_5_6:ESP32S3_LO_NORMAL));
        int64_t error=(int64_t)plan.lo_hz-hz;
        assert(error>=-229 && error<=229); /* half a normal PLL frequency step */
        assert(plan.pll_hz>=ESP32S3_PLL_MIN_HZ-229 && plan.pll_hz<=ESP32S3_PLL_MAX_HZ+229);
    }
    assert(!esp32s3_plan_lo(ESP32S3_LO_MIN_HZ-1, ESP32S3_LO_AUTO, &plan));
    assert(!esp32s3_plan_lo(ESP32S3_PLL_MAX_HZ+1, ESP32S3_LO_AUTO, &plan));
    assert(!esp32s3_plan_lo(ESP32S3_PLL_MIN_HZ-1, ESP32S3_LO_NORMAL, &plan));
    assert(!esp32s3_plan_lo(ESP32S3_LO_5_6_MAX_HZ+1, ESP32S3_LO_5_6, &plan));
    printf("s3 radio iq plan ok\n");
    return 0;
}

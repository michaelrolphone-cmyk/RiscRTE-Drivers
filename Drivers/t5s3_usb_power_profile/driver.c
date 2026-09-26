/* T5S3 electrical policy only. Register encodings and chip ownership belong
 * to the reusable BQ25896 ELF. Never import this into generic firmware. */
#include "RiscBq25896ProfileV1.h"

static const risc_bq25896_profile_api_v1 profile = {
    RISC_BQ25896_PROFILE_API_V1, sizeof(risc_bq25896_profile_api_v1),
    500u, 5126u, 1200u, 80u, 500u, 250u, 200u
};
static bool start(const risc_provider_dependency_v1 *deps, size_t count) {
    (void)deps;
    return count == 0u;
}
static bool quiesce(void) { return true; }
static void stop(void) {}
static const risc_driver_v2 driver = {
    RISC_PROVIDER_DRIVER_ABI_V2, sizeof(risc_driver_v2),
    "t5s3-usb-power-profile", RISC_BQ25896_PROFILE_CAPABILITY,
    RISC_BQ25896_PROFILE_API_V1, &profile, start, stop, quiesce
};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &driver : NULL;
}

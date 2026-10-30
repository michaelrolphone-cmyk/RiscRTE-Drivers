/* Electrical values recovered from Reader9c8783 TPS65185 source. The chip ELF
 * owns register operations; this explicitly selected board profile owns data. */
#include "RiscProviderV2.h"
#include "RiscTps65185ProfileV1.h"
static const risc_tps65185_profile_api_v1 profile={1,sizeof(profile),0x3b00,0xc000,0x2000,0x4000,1600,0};
static bool start(const risc_provider_dependency_v1*d,size_t n){(void)d;return n==0;}
static bool quiesce(void){return true;}
static void stop(void){}
static const risc_driver_v2 driver={2,sizeof(driver),"t5s3-panel-profile",RISC_TPS65185_PROFILE_CAPABILITY,1,&profile,start,stop,quiesce};
__attribute__((visibility("default")))
const risc_driver_v2*t5_driver_get(uint32_t abi){return abi==2?&driver:NULL;}

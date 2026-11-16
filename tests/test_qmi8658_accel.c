#include "hardware_model.h"
#include "../Drivers/qmi8658_accel/driver.c"
static tw_hw_i2c_device_v1 selected;
static bool fail_release,fail_claim,retain_claim,stuck_clock;
static bool release_(void*c,uint64_t t){return !fail_release&&bus_release_(c,t);}
static bool claim_(void*c,uint8_t a,uint64_t*t){if(fail_claim&&!retain_claim){*t=0;return false;}bool ok=bus_claim_(c,a,t);return ok&&!fail_claim;}
static void delay_(void*c,uint32_t n){if(!stuck_clock)sleep_(c,n);}
static void setup(uint8_t one,uint8_t seven){fail_release=fail_claim=retain_claim=stuck_clock=false;fail_transaction_=0;assert(quiesce());no_claims_();reset_model_();bus_.claim_device=claim_;bus_.release_device=release_;clock_.sleep_ms=delay_;hardware_.compatible="qst,qmi8658";hardware_.config_type="peripheral.i2c";selected=(tw_hw_i2c_device_v1){.struct_size=sizeof(selected),.bus=i2c_config_,.address=0x6b,.chip_id=5,.irq=-1};hardware_.config=&selected;hardware_.config_size=sizeof(selected);registers_[0]=5;registers_[2]=one;registers_[3]=0x23;registers_[8]=seven;registers_[0x2e]=1;}
int main(void){
 risc_provider_dependency_v1 dep[]={deps_[0],deps_[1],deps_[3]};
 for(unsigned a=0;a<128;a++)for(unsigned b=0;b<256;b++){
  setup(a,b);
  if(a&1){assert(!start(dep,3)&&!writes_&&!bus_token_&&!running);assert(quiesce());assert(registers_[2]==a&&registers_[3]==0x23&&registers_[8]==b);continue;}
  assert(start(dep,3));assert(registers_[2]==((a&~0x60)|0x60)&&registers_[3]==0x16&&registers_[8]==(b|1)&&tick_>=31&&!bus_token_);
  registers_[2]^=0x1f;registers_[8]^=0xfe;
  assert(quiesce());assert(registers_[2]==(((a&~0x60)^0x1f)|(a&0x60))&&registers_[3]==0x23&&registers_[8]==(b^0xfe));no_claims_();
 }
 setup(0,0);assert(start(dep,3));twatch_accel_sample_v1 s;
 for(int v=-32768;v<=32767;v++){
  for(unsigned axis=0;axis<3;axis++){uint16_t raw=(uint16_t)(int16_t)(axis==1?-v-1:v);registers_[0x35+axis*2]=raw;registers_[0x36+axis*2]=raw>>8;}
  assert(read_sample(NULL,&s));assert(s.x==v*1000/8192&&s.y==(-v-1)*1000/8192&&s.z==s.x);
 }
 assert(quiesce());
 for(unsigned fault=1;fault<=21;fault++){
  setup(0,0);fail_transaction_=fault;bool opened=start(dep,3);fail_transaction_=0;assert(quiesce());assert(registers_[2]==0&&registers_[3]==0x23&&registers_[8]==0);(void)opened;
 }
 for(unsigned fault=1;fault<=4;fault++){
  setup(0,0);assert(start(dep,3));transactions_=0;fail_transaction_=fault;s=(twatch_accel_sample_v1){123,456,789};assert(!read_sample(NULL,&s)&&s.x==123&&s.y==456&&s.z==789);fail_transaction_=0;assert(quiesce());
 }
 setup(0,0);stuck_clock=true;assert(!start(dep,3));stuck_clock=false;assert(quiesce());
 setup(0,0);fail_release=true;assert(!start(dep,3)&&token);unsigned calls=transactions_;assert(!read_sample(NULL,&s)&&calls==transactions_&&!quiesce());fail_release=false;assert(quiesce());
 setup(0,0);fail_claim=retain_claim=true;assert(!start(dep,3)&&token);fail_claim=retain_claim=false;assert(quiesce());
 setup(0,0);registers_[0]=0;assert(!start(dep,3)&&!writes_&&quiesce());setup(0,0);selected.address=0x34;assert(!start(dep,3)&&!transactions_&&quiesce());
 assert(!api.prepare_wake&&!api.tap_info&&api.struct_size==TWATCH_MOTION_SAMPLE_SIZE);
 puts("PASS QMI8658:16384 active owned-mask cases,16384 inherited-disabled rejections,65536 signed mg cases, per-operation faults, clock stalls and retained cleanup; no gyro/IRQ/reset/PMIC/wake");
}

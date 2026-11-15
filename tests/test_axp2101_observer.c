#include "hardware_model.h"
#include "../Drivers/axp2101_observer/driver.c"
static tw_hw_i2c_device_v1 selected;
static bool fail_release,fail_claim,retain_claim,zero_claim;
static bool claim(void*c,uint8_t address,uint64_t*out){if(zero_claim){*out=0;return true;}if(fail_claim&&!retain_claim){*out=0;return false;}bool ok=bus_claim_(c,address,out);return ok&&!fail_claim;}
static bool release(void*c,uint64_t value){return !fail_release&&bus_release_(c,value);}
static void setup(void){fail_release=false;stop();no_claims_();reset_model_();fail_claim=retain_claim=zero_claim=false;bus_.claim_device=claim;bus_.release_device=release;hardware_.compatible="x-powers,axp2101";hardware_.config_type="peripheral.i2c";selected=(tw_hw_i2c_device_v1){.struct_size=sizeof(selected),.bus=i2c_config_,.address=0x34,.chip_id=0x4a,.irq=-1};hardware_.config=&selected;hardware_.config_size=sizeof(selected);registers_[3]=0x4a;registers_[0]=0x28;registers_[1]=0x20;registers_[0x18]=0x0a;registers_[0x68]=1;registers_[0x34]=0x10;registers_[0x35]=0x04;registers_[0xa4]=42;}
static bool open(void){return start(deps_,2);}
int main(void){
 risc_battery_sample_v1 value;
 for(unsigned pct=0;pct<256;pct++)for(unsigned gate=0;gate<6;gate++){
  setup();registers_[0xa4]=pct;
  if(gate==1)registers_[0]&=~8;
  if(gate==2)registers_[0x68]=0;
  if(gate==3)registers_[0x18]&=~8;
  if(gate==4)registers_[0x17]=4;
  if(gate==5)registers_[0xa2]=1;
  uint8_t before[256];memcpy(before,registers_,256);
  assert(open());assert(read_sample(NULL,&value));assert(value.millivolts==4100);
  bool valid=gate==0&&pct<=100;assert(value.percent==(valid?pct:255));
  assert(((value.flags&RISC_BATTERY_PROFILE_MISSING)==0)==valid);
  assert(!writes_&&!memcmp(before,registers_,256));stop();no_claims_();
 }
 for(unsigned n=1;n<=7;n++){
  setup();fail_transaction_=n;assert(!open());assert(value.percent==255||value.percent<=100);stop();no_claims_();
  setup();assert(open());fail_transaction_=transactions_+n;value=(risc_battery_sample_v1){4200,99,255};assert(!read_sample(NULL,&value));assert(value.millivolts==0&&value.percent==255&&value.flags==RISC_BATTERY_PROFILE_MISSING);fail_transaction_=0;assert(read_sample(NULL,&value));assert(!writes_);stop();no_claims_();
 }
 setup();assert(open());fail_release=true;assert(!read_sample(NULL,&value));unsigned io=transactions_;assert(!read_sample(NULL,&value)&&transactions_==io);assert(!quiesce()&&token);fail_release=false;assert(quiesce());stop();no_claims_();
 for(unsigned retained=0;retained<2;retained++){setup();fail_claim=true;retain_claim=retained;assert(!open());io=transactions_;assert(!read_sample(NULL,&value)&&transactions_==io);stop();no_claims_();}
 setup();zero_claim=true;assert(!open());stop();no_claims_();
 /* A different scoped PMIC function can legitimately own this address.
  * A rejected claim with no token transfers no custody and must be retryable. */
 setup();assert(open());uint64_t other=0;assert(bus_claim_(NULL,0x34,&other));
 assert(!read_sample(NULL,&value)&&!token);assert(bus_release_(NULL,other));
 assert(read_sample(NULL,&value)&&value.percent==42);stop();no_claims_();
 setup();selected.address=0x35;assert(!open()&&!transactions_);stop();
 setup();selected.chip_id=0;assert(!open()&&!transactions_);stop();
 setup();registers_[3]=0;assert(!open());stop();no_claims_();
 puts("AXP2101 read-only observer:1536 SOC/validity combinations, per-read faults, stale clearing, false/zero claims and retained releases PASS");
}

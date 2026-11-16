#include "hardware_model.h"
#include "../Drivers/axp2101_power_key/driver.c"
static risc_hw_i2c_function_v1 selected;
static bool fail_release;
static bool release(void*c,uint64_t token){return !fail_release&&bus_release_(c,token);}
static bool transaction(void*c,uint64_t token,const uint8_t*tx,size_t n,uint8_t*rx,size_t count,uint32_t timeout){
 uint8_t before=registers_[0x49];assert((rx&&n==1&&count==1)||(!rx&&n==2&&(tx[0]==0x41||tx[0]==0x49)));
 if(!rx&&tx[0]==0x49)assert(!(tx[1]&~0x0c));
 bool ok=transact_(c,token,tx,n,rx,count,timeout);if(ok&&!rx&&tx[0]==0x49)registers_[0x49]=before&~tx[1];return ok;
}
static void setup(uint8_t mask){fail_release=false;stop();no_claims_();reset_model_();bus_.transact=transaction;bus_.release_device=release;hardware_.compatible="x-powers,axp2101-power-key";hardware_.config_type="peripheral.i2c-function";selected=(risc_hw_i2c_function_v1){.struct_size=sizeof(selected),.bus=i2c_config_,.parent_instance_id=4,.function_id=2,.address=0x34,.chip_id=0x4a};hardware_.config=&selected;hardware_.config_size=sizeof(selected);registers_[3]=0x4a;registers_[0x41]=mask;registers_[0x49]=0xff;}
int main(void){
 for(unsigned mask=0;mask<256;mask++){
  setup(mask);assert(start(deps_,2));assert(registers_[0x41]==(mask|12));assert(registers_[0x49]==0xf3);
  for(unsigned status=0;status<256;status++){
   registers_[0x49]=status;uint32_t got=99;assert(events(NULL,&got));assert(got==((status&8?RISC_POWER_KEY_SHORT_PRESS:0)|(status&4?RISC_POWER_KEY_LONG_PRESS:0)));assert(registers_[0x49]==(status&~12));assert(!session);
  }
  // A concurrent change to unowned enable bits survives this function's close.
  registers_[0x41]^=0x80;assert(quiesce());assert(registers_[0x41]==(mask^0x80));stop();no_claims_();
 }
 for(unsigned fail=1;fail<=5;fail++){setup(0xa1);fail_transaction_=fail;assert(!start(deps_,2));fail_transaction_=0;assert(quiesce());stop();no_claims_();}
 for(unsigned fail=1;fail<=2;fail++){setup(0x83);assert(start(deps_,2));registers_[0x49]=0xfc;fail_transaction_=transactions_+fail;uint32_t got=99;assert(!events(NULL,&got)&&!got);fail_transaction_=0;assert(quiesce());stop();no_claims_();}
 setup(0x81);assert(start(deps_,2));fail_release=true;uint32_t got;assert(!events(NULL,&got)&&session);assert(!quiesce());fail_release=false;assert(quiesce());stop();no_claims_();
 /* Scoped functions share 0x34. Ordinary rejection has no retained custody. */
 setup(0x81);assert(start(deps_,2));uint64_t other=0;assert(bus_claim_(NULL,0x34,&other));
 assert(!events(NULL,&got)&&!got&&!session);assert(bus_release_(NULL,other));
 registers_[0x49]=8;assert(events(NULL,&got)&&got==RISC_POWER_KEY_SHORT_PRESS);
 stop();no_claims_();
 setup(0);selected.function_id=1;assert(!start(deps_,2)&&!transactions_);stop();
 puts("PASS PMIC PWRON:65536 status/mask cases, W1C ownership, enable restore, faults and retained cleanup; no charger/rail/GPIO0 access");
}

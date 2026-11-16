/* PMIC PWRON polling, not GPIO0. Primary source:
 * curisama/The-Badge@76172dfa main/port_esp.c lines1277..1407.
 * Own ONLY IRQ enable/status bits0x0c. Never copy vendor charger policy,
 * acknowledge unrelated status, change long-hold shutdown policy, or cut power.
 */
#include "DriverSupport.h"
#include "RiscI2cFunctionV1.h"
#include "RiscPowerKeyV1.h"
static const risc_i2c_bus_api_v1*bus;
static const risc_hw_i2c_function_v1*function;
static uint64_t session;
static uint8_t saved_mask;
static bool active,closing,mask_owned;
static bool read8(uint8_t address,uint8_t*out){return bus->transact(bus->context,session,&address,1,out,1,20);}
static bool write8(uint8_t address,uint8_t value){uint8_t bytes[]={address,value};return bus->transact(bus->context,session,bytes,2,NULL,0,20);}
static bool release_session(void){if(!session)return true;if(!bus->release_device(bus->context,session)){closing=true;return false;}session=0;return true;}
/* A rejected zero-token claim does not acquire custody and can be retried. */
static bool claim_session(void){if(session)return false;bool ok=bus->claim_device(bus->context,function->address,&session);if(!ok||!session){if(ok||session)closing=true;return false;}return true;}
static bool events(void*c,uint32_t*out){
 (void)c;if(!out)return false;*out=0;if(!active||closing||!claim_session())return false;
 uint8_t status=0;bool ok=read8(0x49,&status);uint8_t hit=status&0x0c;
 if(ok&&hit)ok=write8(0x49,hit); // W1C: acknowledge only this function's observed bits.
 bool closed=release_session();if(!ok||!closed){closing=true;return false;}
 if(hit&8)*out|=RISC_POWER_KEY_SHORT_PRESS;if(hit&4)*out|=RISC_POWER_KEY_LONG_PRESS;return true;
}
static bool quiesce(void){
 closing=true;if(!bus)return true;
 if(mask_owned){
  if(!session&&!claim_session())return false;
  uint8_t mask=0,check=0;
  if(!read8(0x41,&mask)||!write8(0x41,(uint8_t)((mask&~0x0c)|saved_mask))||!read8(0x41,&check)||(check&0x0c)!=saved_mask)return false;
  mask_owned=false;
 }
 return release_session();
}
static void stop(void){if(!quiesce())return;active=closing=false;bus=NULL;function=NULL;}
static bool start(const risc_provider_dependency_v1*deps,size_t count){
 if(bus||session||active||closing||mask_owned)return false;
 const risc_hw_i2c_function_v1*f=tw_config(deps,count,"x-powers,axp2101-power-key","peripheral.i2c-function",sizeof(*f));
 const risc_i2c_bus_api_v1*b=tw_dep(deps,count,"i2c.bus",sizeof(*b));
 if(!f||!f->parent_instance_id||f->function_id!=2||f->address!=0x34||f->chip_id!=0x4a||f->reserved||!tw_bus_limit(&f->bus,RISC_HW_BUS_I2C,400000)||!tw_i2c_valid(b))return false;
 function=f;bus=b;if(!claim_session())return false;
 uint8_t id=0,mask=0,check=0;
 bool ok=read8(3,&id)&&id==0x4a&&read8(0x41,&mask);
 if(ok){saved_mask=mask&0x0c;mask_owned=true;ok=write8(0x41,(uint8_t)(mask|0x0c))&&read8(0x41,&check)&&(check&0x0c)==0x0c&&write8(0x49,0x0c);}
 bool closed=release_session();active=ok&&closed;if(!active)closing=true;return active;
}
static const risc_power_key_api_v1 api={1,sizeof(api),NULL,events};
static const risc_driver_v2 driver={2,sizeof(driver),"axp2101-power-key","input.power-key.raw",1,&api,start,stop,quiesce};
__attribute__((visibility("default")))const risc_driver_v2*t5_driver_get(uint32_t abi){return abi==2?&driver:NULL;}

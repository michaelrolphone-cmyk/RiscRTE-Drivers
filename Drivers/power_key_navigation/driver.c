/* Transport-neutral mapping. No PMIC, GPIO, board identity or borrowed claims. */
#include "RiscProviderV2.h"
#include "RiscPowerKeyV1.h"
#include "RiscInputNavigationV1.h"
#include <string.h>
static const risc_power_key_api_v1*key;
static uint32_t previous;
static bool blocked,neutral;
static bool reset(void*c){(void)c;previous=0;neutral=false;return key!=NULL;}
static bool poll(void*c,risc_input_navigation_frame_v1*out){
 (void)c;if(!out)return false;*out=(risc_input_navigation_frame_v1){0};if(!key)return false;
 if(blocked)return true;
 uint32_t events=0;if(!key->events(key->context,&events)){neutral=false;previous=0;return false;}
 if(!neutral){if(!events)neutral=true;return true;}
 uint32_t now=(events&RISC_POWER_KEY_LONG_PRESS)?RISC_NAV_HOME:(events&RISC_POWER_KEY_SHORT_PRESS)?RISC_NAV_BACK:0;
 /* Each consumed event is a distinct pulse, even on consecutive polls.
  * A frame may release the preceding pulse and press a new one of the same key. */
 *out=(risc_input_navigation_frame_v1){now,now,previous};previous=now;return true;
}
static bool foreground(void*c,const risc_input_foreground_v1*claims,size_t count){
 (void)c;blocked=true;previous=0;neutral=false;
 if(count>RISC_INPUT_NAVIGATION_MAX_FOREGROUND||(count&&!claims))return false;
 bool overlap=false;for(size_t i=0;i<count;i++){if(!claims[i].capability||!claims[i].api_version)return false;if(!strcmp(claims[i].capability,RISC_POWER_KEY_CAPABILITY))overlap=true;}
 blocked=overlap;return true;
}
static bool start(const risc_provider_dependency_v1*d,size_t count){if(key||!d||count!=1||!d[0].capability_id||strcmp(d[0].capability_id,RISC_POWER_KEY_CAPABILITY)||d[0].api_version!=1||!d[0].api)return false;const risc_power_key_api_v1*k=d[0].api;if(k->api_version!=1||k->struct_size<sizeof(*k)||!k->events)return false;key=k;blocked=false;neutral=false;previous=0;return true;}
static bool quiesce(void){return true;}
static void stop(void){key=NULL;blocked=false;neutral=false;previous=0;}
static const risc_input_navigation_api_v1 api={1,sizeof(api),NULL,poll,foreground,reset};
static const risc_driver_v2 driver={2,sizeof(driver),"power-key-navigation","input.navigation",1,&api,start,stop,quiesce};
__attribute__((visibility("default")))const risc_driver_v2*t5_driver_get(uint32_t abi){return abi==2?&driver:NULL;}

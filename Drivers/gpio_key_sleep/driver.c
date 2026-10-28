/* Single deliberately selected physical key. No default timer, touch, motion, PMIC,
 * rail or charger authority. Runtime owns all native quiescence and RTC entry. */
#include "DriverSupport.h"
#include "GardenPlatformV1.h"
#include "RiscPowerSleepV1.h"
#include "RiscPowerSleepGuardV1.h"
static const garden_gpio_v1 *gpio;
static const risc_power_sleep_guard_v1 *guard;
static const risc_platform_clock_api_v1 *clock_api;
static const risc_hw_gpio_bank_v1 *config;
static uint64_t token;
static bool prepared, retained, admitted;
static bool pending(void *context,bool *out){
 (void)context;if(!out)return false;*out=true;
 if(!admitted||!gpio||!token||retained)return false;
 bool level=false;if(!gpio->read(gpio->context,token,&level))return false;
 *out=level==!!config->active_high;return true;
}
static bool prepare(void *context){
 (void)context;prepared=false;bool active=true;
 /* Retained custody forbids even a cached guard/clock callback. */
 if(!admitted||retained||!gpio||!token||!guard||!clock_api||!config)return false;
 if(!guard->ready(guard->context)||!pending(NULL,&active)||active)return false;
 /* sleep_ms yields but does not promise a minimum elapsed duration. Measure
  * the release interval; a tick-relative delay may return just short of it.
  * Integer-ms observation may also remain unchanged after a sub-ms yield.
  * A bounded requested-time budget permits that quantization without spinning
  * forever on a stalled clock. Never wait for an already-held key. */
 const uint32_t delay=(config->debounce_us+999u)/1000u;
 const uint64_t before=clock_api->monotonic_ms(clock_api->context);
 if(before==UINT64_MAX)return false;
 uint64_t last=before;
 uint32_t remaining=delay,budget=2u*delay+2u;
 while(budget){
  const uint32_t wait=remaining<budget?remaining:budget;
  clock_api->sleep_ms(clock_api->context,wait);budget-=wait;
  const uint64_t after=clock_api->monotonic_ms(clock_api->context);
  if(after==UINT64_MAX||after<last)return false;
  if(!guard->ready(guard->context)||!pending(NULL,&active)||active)return false;
  const uint64_t elapsed=after-before;
  if(elapsed>=delay){prepared=true;return true;}
  remaining=delay-(uint32_t)elapsed;last=after;
 }
 return false;
}
static bool resume(void *context){(void)context;prepared=false;return admitted&&gpio&&token&&!retained;}
static int32_t enter_deep(void *context){
 (void)context;if(retained)return RISC_DEEP_SLEEP_RETAINED;
 if(!prepared)return RISC_DEEP_SLEEP_INVALID;
 if(!guard->ready(guard->context))return RISC_DEEP_SLEEP_BUSY;
 bool active=true;if(!pending(NULL,&active))return RISC_DEEP_SLEEP_PLATFORM;
 if(active)return RISC_DEEP_SLEEP_ACTIVE_WAKE;
 int32_t rc=gpio->deep_sleep(gpio->context,token,!!config->active_high);
 /* Success is terminal. A backend that returns success is not recoverable. */
 if(rc>=0||rc==RISC_DEEP_SLEEP_RETAINED){retained=true;return RISC_DEEP_SLEEP_RETAINED;}
 return rc;
}
static int32_t enter_light(void *context,risc_light_sleep_result_v1 *out){
 (void)context;if(retained)return RISC_LIGHT_SLEEP_RETAINED;
 if(!prepared)return RISC_LIGHT_SLEEP_INVALID;
 if(!guard->ready(guard->context))return RISC_LIGHT_SLEEP_BUSY;
 int32_t rc=gpio->light_sleep(gpio->context,token,!!config->active_high,out);
 if(rc==RISC_LIGHT_SLEEP_RETAINED)retained=true;return rc;
}
/* Timed Light implements the shared staged policy; timed Deep preserves
 * earlier alarm deadlines. Untimed entry never arms a timer implicitly. */
static int32_t light_for(void *context,uint32_t ms,risc_light_sleep_result_v1 *out){
 (void)context;if(retained)return RISC_LIGHT_SLEEP_RETAINED;
 if(!prepared||!ms||ms>RISC_TIMED_SLEEP_MAX_MS)return RISC_LIGHT_SLEEP_INVALID;
 if(!guard->ready(guard->context))return RISC_LIGHT_SLEEP_BUSY;
 if(gpio->struct_size<GARDEN_GPIO_LIGHT_SLEEP_FOR_V1_SIZE||!gpio->light_sleep_for)return RISC_LIGHT_SLEEP_UNSUPPORTED;
 int32_t rc=gpio->light_sleep_for(gpio->context,token,!!config->active_high,ms,out);
 if(rc==RISC_LIGHT_SLEEP_RETAINED)retained=true;return rc;
}
static int32_t deep_for(void *context,uint32_t ms){
 (void)context;if(retained)return RISC_DEEP_SLEEP_RETAINED;
 if(!prepared||!ms||ms>RISC_TIMED_SLEEP_MAX_MS)return RISC_DEEP_SLEEP_INVALID;
 if(!guard->ready(guard->context))return RISC_DEEP_SLEEP_BUSY;
 if(gpio->struct_size<GARDEN_GPIO_DEEP_SLEEP_FOR_V1_SIZE||!gpio->deep_sleep_for)return RISC_DEEP_SLEEP_UNSUPPORTED;
 int32_t rc=gpio->deep_sleep_for(gpio->context,token,!!config->active_high,ms);
 if(rc>=0||rc==RISC_DEEP_SLEEP_RETAINED){retained=true;return RISC_DEEP_SLEEP_RETAINED;}return rc;
}
static bool quiesce(void){
 admitted=prepared=false;if(retained)return false;
 if(token){if(!gpio||!gpio->release(gpio->context,token))return false;token=0;}
 return true;
}
static void stop(void){if(!quiesce())return;gpio=NULL;guard=NULL;clock_api=NULL;config=NULL;}
static bool start(const risc_provider_dependency_v1 *deps,size_t count){
 if(gpio||token||retained)return false;
 const risc_hw_gpio_bank_v1 *c=tw_config(deps,count,"riscrte,gpio-key-sleep","gpio.bank",sizeof(*c));
 const risc_power_sleep_guard_v1 *v=tw_dep(deps,count,"power.sleep-guard",sizeof(*v));
 const garden_gpio_v1 *g=tw_dep(deps,count,"platform.gpio",GARDEN_GPIO_DEEP_SLEEP_V1_SIZE);
 const risc_platform_clock_api_v1 *k=tw_dep(deps,count,"platform.clock",sizeof(*k));
 if(!c||c->count!=1||!tw_pin(c->pins[0])||c->active_high>1||c->pull_up>1||c->reserved||
    c->debounce_us<1000||c->debounce_us>100000||c->long_press_us||c->click_min_us||
    !v||!v->ready||!v->ready(v->context)||!g||!g->claim||!g->read||!g->release||!g->deep_sleep||!g->light_sleep||!tw_clock_valid(k))return false;
 gpio=g;guard=v;clock_api=k;config=c;
 bool ok=g->claim(g->context,(uint8_t)c->pins[0],false,false,!!c->pull_up,&token);
 if(!ok||!token)return false;
 admitted=true;return true;
}
static const risc_power_sleep_api_v1 api={.api_version=1,.struct_size=sizeof(api),
 .prepare_sleep=prepare,.resume=resume,.light_sleep=enter_light,.deep_sleep=enter_deep,
 .light_sleep_for=light_for,.sleep_wake_pending=pending,.deep_sleep_for=deep_for};
static const risc_driver_v2 driver={2,sizeof(driver),"gpio-key-sleep",RISC_POWER_SLEEP_CAPABILITY,1,&api,start,stop,quiesce};
__attribute__((visibility("default")))const risc_driver_v2*t5_driver_get(uint32_t abi){return abi==2?&driver:NULL;}

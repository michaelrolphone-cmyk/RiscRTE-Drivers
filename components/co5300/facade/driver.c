/* Shared CO5300 display facade backed by the device-linked native component. */
#include "DriverSupport.h"
#include "RiscDisplayBackendV1.h"
#include "RiscDisplaySpiV2.h"
#include "RiscCo5300Profile.h"
#include "RiscPowerRailV1.h"
#include "ProviderLog.h"
#include "GardenPlatformV1.h"
#include "twatch_power.h"
#define TWATCH_PANEL_POWER 1
#define WIDTH RISC_CO5300_WIDTH
#define HEIGHT RISC_CO5300_HEIGHT
#define STRIDE (WIDTH*2u)
#define DISPLAY_SAFE_INSETS {RISC_CO5300_SAFE_TOP, RISC_CO5300_SAFE_RIGHT, RISC_CO5300_SAFE_BOTTOM, RISC_CO5300_SAFE_LEFT}
#define FORMAT RISC_DISPLAY_FORMAT_RGB565
#define DISPLAY_FLAGS (RISC_DISPLAY_INFO_ASYNC_PRESENT|RISC_DISPLAY_INFO_BRIGHTNESS)
#define DISPLAY_ID "co5300-display"
static const risc_platform_clock_api_v1* timer;
static const risc_display_backend_v1* backend;
static uint64_t backend_token;
static uint32_t spi_timeout_ms;
static uint8_t brightness=80;
static bool suspended,reset_held,reset_retained;
static const garden_gpio_v1 *reset_gpio;
static int16_t reset_pin;
static uint64_t reset_token;
static bool release_reset(void){
 if(reset_retained)return false;
 if(!reset_token)return true;
 if(reset_held){
  int32_t rc=reset_gpio->deep_sleep_hold(reset_gpio->context,reset_token,false);
  if(rc){reset_retained=true;return false;}
  reset_held=false;
 }
 if(!reset_gpio->release(reset_gpio->context,reset_token)){reset_retained=true;return false;}
 reset_token=0;return true;
}
static bool hw_start(const risc_provider_dependency_v1* d,size_t n){
 const risc_hw_spi_display_v2 *config=tw_config_version(d,n,"chipone,co5300","display.spi",sizeof(*config),2);
 if(backend_token || reset_token || reset_retained || !config || config->base.reset<0 || config->base.reset>48 || config->base.reset_active_high)return false;
 reset_pin=config->base.reset;
 reset_gpio=tw_dep(d,n,"platform.gpio",GARDEN_GPIO_DEEP_SLEEP_HOLD_V1_SIZE);
 if(!reset_gpio || !reset_gpio->claim || !reset_gpio->release || !reset_gpio->deep_sleep_hold)return false;
 timer=tw_dep(d,n,"platform.clock",sizeof(*timer));
 backend=tw_dep(d,n,RISC_DISPLAY_BACKEND_CAPABILITY,sizeof(*backend));
 const risc_power_rail_api_v1* rail=tw_dep(d,n,RISC_POWER_RAIL_CAPABILITY,sizeof(*rail));
 if(!tw_clock_valid(timer) || !backend || !backend->open || !backend->row || !backend->brightness || !backend->close || !rail || !rail->get_info)return false;
 risc_power_rail_info_v1 info={.struct_size=sizeof(info)};
 // Observe the selected supply; this facade never changes a PMIC rail.
 if(!rail->get_info(rail->context,&info) || !info.configuration_verified || !info.enabled || info.configured_millivolts!=3300)return false;
 provider_log("PANEL_LINK transport=device-selected-co5300 backend=platform.display.backend");
 // Fresh deep wake retains the electrical pad hold, not the old token.
 // A scoped claim stages LOW before NativeHardware releases that old hold.
 if(!reset_gpio->claim(reset_gpio->context,reset_pin,true,false,false,&reset_token))return false;
 if(!reset_token || !release_reset())return false;
 suspended=false;brightness=80;return backend->open(backend->context,&backend_token);
}
static void hw_submit(void){}
static uint32_t hw_row_min_budget_ms(void){return 1;}
static bool hw_row(uint32_t y,const uint8_t* pixels){return backend->row(backend->context,backend_token,y,(const uint16_t*)pixels,WIDTH);}
static int hw_finish(void){return 1;}
static bool hw_brightness(uint16_t value,uint16_t maximum){
 if(!maximum || value>maximum)return false;
 uint8_t scaled=((uint32_t)value*255u+maximum/2u)/maximum;
 if(!backend->brightness(backend->context,backend_token,scaled))return false;
 brightness=scaled;return true;
}
static bool hw_stop(void){
 if(reset_token || reset_retained)return false;
 if(!backend_token)return true;
 if(!backend->close(backend->context,backend_token))return false;
 backend_token=0;return true;
}
#include "common/display.h"
// Close the native transport before CPU sleep: its ownership token is a
// deliberate native sleep barrier. Retain the framebuffer for rollback/wake.
static bool panel_prepare_sleep(void* c){
 (void)c;if(!enter())return false;
 bool ok=running&&!failed&&!held&&!active&&(!closing||suspended);
 if(ok){closing=suspended=true;ok=hw_stop();}
 leave();return ok;
}
static bool panel_resume(void* c){
 (void)c;if(!enter())return false;
 bool ok=running&&!failed&&!reset_retained&&(!closing||suspended);
 if(ok&&suspended){
  // A failed close/open can retain ownership. Complete that cleanup first.
  ok=release_reset();
  if(ok)ok=hw_stop();
  if(ok)ok=backend->open(backend->context,&backend_token);
  if(ok)ok=backend->brightness(backend->context,backend_token,0);
  for(uint32_t y=0;ok&&y<HEIGHT;++y)ok=hw_row(y,pixels+y*STRIDE);
  if(ok)ok=backend->brightness(backend->context,backend_token,brightness);
  if(ok){suspended=false;closing=false;}
  else (void)hw_stop();
 }
 leave();return ok;
}
/* Transfer reset ownership only after the native backend has released every
 * configured pad. The existing scoped GPIO hold fences CPU deep entry; rollback
 * unholds/releases it before reopening the vendor transport. */
static int32_t panel_prepare_deep_sleep(void* c){
 if(reset_retained)return RISC_DEEP_SLEEP_RETAINED;
 if(reset_held)return suspended?0:RISC_DEEP_SLEEP_PLATFORM;
 if(!panel_prepare_sleep(c))return RISC_DEEP_SLEEP_PLATFORM;
 if(!enter())return RISC_DEEP_SLEEP_BUSY;
 int32_t rc=RISC_DEEP_SLEEP_PLATFORM;
 if(reset_gpio->claim(reset_gpio->context,reset_pin,true,false,false,&reset_token)&&reset_token){
  rc=reset_gpio->deep_sleep_hold(reset_gpio->context,reset_token,true);
  if(rc==0)reset_held=true;
  else if(rc==RISC_DEEP_SLEEP_RETAINED)reset_retained=true;
  else if(!release_reset())rc=RISC_DEEP_SLEEP_RETAINED;
 }else if(reset_token){reset_retained=true;rc=RISC_DEEP_SLEEP_RETAINED;}
 leave();return rc;
}
static int32_t panel_resume_status(void* c){return panel_resume(c)?RISC_LIGHT_SLEEP_OK:reset_retained?RISC_LIGHT_SLEEP_RETAINED:RISC_LIGHT_SLEEP_PLATFORM;}

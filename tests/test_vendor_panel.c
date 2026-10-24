#include "DriverSupport.h"
#include "RiscCo5300Profile.h"
#ifndef TEST_DISPLAY_RESET
#define TEST_DISPLAY_RESET 8
#endif
#include "RiscDisplayBackendV1.h"
#include "GardenPlatformV1.h"
#include "RiscPowerRailV1.h"
#include "provider_log_mock.h"
#include <assert.h>
#include <stdio.h>
static uint64_t now;static unsigned opened,closed,rows,levels;
static bool open_ok=true,close_ok=true,row_ok=true,level_ok=true;
static uint8_t last_level;
static bool rail_enabled=true,rail_verified=true,rail_ok=true;
static uint32_t rail_mv=3300;
static bool backend_live,gpio_live,hold_live,release_ok=true;
static int32_t hold_result;
static bool claim_reset(void*c,uint8_t pin,bool output,bool initial,bool pullup,uint64_t*out){
 (void)c;assert(pin==TEST_DISPLAY_RESET&&output&&!initial&&!pullup&&!backend_live&&!gpio_live);gpio_live=true;*out=9;return true;
}
static bool release_gpio(void*c,uint64_t t){(void)c;assert(t==9&&gpio_live&&!hold_live);if(!release_ok)return false;gpio_live=false;return true;}
static int32_t hold_reset(void*c,uint64_t t,bool enabled){(void)c;assert(t==9&&gpio_live&&!backend_live);if(hold_result)return hold_result;hold_live=enabled;return 0;}
static garden_gpio_v1 reset_api={.api_version=1,.struct_size=sizeof(reset_api),.claim=claim_reset,.release=release_gpio,.deep_sleep_hold=hold_reset};
static uint64_t mono(void* c){(void)c;return now;}
static void delay(void*c,uint32_t ms){(void)c;now+=ms;}
static bool backend_open(void*c,uint64_t*t){(void)c;assert(!gpio_live&&!hold_live);++opened;*t=123;backend_live=true;return open_ok;}
static bool backend_row(void*c,uint64_t t,uint32_t y,const uint16_t*p,size_t n){(void)c;assert(t==123&&y<RISC_CO5300_HEIGHT&&p&&n==RISC_CO5300_WIDTH);if(!row_ok)return false;++rows;assert(p[0]==(uint16_t)(y*RISC_CO5300_WIDTH));++now;return true;}
static bool backend_level(void*c,uint64_t t,uint8_t v){(void)c;assert(t==123);++levels;last_level=v;return level_ok;}
static bool backend_close(void*c,uint64_t t){(void)c;assert(t==123);++closed;if(close_ok)backend_live=false;return close_ok;}
static bool rail_info(void*c,risc_power_rail_info_v1*i){(void)c;*i=(risc_power_rail_info_v1){.struct_size=sizeof(*i),.enabled=rail_enabled,.configuration_verified=rail_verified,.configured_millivolts=rail_mv};return rail_ok;}
static bool rail_write(void*c,bool b){(void)c;(void)b;assert(!"vendor baseline must not modify PMIC");return false;}
static risc_display_backend_v1 native={1,sizeof(native),NULL,backend_open,backend_row,backend_level,backend_close};
static risc_platform_clock_api_v1 clock_api={1,sizeof(clock_api),NULL,mono,delay};
static risc_power_rail_api_v1 rail={1,sizeof(rail),NULL,rail_info,rail_write};
#include "../components/co5300/facade/driver.c"
int main(void){
 risc_hw_spi_display_v2 cfg={.base={.struct_size=sizeof(cfg),.reset=TEST_DISPLAY_RESET}};
 risc_hardware_device_v1 hw={.api_version=1,.struct_size=sizeof(hw),.instance_id=5,.compatible="chipone,co5300",.revision="unspecified",.config_type="display.spi",.config_version=2,.config_size=sizeof(cfg),.config=&cfg};
 risc_provider_dependency_v1 deps[]={{"hardware.device",1,&hw},{"platform.clock",1,&clock_api},{RISC_DISPLAY_BACKEND_CAPABILITY,1,&native},{"power.rail",1,&rail},{"platform.gpio",1,&reset_api}};
 for(unsigned bad=0;bad<4;++bad){
  rail_enabled=bad!=0;rail_verified=bad!=1;rail_ok=bad!=2;rail_mv=bad==3?3200:3300;
  assert(!start(deps,5)&&!opened&&!gpio_live&&!backend_live);assert(quiesce());
 }
 rail_enabled=rail_verified=rail_ok=true;rail_mv=3300;
 assert(start(deps,5)&&opened==1&&backend_token==123);
 risc_display_info_v1 info; assert(api.base.get_info(NULL,&info));
 assert(info.width==RISC_CO5300_WIDTH && info.height==RISC_CO5300_HEIGHT);
 assert(info.safe_insets.top==RISC_CO5300_SAFE_TOP && info.safe_insets.right==RISC_CO5300_SAFE_RIGHT &&
        info.safe_insets.bottom==RISC_CO5300_SAFE_BOTTOM && info.safe_insets.left==RISC_CO5300_SAFE_LEFT);
 risc_display_surface_v1 surface;assert(api.base.acquire(NULL,FORMAT,&surface));
 assert(!panel_prepare_sleep(NULL));
 for(unsigned i=0;i<WIDTH*HEIGHT;++i)((uint16_t*)surface.pixels)[i]=(uint16_t)i;
 risc_display_present_token_v1 token;assert(api.base.submit(NULL,surface.frame,NULL,0,NULL,&token));
 assert(!panel_prepare_sleep(NULL));
 for(unsigned i=0;i<600&&active;++i)display_poll(8);
 risc_display_present_status_v1 status;assert(api.base.present_status(NULL,token,&status));
 assert(status.state==RISC_DISPLAY_PRESENT_COMPLETE&&rows==RISC_CO5300_HEIGHT);
 assert(api.base.set_brightness(NULL,40,100)&&last_level==102&&levels==1);
 for(unsigned cycle=0;cycle<20;++cycle){
  assert(panel_prepare_sleep(NULL)&&!backend_token&&suspended&&closing);
  assert(panel_prepare_sleep(NULL));
  assert(!api.base.acquire(NULL,FORMAT,&surface));
  assert(!api.base.set_brightness(NULL,1,100));
  assert(panel_resume(NULL)&&backend_token==123&&!suspended&&!closing&&last_level==102);
  unsigned n=opened;assert(panel_resume(NULL)&&opened==n);
 }
 close_ok=false;assert(!panel_prepare_sleep(NULL)&&backend_token==123&&suspended);
 assert(!panel_resume(NULL)&&backend_token==123&&closing);
 close_ok=true;assert(panel_resume(NULL)&&!closing);
 assert(panel_prepare_sleep(NULL));open_ok=false;
 assert(!panel_resume(NULL)&&!backend_token&&suspended&&closing);
 open_ok=true;assert(panel_resume(NULL));
 assert(panel_prepare_sleep(NULL));row_ok=false;
 assert(!panel_resume(NULL)&&!backend_token&&suspended&&closing);
 row_ok=true;assert(panel_resume(NULL));
 assert(panel_prepare_sleep(NULL));level_ok=false;
 assert(!panel_resume(NULL)&&!backend_token&&suspended&&closing);
 level_ok=true;assert(panel_resume(NULL)&&last_level==102);
 for(unsigned cycle=0;cycle<3;cycle++){
  assert(panel_prepare_deep_sleep(NULL)==0&&!backend_live&&gpio_live&&hold_live);
  assert(panel_prepare_deep_sleep(NULL)==0);
  assert(!quiesce()&&running&&gpio_live);
  assert(panel_resume_status(NULL)==0&&backend_live&&!gpio_live&&!hold_live);
 }
 hold_result=RISC_DEEP_SLEEP_PLATFORM;assert(panel_prepare_deep_sleep(NULL)==RISC_DEEP_SLEEP_PLATFORM&&!gpio_live);
 hold_result=0;assert(panel_resume(NULL));
 close_ok=false;assert(panel_prepare_deep_sleep(NULL)==RISC_DEEP_SLEEP_PLATFORM&&!gpio_live);close_ok=true;assert(panel_resume(NULL));
 close_ok=false;assert(!quiesce()&&backend_token==123&&running);
 close_ok=true;assert(quiesce()&&!backend_token&&!running);
 open_ok=false;assert(!start(deps,5)&&backend_token==123);assert(quiesce()&&!backend_token);
 open_ok=true;assert(start(deps,5));assert(panel_prepare_deep_sleep(NULL)==0);
 release_ok=false;assert(panel_resume_status(NULL)==RISC_LIGHT_SLEEP_RETAINED&&reset_retained&&gpio_live);
 assert(panel_prepare_deep_sleep(NULL)==RISC_DEEP_SLEEP_RETAINED);assert(!quiesce());
 puts("Vendor display facade: full RGB565 frame, generic brightness, PMIC read-only and retained cleanup PASS");
}

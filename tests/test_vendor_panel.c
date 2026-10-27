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
#include "provider_sync_mock.h"
static provider_sync_mock sync_model;
static const risc_provider_sync_api_v1 sync_service=PROVIDER_SYNC_MOCK_API(sync_model);
#include <stdio.h>
static uint32_t row_delay_ms=1;
static unsigned row_attempts,fail_row_at=~0u;
static uint64_t now;static unsigned opened,closed,rows,levels;
static bool open_ok=true,close_ok=true,row_ok=true,level_ok=true;
static uint8_t last_level,requested_level,visible_level;
static unsigned frame_rows;static bool frame_complete;
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
/* The native transport restarts dark. Brightness is requested state until a
 * complete checked frame, not a value inherited from the previous provider. */
static bool backend_open(void*c,uint64_t*t){
 (void)c;assert(!gpio_live&&!hold_live);++opened;*t=123;backend_live=true;
 requested_level=visible_level=0;frame_rows=0;frame_complete=false;return open_ok;
}
static bool backend_row(void*c,uint64_t t,uint32_t y,const uint16_t*p,size_t n){
 (void)c;assert(backend_live&&t==123&&y<RISC_CO5300_HEIGHT&&p&&n==RISC_CO5300_WIDTH);
 ++row_attempts;if(!row_ok||y==fail_row_at)return false;
 assert(!frame_complete&&y==frame_rows&&!visible_level);
 ++rows;++frame_rows;assert(p[0]==(uint16_t)(y*RISC_CO5300_WIDTH));now+=row_delay_ms;
 if(frame_rows==RISC_CO5300_HEIGHT){frame_complete=true;visible_level=requested_level;}
 return true;
}
static bool backend_level(void*c,uint64_t t,uint8_t v){
 (void)c;assert(backend_live&&t==123);++levels;if(!level_ok)return false;
 requested_level=last_level=v;if(frame_complete||!v)visible_level=v;
 else assert(!visible_level);return true;
}
static bool backend_close(void*c,uint64_t t){(void)c;assert(t==123);++closed;if(close_ok){backend_live=false;visible_level=0;}return close_ok;}
static bool rail_info(void*c,risc_power_rail_info_v1*i){(void)c;*i=(risc_power_rail_info_v1){.struct_size=sizeof(*i),.enabled=rail_enabled,.configuration_verified=rail_verified,.configured_millivolts=rail_mv};return rail_ok;}
static bool rail_write(void*c,bool b){(void)c;(void)b;assert(!"vendor baseline must not modify PMIC");return false;}
static risc_display_backend_v1 native={1,sizeof(native),NULL,backend_open,backend_row,backend_level,backend_close};
static risc_platform_clock_api_v1 clock_api={1,sizeof(clock_api),NULL,mono,delay};
static risc_power_rail_api_v1 rail={1,sizeof(rail),NULL,rail_info,rail_write};
#include "../components/co5300/facade/driver.c"
static void checked_frame(void){
 risc_display_surface_v1 surface;assert(api.base.acquire(NULL,FORMAT,&surface));
 for(unsigned i=0;i<WIDTH*HEIGHT;++i)((uint16_t*)surface.pixels)[i]=(uint16_t)i;
 risc_display_present_token_v1 token;assert(api.base.submit(NULL,surface.frame,NULL,0,NULL,&token));
 for(unsigned i=0;i<600&&active;++i)display_poll(8);
 risc_display_present_status_v1 status;assert(api.base.present_status(NULL,token,&status));
 assert(status.state==RISC_DISPLAY_PRESENT_COMPLETE&&frame_complete);
}
int main(void){
 risc_hw_spi_display_v2 cfg={.base={.struct_size=sizeof(cfg),.reset=TEST_DISPLAY_RESET}};
 risc_hardware_device_v1 hw={.api_version=1,.struct_size=sizeof(hw),.instance_id=5,.compatible="chipone,co5300",.revision="unspecified",.config_type="display.spi",.config_version=2,.config_size=sizeof(cfg),.config=&cfg};
 risc_provider_dependency_v1 deps[]={{"hardware.device",1,&hw},{"platform.clock",1,&clock_api},{RISC_DISPLAY_BACKEND_CAPABILITY,1,&native},{"power.rail",1,&rail},{"platform.gpio",1,&reset_api},{"platform.sync",1,&sync_service}};
 assert(!start(deps,5)); assert(quiesce());
 sync_model.foreign=true;assert(!start(deps,6));sync_model.foreign=false;
 sync_model.fail_create=true;assert(!start(deps,6));assert(quiesce());sync_model.fail_create=false;
 sync_model.zero_create=true;assert(!start(deps,6));assert(quiesce());sync_model.zero_create=false;
 sync_model.retain_create=true;assert(!start(deps,6)&&sync_model.live);assert(quiesce()&&!sync_model.live);sync_model.retain_create=false;
 for(unsigned bad=0;bad<4;++bad){
  rail_enabled=bad!=0;rail_verified=bad!=1;rail_ok=bad!=2;rail_mv=bad==3?3200:3300;
  assert(!start(deps,6)&&!opened&&!gpio_live&&!backend_live);assert(quiesce());
 }
 rail_enabled=rail_verified=rail_ok=true;rail_mv=3300;
 assert(start(deps,6)&&opened==1&&backend_token==123);
 assert(levels==1&&requested_level==80&&!visible_level&&!frame_complete);
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
 assert(status.state==RISC_DISPLAY_PRESENT_COMPLETE&&rows==RISC_CO5300_HEIGHT&&visible_level==80);
 assert(api.base.set_brightness(NULL,40,100)&&last_level==102&&visible_level==102&&levels==2);
 for(unsigned cycle=0;cycle<20;++cycle){
  assert(panel_prepare_sleep(NULL)&&!backend_token&&suspended&&closing);
  assert(panel_prepare_sleep(NULL));
  assert(!api.base.acquire(NULL,FORMAT,&surface));
  assert(!api.base.set_brightness(NULL,1,100));
  unsigned before_levels=levels;
  assert(panel_resume(NULL)&&backend_token==123&&!suspended&&!closing&&last_level==102&&visible_level==102);
  assert(levels==before_levels+2); /* dark request, every row, selected restore */
  unsigned n=opened;assert(panel_resume(NULL)&&opened==n);
 }
 assert(api.base.set_brightness(NULL,0,100)&&!requested_level&&!visible_level);
 assert(panel_prepare_sleep(NULL)&&panel_resume(NULL)&&!requested_level&&!visible_level);
 assert(api.base.set_brightness(NULL,40,100)&&visible_level==102);
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
 assert(!sync_model.live);
 for(unsigned cycle=0;cycle<3;++cycle){
  unsigned before_levels=levels;
  assert(start(deps,6)&&levels==before_levels+1&&requested_level==80&&!visible_level&&!frame_complete);
  if(cycle==1)assert(api.base.set_brightness(NULL,0,100)&&!requested_level&&!visible_level);
  checked_frame();uint8_t selected=cycle==1?0:80;assert(visible_level==selected);
  assert(panel_prepare_sleep(NULL)&&!visible_level);
  assert(panel_resume(NULL)&&visible_level==selected&&requested_level==selected);
  assert(quiesce()&&!backend_token&&!running&&!sync_model.live);
 }
 /* A rejected startup request owns the opened backend until checked cleanup.
  * Failed cleanup must keep its token, and cleanup must not retry brightness. */
 unsigned before_levels=levels;level_ok=false;
 assert(!start(deps,6)&&backend_token==123&&backend_live&&!running&&sync_model.live);
 assert(levels==before_levels+1&&!requested_level&&!visible_level);
 close_ok=false;assert(!quiesce()&&backend_token==123&&backend_live&&sync_model.live);
 close_ok=true;assert(quiesce()&&!backend_token&&!backend_live&&!sync_model.live);
 assert(levels==before_levels+1);level_ok=true;
 assert(start(deps,6));
 assert(api.base.acquire(NULL,FORMAT,&surface));
 sync_model.foreign=true;api.base.release(NULL,surface.frame);sync_model.foreign=false;
 assert(!quiesce()&&held);
 assert(enter());api.base.release(NULL,surface.frame);assert(held);assert(leave());
 sync_model.busy=true;assert(!quiesce()&&backend_token);sync_model.busy=false;
 sync_model.fail_unlock=true;assert(!quiesce()&&sync_model.live&&sync_model.held);sync_model.fail_unlock=false;
 sync_model.fail_destroy=true;assert(!quiesce()&&sync_model.live&&!sync_model.held);sync_model.fail_destroy=false;
 assert(quiesce()&&!sync_model.live);
 before_levels=levels;open_ok=false;assert(!start(deps,6)&&backend_token==123&&levels==before_levels);assert(quiesce()&&!backend_token);
 open_ok=true;assert(start(deps,6));assert(panel_prepare_deep_sleep(NULL)==0);
 release_ok=false;assert(panel_resume_status(NULL)==RISC_LIGHT_SLEEP_RETAINED&&reset_retained&&gpio_live);
 assert(panel_prepare_deep_sleep(NULL)==RISC_DEEP_SLEEP_RETAINED);assert(!quiesce());
 puts("Vendor display facade: dark startup/default request, fresh reopen, explicit zero, ordered Light restore, request failure custody and PMIC read-only PASS");
 return 0;
}

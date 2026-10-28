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
extern bool composition_open(void);
extern bool composition_row(uint32_t,const uint16_t*,size_t);
extern bool composition_level(uint8_t);
extern bool composition_close(void);
extern void composition_check_complete(void);
extern void composition_fail_close(void);
extern void composition_assert_closed(void);
extern void composition_assert_retained(void);
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
 requested_level=visible_level=0;frame_rows=0;frame_complete=false;return composition_open();
}
static bool backend_row(void*c,uint64_t t,uint32_t y,const uint16_t*p,size_t n){
 (void)c;assert(backend_live&&t==123&&y<RISC_CO5300_HEIGHT&&p&&n==RISC_CO5300_WIDTH);
 if(!composition_row(y,p,n))return false;
 if(!y){frame_rows=0;frame_complete=false;}
 assert(!frame_complete&&y==frame_rows);
 ++rows;++frame_rows;assert(p[0]==(uint16_t)(y*RISC_CO5300_WIDTH));++now;
 if(frame_rows==RISC_CO5300_HEIGHT){frame_complete=true;visible_level=requested_level;}
 return true;
}
static bool backend_level(void*c,uint64_t t,uint8_t v){
 (void)c;assert(backend_live&&t==123);++levels;if(!composition_level(v))return false;
 requested_level=last_level=v;if(frame_complete||!v)visible_level=v;
 else assert(!visible_level);return true;
}
static bool backend_close(void*c,uint64_t t){(void)c;assert(t==123);++closed;if(!composition_close())return false;backend_live=false;visible_level=0;return true;}
static bool rail_info(void*c,risc_power_rail_info_v1*i){(void)c;*i=(risc_power_rail_info_v1){.struct_size=sizeof(*i),.enabled=rail_enabled,.configuration_verified=rail_verified,.configured_millivolts=rail_mv};return rail_ok;}
static bool rail_write(void*c,bool b){(void)c;(void)b;assert(!"vendor baseline must not modify PMIC");return false;}
static risc_display_backend_v1 native={1,sizeof(native),NULL,backend_open,backend_row,backend_level,backend_close};
static risc_platform_clock_api_v1 clock_api={1,sizeof(clock_api),NULL,mono,delay};
static risc_power_rail_api_v1 rail={1,sizeof(rail),NULL,rail_info,rail_write};
#include "../../components/co5300/facade/driver.c"

static risc_display_present_token_v1 begin_partial(void){
 risc_display_surface_v1 surface;assert(api.base.acquire(NULL,FORMAT,&surface));
 for(unsigned i=0;i<WIDTH*HEIGHT;++i)((uint16_t*)surface.pixels)[i]=(uint16_t)i;
 risc_display_rect_v1 damage={.x=2,.y=2,.width=2,.height=2};
 risc_display_present_token_v1 token;assert(api.base.submit(NULL,surface.frame,&damage,1,NULL,&token));return token;
}
int main(void){
 risc_hw_spi_display_v2 cfg={.base={.struct_size=sizeof(cfg),.reset=TEST_DISPLAY_RESET}};
 risc_hardware_device_v1 hw={.api_version=1,.struct_size=sizeof(hw),.instance_id=5,.compatible="chipone,co5300",.revision="unspecified",.config_type="display.spi",.config_version=2,.config_size=sizeof(cfg),.config=&cfg};
 risc_provider_dependency_v1 deps[]={{"hardware.device",1,&hw},{"platform.clock",1,&clock_api},{RISC_DISPLAY_BACKEND_CAPABILITY,1,&native},{"power.rail",1,&rail},{"platform.gpio",1,&reset_api},{"platform.sync",1,&sync_service}};
 assert(start(deps,6));risc_display_present_token_v1 token=begin_partial();
 for(unsigned y=0;y<HEIGHT;++y){
  assert(!panel_prepare_sleep(NULL));display_poll(2);assert(frame_rows==y+1);
  risc_display_present_status_v1 status;assert(api.base.present_status(NULL,token,&status));
  assert(status.state==(y+1==HEIGHT?RISC_DISPLAY_PRESENT_COMPLETE:RISC_DISPLAY_PRESENT_ACTIVE));
 }
 assert(rows==HEIGHT&&visible_level==80);composition_check_complete();
 // An unchanged complete frame now skips all SPI windows, proving the tested
 // composition actually selected reuse before any sleep operation.
 token=begin_partial();for(unsigned y=0;y<HEIGHT;++y)display_poll(2);composition_check_complete();
 for(unsigned cycle=0;cycle<3;++cycle){
  assert(panel_prepare_sleep(NULL)&&!visible_level);composition_assert_closed();
  assert(panel_resume(NULL)&&visible_level==80);composition_check_complete();
  token=begin_partial();for(unsigned y=0;y<HEIGHT;++y)display_poll(2);composition_check_complete();
 }
 // A native close timeout retains both images; facade rollback completes
 // cleanup, starts a new native ownership epoch and writes every row again.
 composition_fail_close();assert(!panel_prepare_sleep(NULL)&&suspended&&backend_token);composition_assert_retained();
 assert(panel_resume(NULL)&&visible_level==80);composition_check_complete();
 for(unsigned cycle=0;cycle<3;++cycle){
  assert(panel_prepare_deep_sleep(NULL)==0&&gpio_live&&hold_live&&!backend_live);composition_assert_closed();
  assert(panel_resume_status(NULL)==0&&!gpio_live&&!hold_live&&visible_level==80);composition_check_complete();
 }
 hold_result=RISC_DEEP_SLEEP_PLATFORM;
 assert(panel_prepare_deep_sleep(NULL)==RISC_DEEP_SLEEP_PLATFORM&&!gpio_live);composition_assert_closed();
 hold_result=0;assert(panel_resume(NULL)&&visible_level==80);composition_check_complete();
 assert(quiesce());assert(start(deps,6));token=begin_partial();
 for(unsigned i=0;i<3;++i)display_poll(2);
 assert(active&&present_state==RISC_DISPLAY_PRESENT_ACTIVE&&!panel_prepare_sleep(NULL));
 assert(quiesce());assert(present_state==RISC_DISPLAY_PRESENT_FAILED&&!active&&!running);
 puts("PASS shadow-enabled sleep/Deep rollback actual facade + native CO5300: partial damage full sequential frame; COMPLETE after final stripe; active sleep refused; cancellation FAILED; resume full frame before brightness");
 return 0;
}

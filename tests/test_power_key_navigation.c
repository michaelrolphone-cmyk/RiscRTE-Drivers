#include <assert.h>
#include <stdio.h>
#include "../Drivers/power_key_navigation/driver.c"
static unsigned reads;static uint32_t queued;static bool good=true;
static bool events(void*c,uint32_t*out){(void)c;++reads;if(!good)return false;*out=queued;queued=0;return true;}
int main(void){
 const risc_power_key_api_v1 k={1,sizeof(k),NULL,events};const risc_provider_dependency_v1 dep={RISC_POWER_KEY_CAPABILITY,1,&k};assert(start(&dep,1));
 risc_input_navigation_frame_v1 f;queued=RISC_POWER_KEY_SHORT_PRESS;assert(poll(NULL,&f)&&!f.pressed);assert(poll(NULL,&f)&&!f.pressed);
 for(unsigned cycle=0;cycle<1000;cycle++){queued=RISC_POWER_KEY_SHORT_PRESS;assert(poll(NULL,&f)&&f.pressed==RISC_NAV_BACK);assert(poll(NULL,&f)&&f.released==RISC_NAV_BACK);assert(poll(NULL,&f)&&!f.buttons);queued=RISC_POWER_KEY_LONG_PRESS;assert(poll(NULL,&f)&&f.pressed==RISC_NAV_HOME);assert(poll(NULL,&f)&&f.released==RISC_NAV_HOME);}
 /* Events are consumed pulses, not level samples. No empty poll is required
  * between independent presses, including consecutive events of one kind. */
 for(unsigned kind=0;kind<2;kind++){
  assert(poll(NULL,&f));uint32_t event=kind?RISC_POWER_KEY_LONG_PRESS:RISC_POWER_KEY_SHORT_PRESS;
  uint32_t button=kind?RISC_NAV_HOME:RISC_NAV_BACK;
  for(unsigned cycle=0;cycle<100;cycle++){
   queued=event;assert(poll(NULL,&f)&&f.buttons==button&&f.pressed==button);
   assert(f.released==(cycle?button:0));
  }
  assert(poll(NULL,&f)&&!f.buttons&&f.released==button);
 }
 const risc_input_foreground_v1 claim={RISC_POWER_KEY_CAPABILITY,1};assert(foreground(NULL,&claim,1));unsigned n=reads;queued=2;assert(poll(NULL,&f)&&reads==n&&!f.buttons);assert(foreground(NULL,NULL,0));assert(poll(NULL,&f)&&!f.buttons);assert(poll(NULL,&f)&&!f.buttons);
 good=false;assert(!poll(NULL,&f));good=true;queued=2;assert(poll(NULL,&f)&&!f.buttons);assert(poll(NULL,&f));assert(reset(NULL));stop();assert(!poll(NULL,&f));
 puts("PASS generic power-key navigation:1000 short/long pulse cycles, stale context drain, foreground exclusion and failed reads");
}

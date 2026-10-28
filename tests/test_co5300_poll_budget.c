/* Actual facade + backend mock, not a copied scheduling algorithm. */
#define main original_facade_suite
#include "test_vendor_panel.c"
#undef main
static void begin_frame(bool partial){
 risc_display_surface_v1 s;assert(api.base.acquire(NULL,FORMAT,&s));
 for(unsigned i=0;i<WIDTH*HEIGHT;++i)((uint16_t*)s.pixels)[i]=(uint16_t)i;
 risc_display_rect_v1 damage={2,2,2,2};risc_display_present_token_v1 t;
 assert(api.base.submit(NULL,s.frame,partial?&damage:NULL,partial?1:0,NULL,&t));
}
int main(void){
 risc_hw_spi_display_v2 cfg={.base={.struct_size=sizeof(cfg),.reset=TEST_DISPLAY_RESET}};
 risc_hardware_device_v1 hw={.api_version=1,.struct_size=sizeof(hw),.instance_id=5,.compatible="chipone,co5300",.revision="unspecified",.config_type="display.spi",.config_version=2,.config_size=sizeof(cfg),.config=&cfg};
 risc_provider_dependency_v1 deps[]={{"hardware.device",1,&hw},{"platform.clock",1,&clock_api},{RISC_DISPLAY_BACKEND_CAPABILITY,1,&native},{"power.rail",1,&rail},{"platform.gpio",1,&reset_api},{"platform.sync",1,&sync_service}};
 for(unsigned partial=0;partial<2;++partial)for(unsigned step=0;step<4;++step){
  row_delay_ms=step;assert(start(deps,6));begin_frame(partial);
  unsigned polls=0;
  while(active){unsigned before=frame_rows;uint64_t began=now;display_poll(8);++polls;
   assert(frame_rows>before&&frame_rows-before<=128);
   if(!step)assert(frame_rows-before==(HEIGHT-before<128?HEIGHT-before:128));
   else assert(now-began<=8+step-1);
   if(frame_rows<HEIGHT)assert(present_state==RISC_DISPLAY_PRESENT_ACTIVE&&!visible_level);
  }
  assert(frame_rows==HEIGHT&&present_state==RISC_DISPLAY_PRESENT_COMPLETE&&visible_level==80);
  if(!step)assert(polls==(HEIGHT+127)/128);
  printf("partial=%u row_ms=%u rows=%u polls=%u\n",partial,step,frame_rows,polls);
  assert(quiesce());
 }
 // Every possible failure position stops at that row; later polls do no work.
 row_delay_ms=0;
 for(unsigned bad=0;bad<HEIGHT;++bad){
  fail_row_at=bad;assert(start(deps,6));begin_frame(false);
  while(active)display_poll(8);
  assert(frame_rows==bad&&present_state==RISC_DISPLAY_PRESENT_FAILED&&!visible_level);
  unsigned attempts=row_attempts;display_poll(8);display_poll(20);assert(row_attempts==attempts);
  assert(quiesce());
 }
 fail_row_at=~0u;assert(start(deps,6));begin_frame(false);
 display_poll(0);assert(!frame_rows);display_poll(1);assert(!frame_rows);
 sync_model.foreign=true;display_poll(8);assert(!frame_rows);sync_model.foreign=false;
 display_poll(8);assert(frame_rows==128);
 assert(quiesce()&&!active&&present_state==RISC_DISPLAY_PRESENT_FAILED);
 puts("PASS finite stopped-clock bound, deadline progress, partial damage full-frame completion, every-row failure and no-later callbacks");
}

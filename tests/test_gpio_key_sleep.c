#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../Drivers/gpio_key_sleep/driver.c"
static bool level,read_ok=true,release_ok=true,claim_ok=true,claim_token=true;
static unsigned guard_calls,read_calls,clock_calls;
static bool guard_ok=true;static bool guard_cb(void*c){(void)c;assert(!retained);++guard_calls;return guard_ok;}
static const risc_power_sleep_guard_v1 guard_api={1,sizeof(guard_api),NULL,guard_cb};
static bool flip,clock_stalled;static uint64_t ticks;
static unsigned clock_model,phase_us,quantum_us,delay_fault,total_requested;
static unsigned timed_light_calls,timed_deep_calls;static uint32_t timer_ms,wake_reason;
static int32_t deep_rc=RISC_DEEP_SLEEP_BUSY,light_rc=RISC_LIGHT_SLEEP_OK;
static unsigned claims,releases,deep_calls,delays;
static bool claim_cb(void*c,uint8_t p,bool output,bool initial,bool pull,uint64_t*t){(void)c;assert(p==3&&!output&&!initial&&!pull);++claims;*t=claim_token?42:0;return claim_ok;}
static bool read_cb(void*c,uint64_t t,bool*out){(void)c;assert(!retained);assert(t==42);++read_calls;*out=level;return read_ok;}
static bool release_cb(void*c,uint64_t t){(void)c;assert(t==42);++releases;return release_ok;}
static int32_t deep_cb(void*c,uint64_t t,bool high){(void)c;assert(!retained);assert(t==42&&high==!!config->active_high);++deep_calls;return deep_rc;}
static int32_t light_cb(void*c,uint64_t t,bool high,risc_light_sleep_result_v1*out){(void)c;(void)out;assert(!retained);assert(t==42&&high==!!config->active_high);return light_rc;}
static uint64_t now_cb(void*c){(void)c;assert(!retained);++clock_calls;return ticks;}
static void delay_cb(void*c,uint32_t ms){
 (void)c;assert(!retained);assert(ms&&ms<=100);++delays;total_requested+=ms;
 if(!clock_stalled){
  if(clock_model==1){ /* Native tick-relative wait from arbitrary sub-ms phase. */
   uint64_t absolute=ticks*1000+phase_us;
   unsigned count=(ms*1000+quantum_us-1)/quantum_us;if(!count)count=1;
   uint64_t next=(absolute/quantum_us+count)*quantum_us;
   ticks=next/1000;phase_us=(unsigned)(next%1000);
  } else if(clock_model==8){if(delays==1)ticks+=49;else --ticks;}
  else if(clock_model==2)ticks+=delays==1?ms-1:ms;
  else if(clock_model==3)ticks+=delays==1?0:ms;
  else ticks+=ms;
 }
 if(flip)level=!!config->active_high;
 if(delays==delay_fault){
  if(clock_model==4)ticks=UINT64_MAX;
  if(clock_model==5)--ticks;
  if(clock_model==6)guard_ok=false;
  if(clock_model==7)read_ok=false;
 }
}
static int32_t timed_light_cb(void*c,uint64_t t,bool high,uint32_t ms,risc_light_sleep_result_v1*out){
 (void)c;assert(!retained&&t==42&&high==!!config->active_high);++timed_light_calls;timer_ms=ms;
 if(out)out->wake_cause=wake_reason;return light_rc;
}
static int32_t timed_deep_cb(void*c,uint64_t t,bool high,uint32_t ms){
 (void)c;assert(!retained&&t==42&&high==!!config->active_high);++timed_deep_calls;timer_ms=ms;return deep_rc;
}
static garden_gpio_v1 g={.api_version=1,.struct_size=sizeof(g),.claim=claim_cb,.read=read_cb,.release=release_cb,.deep_sleep=deep_cb,.light_sleep=light_cb};
static risc_platform_clock_api_v1 clk={1,sizeof(clk),NULL,now_cb,delay_cb};
static risc_hw_gpio_bank_v1 cfg={.struct_size=sizeof(cfg),.count=1,.active_high=1,.pins={3},.debounce_us=50000};
static risc_hardware_device_v1 hw={.api_version=1,.struct_size=sizeof(hw),.instance_id=17,.compatible="riscrte,gpio-key-sleep",.revision="unspecified",.config_type="gpio.bank",.config_version=1,.config_size=sizeof(cfg),.config=&cfg};
static risc_provider_dependency_v1 deps[]={{"hardware.device",1,&hw},{"platform.gpio",1,&g},{"platform.clock",1,&clk},{"power.sleep-guard",1,&guard_api}};
static void fresh(void){retained=false;release_ok=true;stop();assert(!token);cfg.active_high=1;cfg.debounce_us=50000;g.struct_size=sizeof(g);g.light_sleep_for=NULL;g.deep_sleep_for=NULL;level=flip=clock_stalled=false;ticks=100;clock_model=phase_us=delay_fault=total_requested=guard_calls=read_calls=clock_calls=timed_light_calls=timed_deep_calls=timer_ms=0;quantum_us=1000;wake_reason=RISC_LIGHT_SLEEP_WAKE_TIMER;read_ok=claim_ok=claim_token=guard_ok=true;claims=releases=deep_calls=delays=0;deep_rc=RISC_DEEP_SLEEP_BUSY;light_rc=0;}
static void measured_and_timed(void){
 unsigned cases=0;
 for(unsigned polarity=0;polarity<2;++polarity)for(unsigned duration=1;duration<=100;++duration)
  for(unsigned qi=0;qi<4;++qi)for(unsigned phase=0;phase<(unsigned[]){1,2,10,20}[qi]*1000;phase+=20){
   const unsigned q=(unsigned[]){1,2,10,20}[qi];
   fresh();cfg.active_high=polarity;cfg.debounce_us=duration*1000;level=!polarity;
   assert(start(deps,4));clock_model=1;quantum_us=q*1000;ticks=10000+phase/1000;phase_us=phase%1000;
   uint64_t start_ms=ticks;assert(prepare(NULL)&&prepared);assert(ticks-start_ms>=duration);
   assert(total_requested<=2*duration+2);stop();++cases;
  }
 for(unsigned duration=1;duration<=100;++duration)for(unsigned model=2;model<=3;++model){
  fresh();cfg.debounce_us=duration*1000;assert(start(deps,4));clock_model=model;
  assert(prepare(NULL)&&delays==2&&ticks>=100+duration);stop();
 }
 fresh();assert(start(deps,4));clock_stalled=true;assert(!prepare(NULL)&&!prepared&&delays==3&&total_requested==102);
 clock_stalled=false;assert(prepare(NULL));stop();
 for(unsigned model=4;model<=7;++model){fresh();assert(start(deps,4));clock_model=model;delay_fault=1;
  if(model==5)clock_stalled=true;
  assert(!prepare(NULL)&&!prepared);stop();}
 for(unsigned model=6;model<=7;++model){fresh();assert(start(deps,4));clock_model=model;delay_fault=2;clock_stalled=true;
  assert(!prepare(NULL)&&delays==2&&!prepared);stop();}
 fresh();assert(start(deps,4));clock_model=8;assert(!prepare(NULL)&&delays==2&&ticks==148);stop();
 fresh();assert(start(deps,4));ticks=UINT64_MAX;assert(!prepare(NULL)&&!delays);stop();
 fresh();assert(start(deps,4));ticks=UINT64_MAX-51;clock_model=2;assert(prepare(NULL)&&ticks==UINT64_MAX-1);stop();
 for(unsigned polarity=0;polarity<2;++polarity)for(unsigned wake=RISC_LIGHT_SLEEP_WAKE_GPIO;wake<=RISC_LIGHT_SLEEP_WAKE_TIMER;++wake){
  fresh();cfg.active_high=polarity;level=!polarity;assert(start(deps,4)&&prepare(NULL));
  g.light_sleep_for=timed_light_cb;g.deep_sleep_for=timed_deep_cb;wake_reason=wake;
  for(unsigned duration=1;duration<=100;duration++){
   risc_light_sleep_result_v1 result={.struct_size=sizeof(result)};
   assert(light_for(NULL,duration,&result)==RISC_LIGHT_SLEEP_OK&&result.wake_cause==wake&&timer_ms==duration);
   assert(resume(NULL)&&prepare(NULL));
  }
  assert(deep_for(NULL,300000)==RISC_DEEP_SLEEP_BUSY&&timer_ms==300000);
  g.struct_size=GARDEN_GPIO_LIGHT_SLEEP_FOR_V1_SIZE-1;
  assert(light_for(NULL,1,NULL)==RISC_LIGHT_SLEEP_UNSUPPORTED);
  g.struct_size=GARDEN_GPIO_LIGHT_SLEEP_FOR_V1_SIZE;
  assert(deep_for(NULL,1)==RISC_DEEP_SLEEP_UNSUPPORTED);g.struct_size=sizeof(g);
  unsigned before=timed_light_calls+timed_deep_calls;
  assert(light_for(NULL,0,NULL)==RISC_LIGHT_SLEEP_INVALID&&deep_for(NULL,0)==RISC_DEEP_SLEEP_INVALID);
  assert(light_for(NULL,RISC_TIMED_SLEEP_MAX_MS+1,NULL)==RISC_LIGHT_SLEEP_INVALID);
  assert(deep_for(NULL,RISC_TIMED_SLEEP_MAX_MS+1)==RISC_DEEP_SLEEP_INVALID);
  assert(before==timed_light_calls+timed_deep_calls);
  light_rc=RISC_LIGHT_SLEEP_RETAINED;assert(light_for(NULL,1,NULL)==RISC_LIGHT_SLEEP_RETAINED);
  unsigned callbacks=guard_calls+read_calls+clock_calls+timed_light_calls+timed_deep_calls+delays;
  assert(!prepare(NULL)&&!resume(NULL));bool pending_key=false;assert(!pending(NULL,&pending_key)&&pending_key);
  assert(light_for(NULL,1,NULL)==RISC_LIGHT_SLEEP_RETAINED&&deep_for(NULL,1)==RISC_DEEP_SLEEP_RETAINED);
  assert(enter_light(NULL,NULL)==RISC_LIGHT_SLEEP_RETAINED&&enter_deep(NULL)==RISC_DEEP_SLEEP_RETAINED);
  assert(!quiesce());assert(callbacks==guard_calls+read_calls+clock_calls+timed_light_calls+timed_deep_calls+delays);
 }
 for(unsigned which=0;which<2;++which){fresh();assert(start(deps,4)&&prepare(NULL));g.deep_sleep_for=timed_deep_cb;
  deep_rc=which?RISC_DEEP_SLEEP_RETAINED:0;assert(deep_for(NULL,300000)==RISC_DEEP_SLEEP_RETAINED&&retained);
  unsigned callbacks=guard_calls+read_calls+clock_calls+timed_deep_calls+delays;
  assert(!prepare(NULL)&&!resume(NULL)&&!quiesce());assert(deep_for(NULL,1)==RISC_DEEP_SLEEP_RETAINED);
  assert(callbacks==guard_calls+read_calls+clock_calls+timed_deep_calls+delays);
 }
 fresh();stop();printf("Measured debounce %u phase/polarity/duration cases; quantized zero progress, bounded stall, invalid clocks, timed suffix/custody PASS\n",cases);
}
int main(void){
 for(unsigned cycle=0;cycle<100;cycle++){
  fresh();assert(start(deps,4));bool p=true;assert(pending(NULL,&p)&&!p);
  assert(enter_deep(NULL)==RISC_DEEP_SLEEP_INVALID&&!deep_calls);
  level=true;assert(!prepare(NULL)&&!delays&&!deep_calls);level=false;
  flip=true;assert(!prepare(NULL)&&delays==1&&!deep_calls);flip=false;level=false;
  assert(prepare(NULL));assert(light_for(NULL,1,NULL)==RISC_LIGHT_SLEEP_UNSUPPORTED);assert(deep_for(NULL,1)==RISC_DEEP_SLEEP_UNSUPPORTED);level=true;assert(enter_deep(NULL)==RISC_DEEP_SLEEP_ACTIVE_WAKE&&!deep_calls);
  level=false;assert(enter_deep(NULL)==RISC_DEEP_SLEEP_BUSY&&deep_calls==1);assert(resume(NULL));
  assert(prepare(NULL));read_ok=false;assert(enter_deep(NULL)==RISC_DEEP_SLEEP_PLATFORM);read_ok=true;
  assert(resume(NULL));release_ok=false;assert(!quiesce()&&token);release_ok=true;stop();assert(!token);
 }
 for(unsigned i=0;i<2;i++){fresh();claim_ok=false;claim_token=i;assert(!start(deps,4));bool p=false;assert(!pending(NULL,&p)&&p&&!prepare(NULL)&&!resume(NULL));stop();assert(!token&&releases==i);}
 fresh();claim_token=false;assert(!start(deps,4));stop();
 fresh();assert(start(deps,4)&&prepare(NULL));deep_rc=0;assert(enter_deep(NULL)==RISC_DEEP_SLEEP_RETAINED);assert(!resume(NULL)&&!quiesce()&&token);
 fresh();assert(start(deps,4)&&prepare(NULL));deep_rc=RISC_DEEP_SLEEP_RETAINED;assert(enter_deep(NULL)==RISC_DEEP_SLEEP_RETAINED&&!quiesce());
 fresh();assert(start(deps,4)&&prepare(NULL));light_rc=RISC_LIGHT_SLEEP_RETAINED;assert(enter_light(NULL,NULL)==RISC_LIGHT_SLEEP_RETAINED&&!quiesce());
 fresh();assert(start(deps,4));clock_stalled=true;assert(!prepare(NULL));clock_stalled=false;assert(prepare(NULL));stop();
 fresh();guard_ok=false;assert(!start(deps,4)&&!claims);guard_ok=true;stop();assert(start(deps,4)&&prepare(NULL));guard_ok=false;assert(enter_deep(NULL)==RISC_DEEP_SLEEP_BUSY&&!deep_calls);guard_ok=true;stop();
 fresh();cfg.count=2;assert(!start(deps,4));cfg.count=1;g.deep_sleep=NULL;assert(!start(deps,4));g.deep_sleep=deep_cb;
 cfg.debounce_us=0;assert(!start(deps,4));cfg.debounce_us=50000;
 measured_and_timed();
 puts("GPIO key sleep: 100 repeated cycles, held/reasserted keys, failures, retained custody PASS");
}

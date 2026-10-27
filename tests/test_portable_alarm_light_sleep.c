#define PORTABLE_FOREGROUND_ALARM_SLEEP 1
#define main untimed_baseline_main
#include "test_portable_light_sleep.c"
#undef main
static unsigned alarm_prepares,alarm_resumes,timed_calls;
static int prepare_result,resume_result;
static uint32_t requested_duration,observed_duration;
static int alarm_prepare(void *context,uint32_t *duration){
 assert(context==&requested_duration&&duration&&!*duration);
 operation('q');assert(native_prepared&&panel_suspended&&sleep_prepared);
 ++alarm_prepares;*duration=requested_duration;
 if((prepare_result!=0&&prepare_result!=1)||(prepare_result==1&&requested_duration>86400000u))sealed=true;
 return prepare_result;
}
static int alarm_resume(void *context){
 assert(context==&requested_duration);operation('r');++alarm_resumes;
 assert(!native_prepared&&!panel_suspended&&!sleep_prepared&&released==3);
 assert(alarm_resumes==1);
 if(resume_result!=0&&resume_result!=1)sealed=true;
 return resume_result;
}
static int32_t fixture_timed(void *context,uint32_t duration,risc_light_sleep_result_v1 *out){
 assert(duration&&duration<=86400000u);++timed_calls;observed_duration=duration;
 uint32_t cause=wake_cause;if(cause==RISC_LIGHT_SLEEP_WAKE_TIMER)wake_cause=RISC_LIGHT_SLEEP_WAKE_GPIO;
 int32_t rc=fixture_light(context,out);wake_cause=cause;out->wake_cause=cause;
 if(cause==RISC_LIGHT_SLEEP_WAKE_TIMER){held=false;queued=0;}
 return rc;
}
static const portable_foreground_alarm_hooks hooks={&requested_duration,alarm_prepare,alarm_resume};
static void alarm_fresh(void){fresh(17,13,1);alarm_prepares=alarm_resumes=timed_calls=0;prepare_result=resume_result=1;requested_duration=1000;observed_duration=0;sleep_fixture.light_sleep_for=fixture_timed;}
static int alarm_attempt(void){return portable_app_foreground_idle_sleep_alarm(&runtime_fixture,&output_fixture,&gauge_fixture,&hooks);}
static void alarm_terminal(const char *expected){assert(alarm_attempt()==-2&&sealed&&fences==1);assert(!strcmp(trace,expected));unsigned q=alarm_prepares,r=alarm_resumes;assert(alarm_attempt()==-2);assert(alarm_prepares==q&&alarm_resumes==r);}
int main(void){
 assert(!untimed_baseline_main());
 const uint32_t durations[]={0,1,999,1000,86400000};
 for(unsigned d=0;d<5;d++)for(unsigned c=0;c<3;c++){
  alarm_fresh();requested_duration=durations[d];wake_cause=c==0?RISC_LIGHT_SLEEP_WAKE_GPIO:c==1?RISC_LIGHT_SLEEP_WAKE_OTHER:RISC_LIGHT_SLEEP_WAKE_TIMER;
  /* The untimed provider fixture keeps its original TIMER rejection contract. */
  if(!requested_duration&&c==2)continue;
  assert(alarm_attempt()==1);assert(alarm_prepares==1&&alarm_resumes==1&&timed_calls==(requested_duration!=0));
  if(requested_duration)assert(observed_duration==requested_duration);
  assert(!strcmp(trace,"abcntpsqlSPTnCBAr"));restored();
 }
 alarm_fresh();prepare_result=0;assert(alarm_attempt()==0&&!timed_calls&&!alarm_resumes);assert(!strcmp(trace,"abcntpsqSPTnCBA"));restored();
 alarm_fresh();light_result=RISC_LIGHT_SLEEP_ACTIVE_WAKE;assert(alarm_attempt()==0&&timed_calls==1&&!alarm_resumes);restored();
 alarm_fresh();sleep_fixture.light_sleep_for=NULL;assert(alarm_attempt()==0&&!timed_calls&&!alarm_resumes);restored();
 alarm_fresh();sleep_fixture.struct_size=RISC_POWER_LIGHT_SLEEP_SIZE;assert(alarm_attempt()==0&&!timed_calls&&!alarm_resumes);restored();
 alarm_fresh();panel_ready=false;assert(alarm_attempt()==0&&!alarm_prepares&&!alarm_resumes);restored();
 alarm_fresh();sleep_ready=false;assert(alarm_attempt()==0&&!alarm_prepares&&!alarm_resumes);restored();
 alarm_fresh();resume_result=0;assert(alarm_attempt()==1&&alarm_resumes==1);restored();
 const int unknown[]={-2,-1,2,99};
 for(unsigned i=0;i<4;i++){alarm_fresh();prepare_result=unknown[i];alarm_terminal("abcntpsq");assert(!alarm_resumes);}
 for(unsigned i=0;i<4;i++){alarm_fresh();resume_result=unknown[i];alarm_terminal("abcntpsqlSPTnCBAr");}
 alarm_fresh();requested_duration=86400001;alarm_terminal("abcntpsq");
 const int32_t bad_light[]={RISC_LIGHT_SLEEP_RETAINED,RISC_LIGHT_SLEEP_CONTEXT,1,88,-99};
 for(unsigned i=0;i<5;i++){alarm_fresh();light_result=bad_light[i];alarm_terminal("abcntpsql");assert(!alarm_resumes);}
 alarm_fresh();wake_size=0;alarm_terminal("abcntpsql");assert(!alarm_resumes);
 alarm_fresh();wake_cause=RISC_LIGHT_SLEEP_WAKE_NONE;alarm_terminal("abcntpsql");assert(!alarm_resumes);
 alarm_fresh();fail_neutralize=1;alarm_terminal("abcn");assert(!alarm_prepares);
 alarm_fresh();fail_neutralize=2;alarm_terminal("abcntpsqlSPTn");assert(!alarm_resumes);
 alarm_fresh();native_resume_result=RISC_RUNTIME_SLEEP_MEDIA_UNAVAILABLE;assert(alarm_attempt()==1&&alarm_resumes==1);restored();
 alarm_fresh();sleep_restored=false;alarm_terminal("abcntpsqlS");assert(!alarm_resumes);
 alarm_fresh();panel_result=RISC_LIGHT_SLEEP_RETAINED;alarm_terminal("abcntpsqlSP");assert(!alarm_resumes);
 alarm_fresh();native_resume_result=RISC_RUNTIME_SLEEP_RETAINED;alarm_terminal("abcntpsqlSPT");assert(!alarm_resumes);
 for(unsigned i=1;i<=3;i++){alarm_fresh();fail_release=i;alarm_terminal(i==1?"abcntpsqlSPTnC":i==2?"abcntpsqlSPTnCB":"abcntpsqlSPTnCBA");assert(!alarm_resumes);}
 alarm_fresh();portable_foreground_alarm_hooks bad=hooks;bad.resume=NULL;assert(!portable_app_foreground_idle_sleep_alarm(&runtime_fixture,&output_fixture,NULL,&bad)&&!trace_size);
 puts("PASS foreground alarm helper: late fresh prepare; bounded timer; GPIO/TIMER/OTHER; full restore/release before exact-once resume; refusal never consumes; terminal fences; untimed baseline preserved");return 0;
}

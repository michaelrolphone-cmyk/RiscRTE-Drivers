#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../Drivers/wake_key_filter/driver.c"
#include "../components/portable-light-sleep/foreground_light_sleep.c"

/* Use the real filter, not a one-shot drain approximation. The application
 * navigation grant keeps this provider alive while helper grants come/go. */
static bool sealed,held,raw_ok,pending_ok;
static uint32_t queued;
static unsigned raw_reads,pending_reads;
static char trace[128];
static size_t trace_size;
static unsigned acquired,released,fences,neutralizations;
static unsigned fail_acquire,fail_release,dirty_release,fail_neutralize;
static bool bad_acquire_record,panel_ready,sleep_ready,sleep_restored;
static bool panel_suspended,sleep_prepared,native_prepared;
static int32_t light_result,panel_result,native_prepare_result,native_resume_result;
static bool dirty_native_token,missing_native_token;
static uint32_t wake_cause,wake_size;
static unsigned width,height,stride;
static uint8_t completed[520u*520u*2u],visible[sizeof(completed)],original[sizeof(completed)];
static const size_t storage_capacity=sizeof(completed);
static const risc_runtime_api_v1 runtime_fixture;
static risc_display_power_api_v1 panel_fixture;
static risc_power_sleep_api_v1 sleep_fixture;
static risc_power_key_neutralize_v1 key_fixture;
static risc_display_output_api_v1 output_fixture;
static risc_battery_gauge_api_v1 gauge_fixture;

static void operation(char code) {
    assert(!sealed);assert(trace_size+1<sizeof(trace));
    trace[trace_size++]=code;trace[trace_size]='\0';
}
static bool fixture_raw(void *context,uint32_t *out) {
    (void)context;assert(!sealed);++raw_reads;*out=queued;queued=0;return raw_ok;
}
static bool fixture_pending(void *context,bool *out) {
    (void)context;assert(!sealed);++pending_reads;*out=held;return pending_ok;
}
static const risc_power_key_api_v1 raw_fixture={1,sizeof(raw_fixture),NULL,fixture_raw};

static bool fixture_neutralize(void *context) {
    operation('n');++neutralizations;
    if(neutralizations==fail_neutralize){sealed=true;return false;}
    unsigned before_raw=raw_reads,before_pending=pending_reads;
    bool ok=api.neutralize(context);
    assert(raw_reads==before_raw && pending_reads==before_pending);
    return ok;
}
static bool fixture_panel_prepare(void *context) {
    (void)context;operation('p');assert(native_prepared);
    panel_suspended=true;memset(visible,0,(size_t)stride*height);
    return panel_ready;
}
static int32_t fixture_panel_resume(void *context) {
    (void)context;operation('P');assert(panel_suspended && !sleep_prepared);
    if(panel_result){sealed=true;return panel_result;}
    /* Simulate the selected provider's geometry-owned dark-first replay. */
    for(unsigned y=0;y<height;++y)
        memcpy(visible+(size_t)y*stride,completed+(size_t)y*stride,width*2u);
    panel_suspended=false;return RISC_LIGHT_SLEEP_OK;
}
static bool fixture_sleep_prepare(void *context) {
    (void)context;operation('s');assert(panel_suspended && native_prepared);
    sleep_prepared=true;return sleep_ready;
}
static bool fixture_sleep_resume(void *context) {
    (void)context;operation('S');assert(sleep_prepared && panel_suspended);
    if(!sleep_restored){sealed=true;return false;}
    sleep_prepared=false;return true;
}
static int32_t fixture_light(void *context,risc_light_sleep_result_v1 *out) {
    (void)context;operation('l');assert(sleep_prepared && panel_suspended && native_prepared);
    assert(out && out->struct_size==sizeof(*out));
    out->struct_size=wake_size;out->wake_cause=wake_cause;
    if(!foreground_idle_ordinary_light(light_result) ||
       (light_result==RISC_LIGHT_SLEEP_OK && (wake_size<sizeof(*out) ||
        (wake_cause!=RISC_LIGHT_SLEEP_WAKE_GPIO && wake_cause!=RISC_LIGHT_SLEEP_WAKE_OTHER))))sealed=true;
    if(light_result==RISC_LIGHT_SLEEP_OK){held=true;queued=RISC_POWER_KEY_SHORT_PRESS;}
    return light_result;
}
static bool fixture_acquire(const char *cap,uint32_t version,uint64_t instance,
                            risc_runtime_capability_v1 *out) {
    static const char *const names[]={RISC_DISPLAY_POWER_CAPABILITY,RISC_POWER_SLEEP_CAPABILITY,RISC_POWER_KEY_CAPABILITY};
    assert(acquired<3);assert(version==1 && instance==0 && foreground_idle_empty(out));
    operation((char)('a'+acquired));++acquired;assert(!strcmp(cap,names[acquired-1]));
    if(acquired==fail_acquire){sealed=true;return false;}
    out->slot=acquired;out->generation=7;
    out->api=acquired==1?(const void *)&panel_fixture:acquired==2?(const void *)&sleep_fixture:&key_fixture.base;
    if(bad_acquire_record && acquired==2){out->slot=0;sealed=true;}
    return true;
}
static bool fixture_release(risc_runtime_capability_v1 *grant) {
    assert(grant->api && grant->generation==7);
    operation((char)('A'+grant->slot-1));++released;
    if(released==fail_release){sealed=true;return false;}
    if(released==dirty_release){sealed=true;return true;}
    *grant=(risc_runtime_capability_v1){.struct_size=sizeof(*grant)};
    return true;
}
static bool fixture_retain(void) {++fences;sealed=true;return true;}
static int32_t fixture_native_prepare(uint64_t *token) {
    operation('t');assert(token && !*token);
    if(native_prepare_result==RISC_RUNTIME_SLEEP_OK) {
        native_prepared=true;*token=missing_native_token?0:99;
        if(missing_native_token)sealed=true;
    } else {
        *token=dirty_native_token?99:0;
        if(dirty_native_token || (native_prepare_result!=RISC_RUNTIME_SLEEP_BUSY &&
           native_prepare_result!=RISC_RUNTIME_SLEEP_UNSUPPORTED))sealed=true;
    }
    return native_prepare_result;
}
static int32_t fixture_native_resume(uint64_t token) {
    operation('T');assert(token==99 && native_prepared && !panel_suspended && !sleep_prepared);
    if(native_resume_result!=RISC_RUNTIME_SLEEP_OK && native_resume_result!=RISC_RUNTIME_SLEEP_MEDIA_UNAVAILABLE) {
        sealed=true;return native_resume_result;
    }
    native_prepared=false;return native_resume_result;
}
static const risc_runtime_api_v1 runtime_fixture={.api_version=1,.struct_size=sizeof(runtime_fixture),
    .acquire=fixture_acquire,.release=fixture_release,.retain_invocation=fixture_retain,
    .sleep_prepare=fixture_native_prepare,.sleep_resume=fixture_native_resume};

static void reset_counts(void) {
    trace_size=0;trace[0]='\0';acquired=released=fences=neutralizations=0;
    raw_reads=pending_reads=0;
}
static void fresh(unsigned w,unsigned h,unsigned padding) {
    sealed=false;foreground_idle_retained=false;stop();
    held=false;queued=0;raw_ok=pending_ok=true;
    fail_acquire=fail_release=dirty_release=fail_neutralize=0;
    bad_acquire_record=dirty_native_token=missing_native_token=false;
    panel_ready=sleep_ready=sleep_restored=true;
    panel_suspended=sleep_prepared=native_prepared=false;
    light_result=panel_result=native_prepare_result=native_resume_result=0;
    wake_cause=RISC_LIGHT_SLEEP_WAKE_GPIO;wake_size=sizeof(risc_light_sleep_result_v1);
    width=w;height=h;stride=w*2u+padding;assert((size_t)stride*height<=storage_capacity);
    for(size_t i=0;i<(size_t)stride*height;++i)completed[i]=(uint8_t)(i*19u+7u);
    memcpy(original,completed,(size_t)stride*height);memcpy(visible,completed,(size_t)stride*height);
    panel_fixture=(risc_display_power_api_v1){.api_version=1,.struct_size=sizeof(panel_fixture),
        .prepare_sleep=fixture_panel_prepare,.resume_status=fixture_panel_resume};
    sleep_fixture=(risc_power_sleep_api_v1){.api_version=1,.struct_size=sizeof(sleep_fixture),
        .prepare_sleep=fixture_sleep_prepare,.resume=fixture_sleep_resume,
        .light_sleep=fixture_light,.sleep_wake_pending=fixture_pending};
    const risc_provider_dependency_v1 deps[]={{"input.power-key.raw",1,&raw_fixture},{RISC_POWER_SLEEP_CAPABILITY,1,&sleep_fixture}};
    assert(start(deps,2));
    key_fixture=api;key_fixture.neutralize=fixture_neutralize;
    output_fixture=(risc_display_output_api_v1){.api_version=1,.struct_size=sizeof(output_fixture)};
    gauge_fixture=(risc_battery_gauge_api_v1){.api_version=1,.struct_size=sizeof(gauge_fixture)};
    reset_counts();
}
static int attempt(void) {
    struct {char draft[17];unsigned unsaved,credential_version;} app={"unsaved edit",1,42};
    const unsigned char *bytes=(const unsigned char *)&app;
    unsigned char copy[sizeof(app)];memcpy(copy,bytes,sizeof(app));
    int result=portable_app_foreground_idle_sleep(&runtime_fixture,&output_fixture,&gauge_fixture);
    assert(!memcmp(copy,bytes,sizeof(app)));return result;
}
static void restored(void) {
    assert(!sealed && !fences && !panel_suspended && !sleep_prepared && !native_prepared);
    assert(!memcmp(original,completed,(size_t)stride*height));
    for(unsigned y=0;y<height;++y)
        assert(!memcmp(original+(size_t)y*stride,visible+(size_t)y*stride,width*2u));
    assert(released==3);
}
static void terminal(const char *expected) {
    assert(attempt()==-2);assert(sealed && fences==1);assert(!strcmp(trace,expected));
    const size_t before=trace_size;assert(attempt()==-2 && trace_size==before && fences==1);
}
static void event(uint32_t value,bool down,uint32_t expected) {
    queued=value;held=down;uint32_t got=99;
    assert(key_fixture.base.events(NULL,&got) && got==expected);
}

int main(void) {
    const unsigned geometries[][3]={{466,466,0},{240,240,0},{317,159,13},{3,2,5},{1,1,31}};
    for(unsigned i=0;i<sizeof(geometries)/sizeof(geometries[0]);++i) {
        fresh(geometries[i][0],geometries[i][1],geometries[i][2]);
        assert(attempt()==1);assert(!strcmp(trace,"abcntpslSPTnCBA"));restored();
    }
    /* No provider stop/restart between any of these Light cycles. */
    fresh(19,11,7);
    for(unsigned cycle=0;cycle<1000;++cycle) {
        reset_counts();held=false;assert(attempt()==1);restored();
        event(3,true,0);event(2,true,0); /* held wake */
        event(2,false,0);              /* release */
        event(2,false,0);event(1,false,0); /* late PMIC latches */
        event(2,true,0);               /* new observed press rearms */
        event(2,false,2);event(1,false,1); /* subsequent fresh pulses */
    }
    const int32_t refusals[]={RISC_LIGHT_SLEEP_INVALID,RISC_LIGHT_SLEEP_BUSY,
        RISC_LIGHT_SLEEP_ACTIVE_WAKE,RISC_LIGHT_SLEEP_PLATFORM,RISC_LIGHT_SLEEP_UNSUPPORTED};
    for(unsigned i=0;i<sizeof(refusals)/sizeof(refusals[0]);++i) {
        fresh(31,17,9);light_result=refusals[i];assert(attempt()==0);restored();
        assert(!strcmp(trace,"abcntpslSPTnCBA"));
    }
    fresh(31,17,1);panel_ready=false;assert(attempt()==0);restored();
    assert(!strcmp(trace,"abcntpPTnCBA"));
    fresh(31,17,3);sleep_ready=false;assert(attempt()==0);restored();
    assert(!strcmp(trace,"abcntpsSPTnCBA"));
    /* Held/reasserted wake refusal still consumes that gesture on restoration. */
    fresh(31,17,5);held=true;sleep_ready=false;assert(attempt()==0);restored();
    event(3,true,0);event(2,false,0);event(2,false,0);event(2,true,0);event(2,false,2);
    fresh(31,17,7);native_resume_result=RISC_RUNTIME_SLEEP_MEDIA_UNAVAILABLE;assert(attempt()==1);restored();
    fresh(31,17,7);wake_cause=RISC_LIGHT_SLEEP_WAKE_OTHER;assert(attempt()==1);restored();

    /* Extension compatibility and known pre-entry refusal never prepare. */
    for(unsigned i=0;i<10;++i) {
        fresh(7,3,1);
        switch(i) {
        case 0:key_fixture.base.struct_size=sizeof(key_fixture.base);break;
        case 1:key_fixture.neutralize=NULL;break;
        case 2:key_fixture.base.api_version=2;break;
        case 3:key_fixture.base.events=NULL;break;
        case 4:panel_fixture.struct_size=RISC_DISPLAY_POWER_LIGHT_SLEEP_SIZE;break;
        case 5:panel_fixture.resume_status=NULL;break;
        case 6:panel_fixture.prepare_sleep=NULL;break;
        case 7:sleep_fixture.struct_size=RISC_POWER_LIGHT_SLEEP_SIZE-1;break;
        case 8:sleep_fixture.light_sleep=NULL;break;
        case 9:sleep_fixture.resume=NULL;break;
        }
        assert(attempt()==0);assert(!strcmp(trace,"abcCBA"));restored();
    }
    for(unsigned i=0;i<6;++i) {
        fresh(7,3,1);risc_runtime_api_v1 rt=runtime_fixture;
        switch(i) {
        case 0:rt.struct_size=RISC_RUNTIME_SLEEP_V1_SIZE-1;break;
        case 1:rt.sleep_prepare=NULL;break;
        case 2:rt.sleep_resume=NULL;break;
        case 3:rt.retain_invocation=NULL;break;
        case 4:rt.acquire=NULL;break;
        case 5:rt.release=NULL;break;
        }
        assert(portable_app_foreground_idle_sleep(&rt,&output_fixture,NULL)==0);assert(!trace_size);
    }
    for(unsigned i=0;i<2;++i) {
        fresh(7,3,1);native_prepare_result=i?RISC_RUNTIME_SLEEP_UNSUPPORTED:RISC_RUNTIME_SLEEP_BUSY;
        assert(attempt()==0);assert(!strcmp(trace,"abcntCBA"));restored();
    }

    /* Each uncertainty is the final provider operation, not just a bad result. */
    for(unsigned i=1;i<=3;++i) {
        fresh(7,3,1);fail_acquire=i;terminal(i==1?"a":i==2?"ab":"abc");
    }
    fresh(7,3,1);bad_acquire_record=true;terminal("ab");
    fresh(7,3,1);fail_neutralize=1;terminal("abcn");
    fresh(7,3,1);fail_neutralize=2;terminal("abcntpslSPTn");
    const int32_t unknown_light[]={RISC_LIGHT_SLEEP_RETAINED,RISC_LIGHT_SLEEP_CONTEXT,1,88,-99};
    for(unsigned i=0;i<sizeof(unknown_light)/sizeof(unknown_light[0]);++i) {
        fresh(7,3,1);light_result=unknown_light[i];terminal("abcntpsl");
    }
    fresh(7,3,1);wake_size=0;terminal("abcntpsl");
    fresh(7,3,1);wake_cause=RISC_LIGHT_SLEEP_WAKE_NONE;terminal("abcntpsl");
    fresh(7,3,1);wake_cause=RISC_LIGHT_SLEEP_WAKE_TIMER;terminal("abcntpsl");
    fresh(7,3,1);sleep_restored=false;terminal("abcntpslS");
    const int32_t bad_panel[]={RISC_LIGHT_SLEEP_RETAINED,RISC_LIGHT_SLEEP_PLATFORM,1,-99};
    for(unsigned i=0;i<sizeof(bad_panel)/sizeof(bad_panel[0]);++i) {
        fresh(7,3,1);panel_result=bad_panel[i];terminal("abcntpslSP");
    }
    const int32_t bad_native[]={RISC_RUNTIME_SLEEP_RETAINED,RISC_RUNTIME_SLEEP_CONTEXT,1,88,-99};
    for(unsigned i=0;i<sizeof(bad_native)/sizeof(bad_native[0]);++i) {
        fresh(7,3,1);native_resume_result=bad_native[i];terminal("abcntpslSPT");
    }
    const int32_t bad_prepare[]={RISC_RUNTIME_SLEEP_RETAINED,RISC_RUNTIME_SLEEP_CONTEXT,88,-99};
    for(unsigned i=0;i<sizeof(bad_prepare)/sizeof(bad_prepare[0]);++i) {
        fresh(7,3,1);native_prepare_result=bad_prepare[i];terminal("abcnt");
    }
    fresh(7,3,1);missing_native_token=true;terminal("abcnt");
    fresh(7,3,1);dirty_native_token=true;native_prepare_result=RISC_RUNTIME_SLEEP_BUSY;terminal("abcnt");
    for(unsigned i=1;i<=3;++i) {
        fresh(7,3,1);fail_release=i;terminal(i==1?"abcntpslSPTnC":i==2?"abcntpslSPTnCB":"abcntpslSPTnCBA");
        fresh(7,3,1);dirty_release=i;terminal(i==1?"abcntpslSPTnC":i==2?"abcntpslSPTnCB":"abcntpslSPTnCBA");
    }
    puts("Foreground Light helper: 1000 live-filter cycles; 5 native/padded geometries; paired failed prepares; native transactions; wake neutrality; fail-closed custody PASS");
    return 0;
}

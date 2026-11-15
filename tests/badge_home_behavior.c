/* Host behavior qualification, not a firmware boot test.
 * The maintained Home source is included unchanged to inspect its private UI
 * state without exporting test hooks or changing its production build defines.
 * Actual CO5300 and CST9217 providers are separately compiled. Only native
 * transport/hardware and Runtime invocation boundaries below are modeled.
 * Exactly four app grants exist. In particular RTC, battery, retained wake,
 * storage, sleep/key/display-power, alarm, radios and services are absent.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "cst9217_test_model.h"
#include "RiscDisplayBackendV1.h"
#include "RiscDisplaySpiV2.h"
#include "apps/clock/crown.c"
#include "PortableQuickLayout.h"

extern const risc_driver_v2 *badge_display_driver_get(uint32_t);
extern const risc_driver_v2 *badge_touch_driver_get(uint32_t);

enum bh_case { BH_RETURN, BH_COLD, BH_SWIPE, BH_PICKER, BH_QUICK, BH_GAP,
    BH_ROW_FAIL, BH_UNBLANK_FAIL, BH_DISPLAY_ABSENT, BH_TOUCH_ABSENT,
    BH_PROMOTE_FAIL, BH_CONFIRM_BUSY, BH_CASE_COUNT };
static const char *const bh_names[]={"return", "cold", "swipe-retry", "picker-no-storage",
    "quick-controls-no-services", "touch-gap-recovery", "display-row-failure", "unblank-failure",
    "display-absent", "touch-absent", "promotion-failure", "confirmation-busy"};
static cst_test_model bh_hw;
static const risc_driver_v2 *bh_display_driver, *bh_touch_driver;
static const risc_display_output_api_v1 *bh_display;
static bool bh_display_live, bh_touch_live, bh_backend_live, bh_reset_live;
static enum bh_case bh_case;
static uint32_t bh_generation, bh_serial, bh_ready_at, bh_services_at, bh_start;
static unsigned bh_granted, bh_released, bh_live, bh_rows, bh_frames, bh_unblanks;
static unsigned bh_polls, bh_promotions, bh_confirms, bh_native_reads, bh_launches;
static unsigned bh_first_frame, bh_first_input, bh_optional_refusals, bh_gap_reports;
static unsigned bh_seen_picker, bh_seen_quick, bh_save_failures, bh_navigation_failures, bh_disabled_hits;
static bool bh_ready, bh_services, bh_stop, bh_runtime_active, bh_busy_seen;
static uint8_t bh_level;
static uint16_t bh_pixels[466u*466u], bh_expected[466u*466u];
static struct { uint32_t token; const void *api; } bh_grants[64];
static char bh_refused[64][64];
static unsigned bh_refused_count;
#include "provider_sync_mock.h"
static provider_sync_mock bh_display_sync_model;
static const risc_provider_sync_api_v1 bh_display_sync=PROVIDER_SYNC_MOCK_API(bh_display_sync_model);

static void bh_display_guard(void) {
    assert(bh_display_sync_model.live&&bh_display_sync_model.held&&!bh_display_sync_model.foreign);
    assert(!bh_hw.sync_held); /* Independent provider custody, no nested touch call. */
}

static void bh_poll_display(void) {
    if(bh_display_live) {
        assert(bh_display_driver->struct_size>=sizeof(risc_driver_poll_v2));
        ((const risc_driver_poll_v2*)bh_display_driver)->poll(8);
    }
}
static uint64_t bh_millis(void *context) {(void)context;return bh_hw.clock_ms;}
static void bh_delay(void *context,uint32_t ms) {(void)context;bh_hw.clock_ms+=ms;}
static bool bh_reset_claim(void *context,uint8_t pin,bool output,bool initial,bool pullup,uint64_t *token) {
    (void)context;bh_display_guard();assert(pin==1&&output&&!initial&&!pullup&&!bh_backend_live&&!bh_reset_live);
    bh_reset_live=true;*token=1;return true;
}
static bool bh_reset_release(void *context,uint64_t token) {
    (void)context;bh_display_guard();assert(token==1&&bh_reset_live&&!bh_backend_live);bh_reset_live=false;return true;
}
static int32_t bh_reset_hold(void *context,uint64_t token,bool enabled) {
    (void)context;(void)token;(void)enabled;assert(!"No display-power capability is granted");return -1;
}
static bool bh_supply(void *context,risc_power_rail_info_v1 *out) {
    (void)context;bh_display_guard();assert(out&&out->struct_size==sizeof(*out));
    *out=(risc_power_rail_info_v1){.struct_size=sizeof(*out),.enabled=true,
        .configuration_verified=true,.configured_millivolts=3300};return true;
}
static bool bh_backend_open(void *context,uint64_t *token) {
    (void)context;bh_display_guard();assert(!bh_backend_live&&!bh_reset_live);bh_backend_live=true;*token=1;return true;
}
static bool bh_backend_close(void *context,uint64_t token) {
    (void)context;bh_display_guard();assert(token==1&&bh_backend_live);bh_backend_live=false;return true;
}
static bool bh_backend_row(void *context,uint64_t token,uint32_t y,const uint16_t *row,size_t width) {
    (void)context;bh_display_guard();assert(token==1&&bh_backend_live&&width==466&&y<466);
    assert(y==bh_rows%466u);
    if(bh_case==BH_ROW_FAIL&&y==100)return false;
    memcpy(bh_pixels+y*466u,row,width*sizeof(*row));++bh_rows;
    if(y==465)++bh_frames;
    return true;
}
static bool bh_backend_level(void *context,uint64_t token,uint8_t level) {
    (void)context;bh_display_guard();assert(token==1&&bh_backend_live);
    if(level){assert(bh_frames&&bh_rows%466u==0);if(bh_case==BH_UNBLANK_FAIL)return false;++bh_unblanks;}
    bh_level=level;return true;
}
static const risc_platform_clock_api_v1 bh_clock={1,sizeof(bh_clock),NULL,bh_millis,bh_delay};
static const garden_gpio_v1 bh_gpio={.api_version=1,.struct_size=sizeof(bh_gpio),
    .claim=bh_reset_claim,.release=bh_reset_release,.deep_sleep_hold=bh_reset_hold};
static const risc_power_rail_api_v1 bh_rail={1,sizeof(bh_rail),NULL,bh_supply,NULL};
static const risc_display_backend_v1 bh_backend={1,sizeof(bh_backend),NULL,
    bh_backend_open,bh_backend_row,bh_backend_level,bh_backend_close};
static risc_hw_spi_display_v2 bh_display_config={.base={
    .struct_size=sizeof(bh_display_config),
    .bus={.struct_size=sizeof(risc_hw_bus_v1),.kind=RISC_HW_BUS_SPI,.instance_id=103,
          .controller=0,.frequency_hz=40000000,.sclk=38,.mosi=4,.miso=5,.sda=-1,.scl=-1},
    .width=466,.height=466,.offset_x=6,.cs=12,.dc=-1,.reset=1,.backlight=-1,.busy=-1,
    .backlight_active_high=1,.power_pins={-1,-1,-1,-1},.reset_assert_ms=10,.reset_recovery_ms=150},
    .data2=6,.data3=7};
static risc_hardware_device_v1 bh_display_hardware={1,sizeof(bh_display_hardware),5,
    "chipone,co5300","unspecified","display.spi",2,sizeof(bh_display_config),&bh_display_config};
static const risc_provider_dependency_v1 bh_display_deps[]={
    {"hardware.device",1,&bh_display_hardware},{"platform.clock",1,&bh_clock},
    {RISC_DISPLAY_BACKEND_CAPABILITY,1,&bh_backend},{"power.rail",1,&bh_rail},
    {"platform.gpio",1,&bh_gpio},{"platform.sync",1,&bh_display_sync}};

/* Script contacts at the hardware report boundary, never at Home recognizers. */
static void bh_contact(void) {
    unsigned x=233,y=179;bool down=false;
    uint32_t age=bh_services?(uint32_t)bh_hw.clock_ms-bh_services_at:0;
    if(bh_services) {
        if(bh_case==BH_SWIPE||bh_case==BH_GAP) {
            unsigned local=age%900u;
            if(age<1800&&local>=100&&local<360){down=true;x=local<200?310:100;}
        } else if(bh_case==BH_PICKER) {
            if(age>=100&&age<820)down=true;
            if(age>=1400&&age<1500)down=true;
        } else if(bh_case==BH_QUICK) {
            if(age>=100&&age<400){down=true;y=age<180?10:age<260?180:450;}
            pqa_layout layout=pqa_layout_for(&clock_quick.ui);
            const pqa_rect *target=NULL;
            if(age>=700&&age<800)target=&layout.tiles[2];
            if(age>=1000&&age<1100)target=&layout.tiles[3];
            if(age>=1300&&age<1400)target=&layout.tiles[4];
            if(age>=1800&&age<1900)target=&layout.handle;
            if(target){down=true;x=(unsigned)(target->x+target->w/2);y=(unsigned)(target->y+target->h/2);}
        }
    }
    cst_test_report(&bh_hw,x,y,down);
    if(bh_case==BH_GAP&&age>=180&&age<280){bh_hw.raw[6]=0;bh_gap_reports++;}
}
static bool bh_touch_transact(void *context,uint64_t token,const uint8_t *tx,size_t tx_size,
                              uint8_t *rx,size_t rx_size,uint32_t timeout) {
    if(rx_size==10){bh_contact();++bh_polls;}
    return cst_test_transact(context,token,tx,tx_size,rx,rx_size,timeout);
}
static bool bh_health(risc_runtime_health_v1 *out) {
    assert(bh_runtime_active);out->uptime_ms=(uint32_t)bh_hw.clock_ms;
    if(picker.open)++bh_seen_picker;
    if(pqa_visible(&clock_quick.ui))++bh_seen_quick;
    if(bh_case==BH_QUICK&&clock_quick.ui.gesture==PQA_PANEL_PENDING&&
       clock_quick.ui.pressed_tile>=2&&clock_quick.ui.pressed_tile<=4) {
        assert(!clock_quick.ui.radios_valid);bh_disabled_hits|=1u<<clock_quick.ui.pressed_tile;
    }
    assert(!face.time_valid&&!face.battery_valid);
    if(bh_services) {
        uint32_t duration=(bh_case==BH_SWIPE||bh_case==BH_PICKER||bh_case==BH_QUICK||bh_case==BH_GAP)?2300:120;
        if((uint32_t)bh_hw.clock_ms-bh_services_at>=duration)bh_stop=true;
    }
    /* End only at a settled frame boundary, not halfway through presentation. */
    if(bh_stop&&!held&&bh_rows%466u==0)return false;
    assert(bh_hw.clock_ms-bh_start<15000);
    return true;
}
static void bh_yield(uint32_t milliseconds) {
    assert(bh_runtime_active&&milliseconds>=1&&milliseconds<=50);
    bh_hw.clock_ms+=milliseconds;bh_poll_display();
}
static bool bh_diagnostic(const char *line) {
    if(!strcmp(line,"WATCH_CLOCK ready crown=enabled")){bh_ready=true;bh_ready_at=(uint32_t)bh_hw.clock_ms;}
    if(strstr(line,"stage=first-frame ")) {
        ++bh_first_frame;assert(bh_frames&&bh_unblanks&&bh_level&&!held);
        assert(!face.time_valid&&!face.battery_valid&&touch.subscription);
        nova_watch_labels labels;nova_watch_format(&face,&labels);
        assert(!strcmp(labels.hour_minute,"--:--")&&!strcmp(labels.date,"TIME UNSET")&&!strcmp(labels.battery,"--%"));
        assert(clock_viewport.width==466&&clock_viewport.height==466);
        assert(clock_viewport.layout.safe.x==69&&clock_viewport.layout.safe.y==69&&
               clock_viewport.layout.safe.width==328&&clock_viewport.layout.safe.height==328);
        risc_display_surface_v1 expected={1,bh_expected,466,466,932,sizeof(bh_expected),RISC_DISPLAY_FORMAT_RGB565};
        assert(nova_watch_face_render(&expected,&face,picker.selected));
        assert(!strcmp(sleep_status,"TIME CHECK ERR"));
        assert(nova_watch_sleep_status(&expected,sleep_status));
        assert(!memcmp(bh_pixels,bh_expected,sizeof(bh_pixels)));
    }
    if(strstr(line,"stage=first-input-poll ")){++bh_first_input;assert(bh_polls&&bh_ready&&bh_first_frame);}
    if(strstr(line,"stage=services-ready ")) {
        assert(bh_ready&&bh_first_frame&&bh_first_input&&bh_promotions==1&&bh_confirms==1);
        bh_services=true;bh_services_at=(uint32_t)bh_hw.clock_ms;
    }
    if(!strcmp(line,"WATCH_CLOCK face=save-failed"))++bh_save_failures;
    if(!strcmp(line,"WATCH_CLOCK error=launcher-request"))++bh_navigation_failures;
    if(!strcmp(line,"WATCH_CLOCK confirm=deferred checkpoint=busy"))bh_busy_seen=true;
    return true;
}
static int32_t bh_realtime_read(void *context,risc_realtime_snapshot_v1 *out) {
    (void)context;assert(out->struct_size==sizeof(*out));++bh_native_reads;
    *out=(risc_realtime_snapshot_v1){.struct_size=sizeof(*out),.validity=RISC_REALTIME_UNSET,
        .monotonic_before_us=bh_hw.clock_ms*1000,.monotonic_after_us=bh_hw.clock_ms*1000};
    return RISC_REALTIME_OK;
}
static int32_t bh_realtime_seed(void *context,int64_t epoch,uint32_t nanoseconds) {
    (void)context;(void)epoch;(void)nanoseconds;assert(!"No authorized RTC/time source exists");return RISC_REALTIME_IO;
}
static int32_t bh_promote(void *context) {
    (void)context;assert(bh_first_frame&&bh_first_input&&bh_ready&&!held&&bh_rows%466u==0);
    ++bh_promotions;return bh_case==BH_PROMOTE_FAIL?RISC_PROVIDER_PROMOTION_FAILED:RISC_PROVIDER_PROMOTION_OK;
}
static const risc_realtime_control_api_v1 bh_realtime={1,sizeof(bh_realtime),NULL,bh_realtime_read,bh_realtime_seed};
static const risc_provider_promotion_api_v1 bh_promotion={1,sizeof(bh_promotion),NULL,bh_promote};
static bool bh_acquire(const char *id,uint32_t version,uint64_t instance,risc_runtime_capability_v1 *out) {
    assert(bh_runtime_active&&out->struct_size==sizeof(*out)&&!out->api&&!out->slot&&!out->generation);
    const void *api=NULL;
    if(!strcmp(id,"display.output")) {
        assert(version==1&&!instance);if(bh_case==BH_DISPLAY_ABSENT)return false;
        if(!bh_display_live){assert(bh_display_driver->start(bh_display_deps,6));bh_display_live=true;}
        api=bh_display;
    } else if(!strcmp(id,"input.touch.raw")) {
        assert(version==1&&!instance);if(bh_case==BH_TOUCH_ABSENT)return false;
        if(!bh_touch_live){assert(bh_touch_driver->start(bh_hw.dependencies,6));bh_touch_live=true;}
        api=bh_touch_driver->capability;
    } else if(!strcmp(id,RISC_REALTIME_CONTROL_CAPABILITY)) {
        assert(version==1&&!instance);api=&bh_realtime;
    } else if(!strcmp(id,RISC_PROVIDER_PROMOTION_CAPABILITY)) {
        assert(version==1&&!instance);api=&bh_promotion;
    } else {
        ++bh_optional_refusals;
        unsigned i;for(i=0;i<bh_refused_count;++i)if(!strcmp(bh_refused[i],id))break;
        if(i==bh_refused_count){assert(i<64&&strlen(id)<sizeof(bh_refused[i]));strcpy(bh_refused[bh_refused_count++],id);}
        return false; /* No stub service, synthetic grant, or optional provider. */
    }
    assert(bh_serial+1<64);uint32_t slot=++bh_serial;
    bh_grants[slot].token=slot;bh_grants[slot].api=api;
    *out=(risc_runtime_capability_v1){sizeof(*out),slot,bh_generation,api};
    ++bh_granted;++bh_live;return true;
}
static bool bh_release(risc_runtime_capability_v1 *grant) {
    assert(bh_runtime_active&&grant->slot<64&&grant->slot&&grant->generation==bh_generation);
    assert(bh_grants[grant->slot].token==grant->slot&&bh_grants[grant->slot].api==grant->api);
    bh_grants[grant->slot].token=0;bh_grants[grant->slot].api=NULL;
    *grant=(risc_runtime_capability_v1){.struct_size=sizeof(*grant)};++bh_released;assert(bh_live);--bh_live;return true;
}
static bool bh_launch(const char *path) {
    assert(!strcmp(path,"springboard.elf")&&bh_services&&!held&&!touch.subscription);
    ++bh_launches;return false; /* The Badge catalog does not supply Springboard. */
}
static bool bh_confirm(void) {assert(bh_promotions==1&&bh_case!=BH_PROMOTE_FAIL);++bh_confirms;return true;}
static bool bh_retain(void) {assert(!"No retained failure is injected in this suite");return true;}
static bool bh_initial(void) {return bh_case==BH_COLD;}
static int32_t bh_checkpoint(void) {
    assert(!held&&bh_rows%466u==0);
    return bh_case==BH_CONFIRM_BUSY&&bh_hw.clock_ms-bh_ready_at<450?RISC_RUNTIME_CHECKPOINT_BUSY:RISC_RUNTIME_CHECKPOINT_OK;
}
static const risc_runtime_api_v1 bh_runtime={.api_version=1,.struct_size=sizeof(bh_runtime),
    .health=bh_health,.yield_ms=bh_yield,.diagnostic=bh_diagnostic,.request_launch=bh_launch,
    .acquire=bh_acquire,.release=bh_release,.confirm_boot=bh_confirm,.retain_invocation=bh_retain,
    .initial_default=bh_initial,.service_checkpoint=bh_checkpoint};
const risc_runtime_api_v1 *risc_runtime_get_api(uint32_t version){return version==1?&bh_runtime:NULL;}

static bool bh_was_refused(const char *id) {
    for(unsigned i=0;i<bh_refused_count;++i)if(!strcmp(bh_refused[i],id))return true;
    return false;
}
static void bh_run(enum bh_case scenario) {
    assert(!bh_display_live&&!bh_touch_live&&!bh_backend_live&&!bh_reset_live&&!bh_live);
    assert(!bh_display_sync_model.live&&!bh_display_sync_model.held);
    assert(bh_display_sync_model.creates==bh_display_sync_model.destroys);
    unsigned display_sync_before=bh_display_sync_model.creates;
    cst_test_setup(&bh_hw);bh_hw.touch=bh_touch_driver->capability;bh_hw.bus.transact=bh_touch_transact;
    bh_case=scenario;++bh_generation;bh_serial=bh_ready_at=bh_services_at=0;bh_start=(uint32_t)bh_hw.clock_ms;
    bh_granted=bh_released=bh_rows=bh_frames=bh_unblanks=bh_polls=bh_promotions=bh_confirms=bh_native_reads=bh_launches=0;
    bh_first_frame=bh_first_input=bh_optional_refusals=bh_gap_reports=bh_refused_count=0;
    bh_seen_picker=bh_seen_quick=bh_save_failures=bh_navigation_failures=bh_disabled_hits=0;
    bh_ready=bh_services=bh_stop=bh_busy_seen=false;bh_level=0;memset(bh_grants,0,sizeof(bh_grants));
    bh_runtime_active=true;app_main();bh_runtime_active=false;
    assert(!bh_live&&bh_granted==bh_released&&!touch.subscription&&!held&&!picker_scratch);
    assert(!native_clock.halted&&!clock_atomic_retained);
    if(bh_touch_live){assert(bh_touch_driver->quiesce());bh_touch_driver->stop();bh_touch_live=false;}
    cst_test_clean(&bh_hw);
    if(bh_display_live){assert(bh_display_driver->quiesce());bh_display_driver->stop();bh_display_live=false;assert(!bh_display_sync_model.live&&!bh_display_sync_model.held);}
    assert(!bh_backend_live&&!bh_reset_live);
    assert(bh_display_sync_model.creates==bh_display_sync_model.destroys);
    assert(bh_display_sync_model.creates-display_sync_before==(scenario==BH_DISPLAY_ABSENT?0u:1u));
    if(scenario<=BH_GAP||scenario==BH_CONFIRM_BUSY) {
        assert(bh_services&&bh_first_frame==1&&bh_first_input==1&&bh_promotions==1&&bh_confirms==1);
        assert(bh_polls>1&&bh_native_reads>1&&bh_optional_refusals);
        const char *absent[]={"runtime.retained-wake","rtc.clock","board.battery","power.sleep",
            "input.power-key","display.power","storage.key-value","alarm.service","net.wifi",
            "bluetooth.hci","telemetry.broadcast","contexts.service"};
        for(unsigned i=0;i<sizeof(absent)/sizeof(*absent);++i)assert(bh_was_refused(absent[i]));
    } else {
        assert(!bh_services&&!bh_confirms);
        if(scenario!=BH_PROMOTE_FAIL)assert(!bh_promotions);
        if(scenario==BH_ROW_FAIL||scenario==BH_UNBLANK_FAIL||scenario==BH_DISPLAY_ABSENT||scenario==BH_TOUCH_ABSENT)
            assert(!bh_ready&&!bh_first_frame&&!bh_unblanks);
    }
    if(scenario==BH_COLD)assert(bh_frames>20);
    if(scenario==BH_SWIPE)assert(bh_launches==2&&bh_navigation_failures==2);
    if(scenario==BH_GAP)assert(bh_gap_reports&&bh_launches==1&&bh_navigation_failures==1);
    if(scenario==BH_PICKER)assert(bh_seen_picker&&bh_save_failures==1&&picker.selected==0);
    if(scenario==BH_QUICK)assert(bh_seen_quick&&bh_disabled_hits==28&&!pqa_visible(&clock_quick.ui)&&!clock_quick.ui.radios_valid&&!clock_quick.ui.contexts_valid);
    if(scenario==BH_CONFIRM_BUSY)assert(bh_busy_seen);
    printf("%s: frames=%u input-reports=%u grants=%u/%u optional-refusals=%u promotion=%u confirm=%u PASS\n",
        bh_names[scenario],bh_frames,bh_polls,bh_released,bh_granted,bh_optional_refusals,bh_promotions,bh_confirms);
}
int main(void) {
    setvbuf(stdout,NULL,_IONBF,0);
    bh_display_driver=badge_display_driver_get(2);bh_touch_driver=badge_touch_driver_get(2);
    assert(bh_display_driver&&bh_touch_driver);bh_display=bh_display_driver->capability;
    for(unsigned i=0;i<BH_CASE_COUNT;++i)bh_run((enum bh_case)i);
    for(unsigned i=0;i<3;++i)bh_run(BH_RETURN);
    puts("Unchanged Home 0.19.4: four authorized app capability classes, native time UNSET, 466x466 safe69, production providers and checked repeat cleanup with independent owner-sync custody PASS");
    return 0;
}

/* A separate lifecycle probe, not Home and not a fake provider. It uses only
 * the four capabilities already granted by the unchanged Badge boot policy. */
#include <assert.h>
#include <string.h>
#include <RiscRuntimeV1.h>
#include <RiscProviderPromotionV1.h>
#include <RiscRealtimeV1.h>
#include <RiscDisplayOutputV1.h>
extern const char *badge_probe_mode(void);
extern void badge_probe_event(const char *event);
#define EXPORT __attribute__((visibility("default")))
EXPORT int app_module_init(void) { badge_probe_event("init"); return 0; }
EXPORT void app_module_fini(void) { badge_probe_event("fini"); }
static bool acquire(const risc_runtime_api_v1 *rt,const char *name,risc_runtime_capability_v1 *out) {
    *out=(risc_runtime_capability_v1){.struct_size=sizeof(*out)};
    return rt->acquire(name,1,0,out);
}
EXPORT void app_main(void) {
    const risc_runtime_api_v1 *rt=risc_runtime_get_api(1);assert(rt);
    const char *mode=badge_probe_mode();
    risc_runtime_capability_v1 display={0},touch={0},time={0},promotion={0},denied={0};
    if(!strcmp(mode,"probe-start-retained") || !strcmp(mode,"probe-i2c-retained")) {
        assert(!acquire(rt,"display.output",&display));
        assert(!display.api&&!display.slot&&!display.generation);
        assert(!risc_runtime_get_api(1));
        assert(!acquire(rt,"input.touch.raw",&denied));
        assert(!rt->request_launch("default.elf"));
        badge_probe_event("retained");return;
    }
    if(!strcmp(mode,"probe-start-retry")) {
        assert(!acquire(rt,"display.output",&display));
        assert(!display.api&&!display.slot&&!display.generation);
        badge_probe_event("start-refused-clean");
    }
    assert(acquire(rt,"display.output",&display));
    const risc_display_output_api_v1 *output=display.api;
    risc_display_info_v1 info={.struct_size=sizeof(info)};
    assert(output->get_info(output->context,&info));
    assert(info.width==466&&info.height==466);
    assert(info.safe_insets.top==69&&info.safe_insets.right==69&&info.safe_insets.bottom==69&&info.safe_insets.left==69);
    if(!strcmp(mode,"probe-release-retry")) {
        badge_probe_event("reject-next-close");
        const risc_runtime_capability_v1 original=display;
        assert(!rt->release(&display));
        assert(display.api==original.api&&display.slot==original.slot&&display.generation==original.generation);
        badge_probe_event("release-refused-mapped");
        assert(rt->release(&display));
        assert(!display.api&&!display.slot&&!display.generation);
        badge_probe_event("release-retried-clean");return;
    }
    assert(acquire(rt,"input.touch.raw",&touch));
    assert(acquire(rt,"runtime.realtime-control",&time));
    const risc_realtime_control_api_v1 *clock=time.api;
    risc_realtime_snapshot_v1 snapshot={.struct_size=sizeof(snapshot)};
    assert(clock->read(clock->context,&snapshot)==RISC_REALTIME_OK);
    assert(snapshot.validity==RISC_REALTIME_UNSET&&!snapshot.epoch_seconds&&!snapshot.nanoseconds);
    assert(acquire(rt,"runtime.provider-promotion",&promotion));
    const risc_provider_promotion_api_v1 *promoter=promotion.api;
    assert(promoter->promote(promoter->context)==RISC_PROVIDER_PROMOTION_OK);
    badge_probe_event("promoted");
    assert(promoter->promote(promoter->context)==RISC_PROVIDER_PROMOTION_ALREADY_READY);
    /* Missing services remain denied by real Runtime policy, not substitutes. */
    const struct {const char *name;uint32_t version;} optional[]={
        {"rtc.clock",2},{"board.battery",1},{"storage.key-value",1},
        {"runtime.retained-wake",1},{"storage.files",1},{"display.power",1},{"platform.gpio",1}};
    for(unsigned i=0;i<sizeof(optional)/sizeof(*optional);++i) {
        denied=(risc_runtime_capability_v1){.struct_size=sizeof(denied)};
        assert(!rt->acquire(optional[i].name,optional[i].version,0,&denied));
        assert(!denied.api&&!denied.slot&&!denied.generation);
    }
    assert(rt->release(&promotion)&&rt->release(&time)&&rt->release(&touch)&&rt->release(&display));
    badge_probe_event("released-pinned");
}

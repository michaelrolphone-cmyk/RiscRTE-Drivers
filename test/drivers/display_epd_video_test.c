#include <assert.h>
#include <string.h>
#include "RiscDisplayOutputV1.h"
#include "RiscProviderV2.h"
#include "T5VideoApi.h"

static unsigned starts, stops, submits;
static bool pending;
static bool incompatible;
static bool fail_stop, fail_start;
static uint32_t frames;
static uint8_t pixels[129600];
static bool start_video(t5_video_surface_v1 *out) {
    ++starts;
    *out = (t5_video_surface_v1){960, 540, 120, T5_VIDEO_PIXEL_MONO_1BPP_MSB,
                                  T5_VIDEO_FLAG_ONE_IS_BLACK};
    if (incompatible) out->width = 1;
    return true;
}
static bool start_format(t5_video_surface_v1 *out, uint8_t format) {
    if (!start_video(out)) return false;
    if (fail_start) return false;
    out->pixel_format = format;
    out->stride_bytes = format == T5_VIDEO_PIXEL_GRAY_2BPP_MSB ? 240 : 120;
    return true;
}
static uint8_t *buffer(size_t *bytes) { *bytes = sizeof(pixels); return pixels; }
static bool can_submit(void) { return !pending; }
static bool submit_video(uint16_t y, uint16_t height) {
    assert(y == 8 && height == 4);
    ++submits;
    pending = true;
    return true;
}
static bool is_pending(void) { return pending; }
static uint32_t frame_counter(void) { return frames; }
static void stop_video(void) { ++stops; pending = false; }
static bool try_stop_video(void) {
    if (fail_stop) return false;
    stop_video();
    return true;
}
static t5_video_api_v1 video_api = {
    .api_version = T5_VIDEO_API_VERSION, .struct_size = sizeof(t5_video_api_v1),
    .start = start_video, .backbuffer = buffer, .can_submit = can_submit,
    .submit = submit_video, .pending = is_pending, .frame_counter = frame_counter,
    .stop = stop_video, .start_format = start_format, .try_stop = try_stop_video
};
const t5_video_api_v1 *t5_video_get_api(uint32_t version) {
    return version == T5_VIDEO_API_VERSION ? &video_api : NULL;
}

int main(void) {
    const risc_driver_v2 *driver = t5_driver_get(RISC_PROVIDER_DRIVER_ABI_V2);
    assert(driver && driver->start(NULL, 0) && starts == 0);
    const risc_display_output_api_v1 *output = driver->capability;
    risc_display_info_v1 info;
    assert(output->get_info(NULL, &info) && info.width == 960 && info.height == 540);
    risc_display_surface_v1 frame;
    assert(output->acquire(NULL, RISC_DISPLAY_FORMAT_MONO1, &frame) && starts == 1);
    assert(frame.pixels == pixels && frame.size_bytes == sizeof(pixels));
    assert(!driver->quiesce());
    const risc_display_rect_v1 dirty = {3, 8, 100, 4};
    risc_display_present_token_v1 token = 0;
    assert(output->submit(NULL, frame.frame, &dirty, 1, NULL, &token) && token && submits == 1);
    assert(!output->acquire(NULL, RISC_DISPLAY_FORMAT_MONO1, &frame));
    risc_display_present_status_v1 status;
    assert(output->present_status(NULL, token, &status) && status.state == RISC_DISPLAY_PRESENT_QUEUED);
    pending = false; ++frames;
    assert(output->present_status(NULL, token, &status) && status.state == RISC_DISPLAY_PRESENT_COMPLETE);
    assert(driver->quiesce() && stops == 1);
    driver->stop();
    assert(stops == 1);
    assert(driver->start(NULL, 0));
    incompatible = true;
    assert(!output->acquire(NULL, RISC_DISPLAY_FORMAT_MONO1, &frame));
    assert(stops == 2 && driver->quiesce());
    incompatible = false;
    assert(driver->start(NULL, 0));
    assert(output->acquire(NULL, RISC_DISPLAY_FORMAT_MONO1, &frame));
    assert(!output->acquire(NULL, RISC_DISPLAY_FORMAT_GRAY2, &frame));
    output->release(NULL, frame.frame);
    assert(output->acquire(NULL, RISC_DISPLAY_FORMAT_GRAY2, &frame));
    assert(frame.pixel_format == RISC_DISPLAY_FORMAT_GRAY2 && frame.stride_bytes == 240);
    output->release(NULL, frame.frame);
    assert(output->acquire(NULL, RISC_DISPLAY_FORMAT_MONO1, &frame));
    assert(frame.pixel_format == RISC_DISPLAY_FORMAT_MONO1 && frame.stride_bytes == 120);
    output->release(NULL, frame.frame);
    assert(driver->quiesce());

    /* Legacy void stop is never accepted as proof of safe release. */
    assert(driver->start(NULL, 0));
    unsigned before = starts;
    video_api.struct_size = offsetof(t5_video_api_v1, try_stop);
    assert(!output->acquire(NULL, RISC_DISPLAY_FORMAT_MONO1, &frame));
    assert(starts == before && driver->quiesce());
    video_api.struct_size = sizeof(video_api);

    /* Failed format switch keeps the outgoing backend pinned; no second
     * start, stale surface submission or unsafe restart is permitted. */
    assert(driver->start(NULL, 0));
    assert(output->acquire(NULL, RISC_DISPLAY_FORMAT_MONO1, &frame));
    output->release(NULL, frame.frame);
    before = starts;
    fail_stop = true;
    assert(!output->acquire(NULL, RISC_DISPLAY_FORMAT_GRAY2, &frame));
    assert(!driver->quiesce());
    driver->stop();
    assert(!driver->start(NULL, 0));
    assert(!output->acquire(NULL, RISC_DISPLAY_FORMAT_MONO1, &frame));
    assert(!output->submit(NULL, frame.frame, &dirty, 1, NULL, &token));
    assert(starts == before);
    fail_stop = false;
    assert(driver->quiesce());

    /* Even failed start and rejected geometry can retain backend resources. */
    for (unsigned malformed = 0; malformed < 2; ++malformed) {
        assert(driver->start(NULL, 0));
        fail_start = !malformed;
        incompatible = !!malformed;
        fail_stop = true;
        assert(!output->acquire(NULL, RISC_DISPLAY_FORMAT_MONO1, &frame));
        assert(!driver->quiesce());
        assert(!driver->start(NULL, 0));
        before = starts;
        assert(!output->acquire(NULL, RISC_DISPLAY_FORMAT_MONO1, &frame));
        assert(starts == before);
        fail_stop = false;
        assert(driver->quiesce());
        fail_start = incompatible = false;
    }
    assert(driver->start(NULL, 0));
    assert(output->acquire(NULL, RISC_DISPLAY_FORMAT_GRAY2, &frame));
    assert(!output->present_status(NULL, token, &status));
    output->release(NULL, frame.frame);
    fail_stop = true;
    assert(!driver->quiesce());
    assert(!output->acquire(NULL, RISC_DISPLAY_FORMAT_GRAY2, &frame));
    fail_stop = false;
    assert(driver->quiesce());
}

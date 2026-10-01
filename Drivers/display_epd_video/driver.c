#include "RiscDisplayOutputV1.h"
#include "RiscProviderV2.h"
#include "T5VideoApi.h"
/* The ELF host exports this bounded scheduler sleep from libc. */
extern int usleep(unsigned int microseconds);

/* Physical panel plumbing is private to the installable provider. This first
 * implementation borrows the board scan service, which may later be replaced
 * without changing the app-facing display.output ABI. */
static const t5_video_api_v1 *video;
static t5_video_surface_v1 scan;
static bool started;
static bool active;
static bool held;
static bool needs_stop;
static bool teardown_failed;
static bool has_pending_token;
static uint64_t frame_serial;
static uint64_t pending_token;
static uint32_t pending_counter;

/* A failed start can also leave DMA/controller resources behind. Keep the
 * provider pinned until the backend explicitly confirms their release. */
static bool stop_backend(void) {
    if (needs_stop && (!video || !video->try_stop())) {
        teardown_failed = true;
        return false;
    }
    started = false;
    needs_stop = false;
    teardown_failed = false;
    video = NULL;
    scan = (t5_video_surface_v1){0};
    has_pending_token = false;
    return true;
}

static bool get_info(void *context, risc_display_info_v1 *out) {
    (void)context;
    if (!out) return false;
    *out = (risc_display_info_v1){0};
    out->api_version = RISC_DISPLAY_OUTPUT_API_V1;
    out->struct_size = sizeof(*out);
    out->width = 960;
    out->height = 540;
    out->supported_formats = RISC_DISPLAY_FORMAT_BIT(RISC_DISPLAY_FORMAT_MONO1) |
                             RISC_DISPLAY_FORMAT_BIT(RISC_DISPLAY_FORMAT_GRAY2);
    out->preferred_format = RISC_DISPLAY_FORMAT_MONO1;
    out->supported_rotations = RISC_DISPLAY_ROTATION_0;
    out->flags = RISC_DISPLAY_INFO_PARTIAL_DAMAGE | RISC_DISPLAY_INFO_ASYNC_PRESENT;
    out->damage_x_alignment = 8;
    out->damage_y_alignment = 1;
    out->damage_width_alignment = 8;
    out->damage_height_alignment = 1;
    out->nominal_refresh_millihz = 24000;
    return true;
}

static bool acquire(void *context, uint32_t format, risc_display_surface_v1 *out) {
    (void)context;
    if (!active || !out || held || teardown_failed ||
        (format != RISC_DISPLAY_FORMAT_MONO1 && format != RISC_DISPLAY_FORMAT_GRAY2)) return false;
    const uint8_t scan_format = format == RISC_DISPLAY_FORMAT_GRAY2
        ? T5_VIDEO_PIXEL_GRAY_2BPP_MSB : T5_VIDEO_PIXEL_MONO_1BPP_MSB;
    if (started && scan.pixel_format != scan_format) {
        if (!stop_backend()) return false;
    }
    if (!started) {
        video = t5_video_get_api(T5_VIDEO_API_VERSION);
        if (!video || video->api_version != T5_VIDEO_API_VERSION ||
            video->struct_size < sizeof(*video) || !video->start || !video->stop ||
            !video->backbuffer || !video->can_submit || !video->submit ||
            !video->pending || !video->frame_counter || !video->start_format ||
            !video->try_stop) { video = NULL; return false; }
        needs_stop = true;
        if (!video->start_format(&scan, scan_format)) {
            (void)stop_backend();
            return false;
        }
        started = true;
        if (scan.width != 960 || scan.height != 540 || scan.stride_bytes != (format == RISC_DISPLAY_FORMAT_GRAY2 ? 240u : 120u) ||
            scan.pixel_format != scan_format ||
            !(scan.flags & T5_VIDEO_FLAG_ONE_IS_BLACK)) {
            (void)stop_backend();
            return false;
        }
    }
    if (!video->can_submit()) return false;
    size_t bytes = 0;
    uint8_t *pixels = video->backbuffer(&bytes);
    if (!pixels || bytes < (size_t)scan.stride_bytes * scan.height) return false;
    if (++frame_serial == 0) ++frame_serial;
    held = true;
    *out = (risc_display_surface_v1){frame_serial, pixels, scan.width, scan.height,
                                      scan.stride_bytes, (uint32_t)bytes,
                                      format};
    return true;
}

static void release(void *context, risc_display_frame_v1 frame) {
    (void)context;
    if (held && frame == frame_serial) held = false;
}

static bool submit(void *context, risc_display_frame_v1 frame,
                   const risc_display_rect_v1 *damage, size_t count,
                   const risc_display_present_options_v1 *options,
                   risc_display_present_token_v1 *token_out) {
    (void)context;
    (void)options;
    if (!started || teardown_failed || !held || frame != frame_serial || count > RISC_DISPLAY_MAX_DAMAGE_RECTS ||
        (count && !damage)) return false;
    uint32_t first = scan.height, last = 0;
    for (size_t i = 0; i < count; ++i) {
        const risc_display_rect_v1 *rect = &damage[i];
        if (rect->x < 0 || rect->y < 0 || !rect->width || !rect->height ||
            (uint32_t)rect->x > scan.width || rect->width > scan.width - (uint32_t)rect->x ||
            (uint32_t)rect->y > scan.height || rect->height > scan.height - (uint32_t)rect->y)
            return false;
        if ((uint32_t)rect->y < first) first = (uint32_t)rect->y;
        if ((uint32_t)rect->y + rect->height > last) last = (uint32_t)rect->y + rect->height;
    }
    if (!count) { first = 0; last = scan.height; }
    if (!video->submit((uint16_t)first, (uint16_t)(last - first))) return false;
    held = false;
    pending_counter = video->frame_counter();
    if (++pending_token == 0) ++pending_token;
    has_pending_token = true;
    if (token_out) *token_out = pending_token;
    return true;
}

static bool present_status(void *context, risc_display_present_token_v1 token,
                           risc_display_present_status_v1 *out) {
    (void)context;
    if (!started || teardown_failed || !has_pending_token || !out || !token || token != pending_token) return false;
    *out = (risc_display_present_status_v1){0};
    out->state = (video->pending() && video->frame_counter() == pending_counter)
        ? RISC_DISPLAY_PRESENT_QUEUED : RISC_DISPLAY_PRESENT_COMPLETE;
    return true;
}

static bool wait_present(void *context, risc_display_present_token_v1 token,
                         uint32_t timeout_ms, risc_display_present_status_v1 *out) {
    if (!present_status(context, token, out)) return false;
    /* Keep a request bounded even if the scan engine never completes. */
    const uint32_t budget = timeout_ms < 250u ? timeout_ms : 250u;
    for (uint32_t elapsed = 0; elapsed < budget &&
         out->state != RISC_DISPLAY_PRESENT_COMPLETE; ++elapsed) {
        usleep(1000);
        if (!present_status(context, token, out)) return false;
    }
    return true;
}

static bool set_brightness(void *context, uint16_t level, uint16_t maximum) {
    (void)context; (void)level; (void)maximum;
    return false;
}

static bool start(const risc_provider_dependency_v1 *deps, size_t count) {
    (void)deps;
    /* Mandatory app dependency resolution happens before display handoff.
     * Defer hardware start until the owner calls acquire after takeover. */
    if (count || started || held || active || needs_stop || teardown_failed) return false;
    active = true;
    return true;
}

static bool quiesce(void) {
    if (held) return false;
    if (!stop_backend()) return false;
    active = false;
    return true;
}

static void stop(void) { (void)quiesce(); }

static const risc_display_output_api_v1 output_api = {
    RISC_DISPLAY_OUTPUT_API_V1, sizeof(risc_display_output_api_v1), NULL,
    get_info, acquire, release, submit, present_status, wait_present, set_brightness
};
static const risc_driver_v2 driver = {
    RISC_PROVIDER_DRIVER_ABI_V2, sizeof(risc_driver_v2),
    "display-epd-video", RISC_DISPLAY_OUTPUT_CAPABILITY,
    RISC_DISPLAY_OUTPUT_API_V1, &output_api, start, stop, quiesce
};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &driver : NULL;
}

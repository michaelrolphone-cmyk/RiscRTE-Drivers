#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define T5_VIDEO_API_VERSION 1u
#define T5_VIDEO_PIXEL_MONO_1BPP_MSB 1u
#define T5_VIDEO_PIXEL_GRAY_2BPP_MSB 2u
#define T5_VIDEO_FLAG_ONE_IS_BLACK (1u << 0)

typedef struct {
    uint16_t width;
    uint16_t height;
    uint16_t stride_bytes;
    uint8_t pixel_format;
    uint8_t flags;
} t5_video_surface_v1;

/* Last completed ~1s scan window. Durations are mean wall microseconds per
 * scan, including preemption. prepare and residual DMA wait are parts of scan;
 * DMA can also run concurrently with preparation. pace is separate target-rate
 * sleep. active_rows counts converted rows, not physical rows transmitted. */
typedef struct {
    uint32_t samples, scan_us, prepare_us, dma_wait_us, pace_us, active_rows;
    uint8_t scan_core, app_core;
    uint16_t reserved;
} t5_video_scan_stats_v1;

typedef struct {
    uint32_t api_version;
    uint32_t struct_size;

    /* Start the fast hardware-takeover scan engine. The calling ELF must
     * export app_hardware_takeover() with T5_HARDWARE_TAKEOVER_DISPLAY.
     * A newly started service clears unknown retained contents with the shared
     * ~0.67s spatial wisp scrub (mono and grayscale). Repeating start on an already
     * running, same-format service does not replay it. Startup can fail on
     * allocation, power, scan, or deadline errors; callers must handle false.
     * On success, surface describes the physical scan-oriented framebuffer. */
    bool (*start)(t5_video_surface_v1 *surface);

    /* Returns the writable backbuffer. It remains owned by the video service;
     * submit() atomically swaps it at a scan boundary. */
    uint8_t *(*backbuffer)(size_t *size_out);

    /* Non-blocking frame publication. dirty_y/dirty_height are physical scan
     * rows; height==0 means the full panel. */
    bool (*can_submit)(void);
    bool (*submit)(uint16_t dirty_y, uint16_t dirty_height);
    bool (*pending)(void);
    uint32_t (*frame_counter)(void);

    /* Stop every scan task, DMA callback and panel resource before the ELF
     * returns. The host also force-stops this service before restoring its
     * normal display as a final unload guard. */
    void (*stop)(void);

    /* Optional additive entry point. Values in a 2bpp surface are packed
     * most-significant pixel first: 0 white, 1 light, 2 dark, 3 black.
     * The surface and all video calls retain the same lifetime/ownership. */
    bool (*start_format)(t5_video_surface_v1 *surface, uint8_t pixel_format);
    /* Optional additive 1.3.32 entry. Check struct_size before access.
     * Copies a coherent snapshot; false when stopped or no window is ready. */
    bool (*scan_stats)(t5_video_scan_stats_v1 *out);
    /* Optional additive 1.3.48 bounded mono black-endpoint reinforcement. Check struct_size.
     * Physical rows only: 1..64 rows, 1..2 extra passes; passes=0 cancels.
     * Latest request replaces previous; expires after 16 scans. Does not own
     * a framebuffer, delay submit completion, or touch whites/gray pixels.
     * False if unsupported/stopped/invalid. Optical result requires device QA. */
    bool (*reinforce_black)(uint16_t y, uint16_t height, uint8_t passes);
    /* Optional additive checked teardown. False retains live backend resources;
     * callers must retain their provider lease and retry before another owner
     * starts or the provider is unloaded. Legacy stop() remains ABI-compatible
     * but cannot certify quiescence. Check struct_size before access. */
    bool (*try_stop)(void);
} t5_video_api_v1;

const t5_video_api_v1 *t5_video_get_api(uint32_t api_version);

#ifdef __cplusplus
}
#endif

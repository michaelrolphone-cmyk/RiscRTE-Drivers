#pragma once
/* Generic duplex PCM device. A codec, USB interface, network endpoint or other
 * implementation may supply this capability. No bus/chip/board ABI is exposed.
 * Each token owns one direction. Open may fail with a nonzero cleanup token;
 * close it before dropping the grant. I/O failure permits close only.
 * Both slots are signed16 left/right; transfers copy <=256 frames and retain no
 * caller buffer. Short/zero RX is valid. A failed/partial TX is never replayed. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define RISC_AUDIO_PCM_CAPABILITY "audio.pcm"
enum { RISC_AUDIO_PCM_OUTPUT=1u,RISC_AUDIO_PCM_INPUT=2u };
typedef struct {
    uint32_t api_version,struct_size;
    void *context;
    bool (*open)(void *,uint8_t direction,uint32_t rate_hz,uint64_t *token);
    bool (*write)(void *,uint64_t,const int16_t *stereo,size_t frames,size_t *written,uint32_t timeout_ms);
    bool (*read)(void *,uint64_t,int16_t *stereo,size_t frames,size_t *read,uint32_t timeout_ms);
    /* Linear digital attenuation for output: zero is silence, level==maximum
     * is unity. A provider must not interpret unity as arbitrary analog boost. */
    bool (*set_gain)(void *,uint64_t,uint16_t level,uint16_t maximum);
    bool (*mute)(void *,uint64_t,bool);
    bool (*close)(void *,uint64_t);
} risc_audio_pcm_api_v1;

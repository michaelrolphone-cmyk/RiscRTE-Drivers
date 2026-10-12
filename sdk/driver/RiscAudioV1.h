#pragma once
/* Generic public PCM contracts. Layout-compatible with the deployed audio
 * application interfaces; type names carry no device identity. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define RISC_AUDIO_OUTPUT_CAPABILITY "audio.output"
#define RISC_AUDIO_INPUT_CAPABILITY "audio.input"
#define RISC_AUDIO_MAX_FRAMES 256u
typedef struct {
 uint32_t api_version,struct_size;void *context;
 bool (*open)(void *,uint32_t rate_hz,uint8_t channels);
 bool (*write)(void *,const int16_t *,size_t frames);
 bool (*set_gain)(void *,uint16_t level,uint16_t maximum);
 bool (*silence)(void *);
 bool (*close)(void *);
} risc_audio_output_api_v1;
typedef struct {
 uint32_t api_version,struct_size;void *context;
 bool (*open)(void *,uint32_t rate_hz);
 bool (*read)(void *,int16_t *,size_t frames,size_t *read);
 /* Mean absolute magnitude of the last successful mono read (not RMS). */
 bool (*level)(void *,uint16_t *mean_absolute);
 bool (*close)(void *);
} risc_audio_input_api_v1;

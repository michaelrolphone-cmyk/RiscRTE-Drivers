#pragma once
#include "RiscProviderV2.h"
#include "RiscAudioPcmV1.h"
#include "RiscAudioV1.h"
#include <string.h>
static const risc_audio_pcm_api_v1 *pcm;
static uint64_t stream;
static bool started,failed;
static bool close_stream(void *ctx) {
 (void)ctx;
 if(stream){failed=true;if(!pcm->close(pcm->context,stream))return false;stream=0;}
 failed=false;return started;
}
static bool quiesce(void) {
 if(stream&&!close_stream(NULL))return false;
 started=false;pcm=NULL;failed=false;return true;
}
static void stop(void){(void)quiesce();}
static bool start(const risc_provider_dependency_v1 *deps,size_t count) {
 if(started||stream||!deps||count!=1||!deps[0].capability_id||
    strcmp(deps[0].capability_id,RISC_AUDIO_PCM_CAPABILITY)||deps[0].api_version!=1||!deps[0].api)return false;
 const risc_audio_pcm_api_v1 *p=deps[0].api;
 if(p->api_version!=1||p->struct_size<sizeof(*p)||!p->open||!p->write||!p->read||!p->set_gain||!p->mute||!p->close)return false;
 pcm=p;started=true;failed=false;return true;
}

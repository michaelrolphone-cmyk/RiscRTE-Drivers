/* Generic audio.output adapter: channel mapping/policy, no device knowledge. */
#include "../pcm_audio_common/client.h"
static uint8_t channels;
static uint16_t gain=1,maximum=1;
static bool open_out(void *ctx,uint32_t rate,uint8_t ch) {
 (void)ctx;if(!started||stream||(ch!=1&&ch!=2))return false;
 if(!pcm->open(pcm->context,RISC_AUDIO_PCM_OUTPUT,rate,&stream)||!stream){failed=true;return false;}
 channels=ch;
 if(!pcm->set_gain(pcm->context,stream,gain,maximum)){failed=true;return false;}
 failed=false;return true;
}
static bool write_out(void *ctx,const int16_t *samples,size_t frames) {
 (void)ctx;if(!started||failed||!stream||!samples||!frames||frames>RISC_AUDIO_MAX_FRAMES)return false;
 int16_t stereo[512];
 for(size_t i=0;i<frames;i++){stereo[2*i]=samples[i*channels];stereo[2*i+1]=channels==1?stereo[2*i]:samples[i*2+1];}
 size_t done=0;
 if(!pcm->mute(pcm->context,stream,false)||!pcm->write(pcm->context,stream,stereo,frames,&done,40)||done!=frames){failed=true;return false;}
 return true;
}
static bool volume(void *ctx,uint16_t value,uint16_t max) {
 (void)ctx;if(!started||failed||!max||value>max)return false;
 if(stream&&!pcm->set_gain(pcm->context,stream,value,max)){failed=true;return false;}
 gain=value;maximum=max;return true;
}
static bool silence(void *ctx) {
 (void)ctx;if(!started||failed)return false;
 if(stream&&!pcm->mute(pcm->context,stream,true)){failed=true;return false;}
 return true;
}
static bool output_start(const risc_provider_dependency_v1 *deps,size_t count) {
 if(!start(deps,count))return false;gain=maximum=1;channels=0;return true;
}
static const risc_audio_output_api_v1 api={1,sizeof(api),NULL,open_out,write_out,volume,silence,close_stream};
static const risc_driver_v2 descriptor={2,sizeof(descriptor),"pcm-audio-output",RISC_AUDIO_OUTPUT_CAPABILITY,1,&api,output_start,stop,quiesce};
__attribute__((visibility("default"))) const risc_driver_v2 *t5_driver_get(uint32_t v){return v==2?&descriptor:NULL;}

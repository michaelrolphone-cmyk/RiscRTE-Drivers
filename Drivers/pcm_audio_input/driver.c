/* Generic audio.input adapter: stereo downmix and deployed level semantics. */
#include "../pcm_audio_common/client.h"
static uint16_t mean_absolute;
static bool open_in(void *ctx,uint32_t rate) {
 (void)ctx;if(!started||stream)return false;
 if(!pcm->open(pcm->context,RISC_AUDIO_PCM_INPUT,rate,&stream)||!stream){failed=true;return false;}
 failed=false;mean_absolute=0;return true;
}
static bool read_in(void *ctx,int16_t *samples,size_t frames,size_t *got) {
 (void)ctx;if(got)*got=0;
 if(!started||failed||!stream||!samples||!got||!frames||frames>RISC_AUDIO_MAX_FRAMES)return false;
 int16_t stereo[512];size_t count=0;
 if(!pcm->read(pcm->context,stream,stereo,frames,&count,40)||count>frames){failed=true;return false;}
 uint32_t sum=0;
 for(size_t i=0;i<count;i++) {
  int32_t value=((int32_t)stereo[i*2]+stereo[i*2+1])/2;
  samples[i]=(int16_t)value;sum+=(uint32_t)(value<0?-value:value);
 }
 mean_absolute=count?(uint16_t)(sum/count):0;*got=count;return true;
}
static bool level(void *ctx,uint16_t *out){(void)ctx;if(!started||failed||!stream||!out)return false;*out=mean_absolute;return true;}
static const risc_audio_input_api_v1 api={1,sizeof(api),NULL,open_in,read_in,level,close_stream};
static const risc_driver_v2 descriptor={2,sizeof(descriptor),"pcm-audio-input",RISC_AUDIO_INPUT_CAPABILITY,1,&api,start,stop,quiesce};
__attribute__((visibility("default"))) const risc_driver_v2 *t5_driver_get(uint32_t v){return v==2?&descriptor:NULL;}

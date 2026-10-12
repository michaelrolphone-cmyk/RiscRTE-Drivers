#include "RiscProviderV2.h"
#include "RiscAudioV1.h"
#include "codec_model.h"
extern const risc_driver_v2 *codec_driver_get(uint32_t);
extern const risc_driver_v2 *output_driver_get(uint32_t);
extern const risc_driver_v2 *input_driver_get(uint32_t);
int main(void){
 const risc_driver_v2 *physical=codec_driver_get(2),*output=output_driver_get(2),*input=input_driver_get(2);
 assert(physical&&output&&input);
 const risc_audio_output_api_v1 *o=output->capability;
 const risc_audio_input_api_v1 *i=input->capability;
 risc_provider_dependency_v1 dep={"audio.pcm",1,physical->capability};
 assert(!input->start(NULL,0));assert(!output->start(NULL,0));
 for(unsigned order=0;order<2;order++){
  assert(physical->start(deps_,4)&&input->start(&dep,1)&&output->start(&dep,1));
  assert(o->set_gain(o->context,1,2));
  if(order){assert(i->open(i->context,16000)&&o->open(o->context,16000,1));}
  else {assert(o->open(o->context,16000,1));assert(!i->open(i->context,8000));assert(i->open(i->context,16000));}
  assert(raw_opens_==order+1&&raw_owned_&&!amp_level_);
  int16_t mono[]={1000,-3000,32767,-32768};
  assert(o->write(o->context,mono,4)&&amp_level_);
  assert(wire_[0]==500&&wire_[1]==500&&wire_[2]==-1500&&wire_[3]==-1500&&wire_[6]==-16384);
  assert(o->silence(o->context)&&!amp_level_);assert(o->write(o->context,mono,4)&&amp_level_);
  int16_t record[256];size_t got=0;
  assert(i->read(i->context,record,256,&got)&&!got);assert(i->read(i->context,record,256,&got)&&!got);
  assert(i->read(i->context,record,256,&got)&&got==256&&record[0]==-500);
  uint16_t level=0;assert(i->level(i->context,&level)&&level==500);
  assert(i->close(i->context)&&input->quiesce()&&raw_owned_);
  assert(o->close(o->context)&&output->quiesce()&&!raw_owned_&&!amp_level_&&physical->quiesce());
 }
 assert(physical->start(deps_,4)&&output->start(&dep,1));
 fail_raw_open_=retain_raw_open_=fail_raw_close_=true;
 assert(!o->open(o->context,16000,2)&&raw_owned_);
 assert(!output->quiesce());fail_raw_open_=retain_raw_open_=fail_raw_close_=false;
 assert(output->quiesce()&&physical->quiesce()&&!raw_owned_);
 assert(physical->start(deps_,4)&&input->start(&dep,1)&&output->start(&dep,1));
 assert(o->open(o->context,22050,2)&&i->open(i->context,22050));
 short_write_=true;int16_t stereo[32]={0};assert(!o->write(o->context,stereo,16));
 size_t got=99;assert(!i->read(i->context,stereo,16,&got)&&got==0);
 fail_raw_close_=true;assert(input->quiesce());assert(!output->quiesce()&&raw_owned_);
 fail_raw_close_=false;short_write_=false;assert(output->quiesce()&&physical->quiesce());
 assert(!amp_owned_&&!owned_[0]&&!owned_[1]&&!raw_owned_);
 puts("Actual generic input/output adapters + ES8311/ES7210 provider: both open orders, clock conflict, downmix/gain/mute, duplex, cleanup-token and cross-direction failure PASS");
}

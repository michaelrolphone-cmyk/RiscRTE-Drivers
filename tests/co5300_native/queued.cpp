#include <unistd.h>
#include <sys/wait.h>
#define main original_native_suite
#include "test.cpp"
#undef main
static void queuedFrame(unsigned salt=0){
#if RISC_PERFORMANCE_TRACE
 const auto frames=RiscPerf::data.counts[34];const auto accepted=RiscPerf::data.values[34],processed=RiscPerf::data.values[36];
 const auto elapsed=RiscPerf::data.durations_us[34],windowTime=RiscPerf::data.durations_us[35],gapTime=RiscPerf::data.durations_us[36];
#endif
 std::vector<uint16_t> rowPixels(TestWidth);
 for(unsigned y=0;y<TestHeight;++y){
  for(unsigned x=0;x<TestWidth;++x)rowPixels[x]=uint16_t(y*TestWidth+x+salt);
  assert(api->row(y,rowPixels.data(),rowPixels.size()));
  if((y+1)%RISC_CO5300_STRIPE_ROWS==0&&y+1<TestHeight&&salt!=99&&state.doubleDma)assert(state.queued||state.shadowComplete);
 }
 assert(!state.inflight&&!state.queued&&state.frameComplete);
#if RISC_PERFORMANCE_TRACE
 assert(RiscPerf::data.counts[34]==frames+1&&RiscPerf::data.values[34]==accepted+TestHeight);
 assert(RiscPerf::data.values[36]==processed+(TestHeight+state.stripeRows-1)/state.stripeRows);
 assert(RiscPerf::data.durations_us[34]-elapsed==(RiscPerf::data.durations_us[35]-windowTime)+(RiscPerf::data.durations_us[36]-gapTime));
#endif
}
int main(){
 using namespace model;
 clear();open();assert(state.doubleDma);const auto start=packets.size();queuedFrame();checkPixels(start,false);
 const auto full=pixelCount();queuedFrame();assert(pixelCount()==full); // unchanged skips
 queuedFrame(3);assert(pixelCount()==full+TestWindows);
 // A changed first stripe is pending when following equal stripes are packed.
 std::vector<uint16_t> mixed(TestWidth);auto mixedBefore=pixelCount();
 for(unsigned y=0;y<TestHeight;++y){for(unsigned x=0;x<TestWidth;++x)mixed[x]=uint16_t(y*TestWidth+x+(y<RISC_CO5300_STRIPE_ROWS?4:3));assert(api->row(y,mixed.data(),mixed.size()));}
 assert(pixelCount()==mixedBefore+1&&!state.inflight);closed();
 // Brightness during an incomplete first frame drains DMA but stays dark.
 clear();open();std::vector<uint16_t> partial(TestWidth,2);
 for(unsigned y=0;y<RISC_CO5300_STRIPE_ROWS;++y)assert(api->row(y,partial.data(),partial.size()));
 assert(state.queued);assert(api->brightness(80));assert(!state.queued&&!state.frameComplete&&!lit());
 closed();clear();open();auto fresh=pixelCount();queuedFrame();assert(pixelCount()==fresh+TestWindows);closed();
 // Fill invalidates cached-panel authority before a pending DMA timeout;
 // neither staging buffer nor shadow bytes may be rewritten on that refusal.
 clear();open();queuedFrame();std::vector<uint16_t> fillPixels(TestWidth,42);
 for(unsigned y=0;y<RISC_CO5300_STRIPE_ROWS;++y)assert(api->row(y,fillPixels.data(),fillPixels.size()));
 assert(state.queued&&state.shadowComplete);
 auto fillDma=std::vector<uint8_t>(static_cast<uint8_t*>(allocation),static_cast<uint8_t*>(allocation)+2*TestPairBytes);
 auto fillShadow=std::vector<uint8_t>(state.completedPixels,state.completedPixels+size_t(TestWidth)*TestHeight*2);
 fault(End);assert(!fill(0));assert(!state.shadowComplete&&state.failed&&state.inflight);
 assert(std::memcmp(allocation,fillDma.data(),fillDma.size())==0);
 assert(std::memcmp(state.completedPixels,fillShadow.data(),fillShadow.size())==0);closed();
 // A failed double allocation selects original single-buffer transport.
 clear();fault(Allocate);open();assert(!state.doubleDma);queuedFrame(99);closed();
 clear();maxAllocation=0;{const auto c=config();const auto h=hardware(c);assert(!api->open(h,2));assert(!allocation);closed();}
 // Every smaller supported allocation fixes a smaller synchronous window
 // before startup fill. Exact row bytes/rotation/remainder and cache still hold.
 for(unsigned limit=RISC_CO5300_STRIPE_ROWS/2;limit>=2;limit=(limit/2)&~1u){
  clear();maxAllocation=size_t(TestWidth)*limit*2;open();assert(!state.doubleDma&&state.stripeRows<=limit);
  const auto first=packets.size();queuedFrame();checkPixels(first,false,0,TestHeight,state.stripeRows);
  auto sent=pixelCount();queuedFrame();assert(pixelCount()==sent);closed();
  if(limit==2)break;
 }
 // Every queued window failure retains descriptors/both buffers. No row writes
 // follow observed timeout. Checked cleanup reaps before releasing allocation.
 for(unsigned window=0;window<TestWindows;++window){
  clear();open();std::vector<uint16_t> p(TestWidth,7);
  for(unsigned y=0;y<(window+1)*RISC_CO5300_STRIPE_ROWS&&y<TestHeight;++y){
   if(y+1==TestHeight)break;
   assert(api->row(y,p.data(),p.size()));
  }
  if(!state.queued){closed();continue;}
  fault(End);auto before=std::vector<uint8_t>(static_cast<uint8_t*>(allocation),static_cast<uint8_t*>(allocation)+2*TestPairBytes);
  assert(!api->brightness(0));assert(state.failed&&state.inflight&&state.queued);
  const auto attempted=calls[Start];assert(!api->row(0,p.data(),p.size()));
  assert(calls[Start]==attempted&&std::memcmp(allocation,before.data(),before.size())==0);
  closed();
 }
 // Fail each command/queue submission and each completion in a fresh full frame.
 for(auto op:{Start,End})for(unsigned at=1;at<=TestWindows*TestWindowPackets;++at){
  clear();open();fault(op,at);std::vector<uint16_t> pixels(TestWidth,11);bool good=true;
  for(unsigned y=0;y<TestHeight&&good;++y)good=api->row(y,pixels.data(),pixels.size());
  assert(!good&&state.failed);
  auto saved=std::vector<uint8_t>(static_cast<uint8_t*>(allocation),static_cast<uint8_t*>(allocation)+2*TestPairBytes);
  unsigned starts=calls[Start];assert(!api->row(0,pixels.data(),pixels.size()));assert(starts==calls[Start]);
  assert(std::memcmp(allocation,saved.data(),saved.size())==0);closed();
 }
 // Proven DMA failure returns custody but must not bless its shadow bytes.
 clear();open();std::vector<uint16_t> badPixels(TestWidth,12);
 for(unsigned y=0;y<RISC_CO5300_STRIPE_ROWS;++y)assert(api->row(y,badPixels.data(),badPixels.size()));
 auto shadowBefore=std::vector<uint8_t>(state.completedPixels,state.completedPixels+size_t(TestWidth)*TestHeight*2);
 dmaFailure=true;endError=ESP_ERR_INVALID_STATE;fault(End);assert(!api->brightness(0));
 assert(std::memcmp(state.completedPixels,shadowBefore.data(),shadowBefore.size())==0);
 assert(state.failed&&!state.inflight&&!state.queued&&!state.uncertain);closed();
 // Wrong descriptor identity remains terminal even if result says success.
 // Use a child process so deliberate retained state cannot contaminate next case.
 pid_t child=fork();assert(child>=0);
 if(!child){clear();open();std::vector<uint16_t> idPixels(TestWidth,1);for(unsigned y=0;y<RISC_CO5300_STRIPE_ROWS;++y)assert(api->row(y,idPixels.data(),idPixels.size()));wrongResult=true;assert(!api->brightness(0));assert(state.uncertain&&state.inflight&&state.queued);assert(!api->close());_exit(0);}
 int status=0;assert(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0);
 clear();open();std::vector<uint16_t> p(TestWidth,1);
 for(unsigned y=0;y<RISC_CO5300_STRIPE_ROWS;++y)assert(api->row(y,p.data(),p.size()));
 fault(End);endError=ESP_FAIL;assert(!api->brightness(0));assert(state.uncertain&&state.queued&&state.inflight);
 auto saved=std::vector<uint8_t>(static_cast<uint8_t*>(allocation),static_cast<uint8_t*>(allocation)+2*TestPairBytes);
 assert(!api->close());assert(!api->row(0,p.data(),p.size()));assert(std::memcmp(allocation,saved.data(),saved.size())==0);
 puts("PASS queued double-buffer byte oracle, unchanged reuse, allocation fallback, per-window timeout and terminal retention");
 // Unknown custody is deliberately not freed. Leak detection is separately disabled.
 return 0;
}

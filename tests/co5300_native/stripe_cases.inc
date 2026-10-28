int main(){
 using namespace model;
 uint16_t line[TestWidth];for(unsigned x=0;x<TestWidth;++x)line[x]=uint16_t(x);
 clear();open();assert(delays==bootDelays());checkPixels(ExpectedInit.size(),true);assert(pixelCount()==TestWindows);
 assert(api->brightness(93)&&lit()==0);
 auto mark=packets.size();frame();checkPixels(mark,false);assert(state.frameComplete&&lit()==1);
#if RISC_PERFORMANCE_TRACE
 assert(RiscPerf::data.counts[34]==1&&RiscPerf::data.values[34]==TestHeight);
 assert(RiscPerf::data.values[35]==TestWindows&&RiscPerf::data.values[36]==TestWindows);
 assert(RiscPerf::data.durations_us[34]==RiscPerf::data.durations_us[35]+RiscPerf::data.durations_us[36]);
#else
 assert(clockReads==0);
#endif
 mark=packets.size();frame();checkPixels(mark,false);closed();
 // Partial even-start stripes retain offset/rotation coverage without claiming
 // a complete frame; the actual final remainder can be shorter than selection.
 for(unsigned first:{2u,TestHeight/2u&~1u,TestHeight-2u}){
  clear();open();const auto mark=packets.size();const unsigned end=std::min(TestHeight,first+unsigned(RISC_CO5300_STRIPE_ROWS));
  for(unsigned y=first;y<end;++y){for(unsigned x=0;x<TestWidth;++x)line[x]=uint16_t(y*TestWidth+x);assert(api->row(y,line,TestWidth));}
  checkPixels(mark,false,first,end);assert(!state.frameComplete&&lit()==0);closed();
 }
 // Invalid ordering neither submits nor mutates accepted storage. Inputs are
 // copied at acceptance; no caller buffer is borrowed across row calls.
 for(unsigned x=0;x<TestWidth;++x)line[x]=uint16_t(x);
 clear();open();const auto boot=packets.size();assert(!api->row(1,line,TestWidth));
 assert(api->row(0,line,TestWidth));std::fill(std::begin(line),std::end(line),0xffff);
 const std::vector<uint8_t> accepted(state.dma,state.dma+TestPairBytes);auto callsBefore=calls;
 assert(!api->row(2,line,TestWidth)&&calls==callsBefore&&std::equal(accepted.begin(),accepted.end(),state.dma));
 for(unsigned y=1;y<TestHeight;++y){for(unsigned x=0;x<TestWidth;++x)line[x]=uint16_t(y*TestWidth+x);assert(api->row(y,line,TestWidth));std::fill(std::begin(line),std::end(line),0xffff);}
 checkPixels(boot,false);closed();
 // Owner mismatch is not an opportunity to mutate the pending stripe.
 clear();open();assert(api->row(0,line,TestWidth));callsBefore=calls;core=1;
 assert(!api->row(1,line,TestWidth)&&!api->brightness(0)&&!api->close()&&calls==callsBefore);core=0;closed();
 // Fail each window/optional RAMWR/pixel boundary. A retained descriptor and
 // owned DMA buffer remain byte-identical despite subsequent caller mutation.
 for(unsigned phase=1;phase<=TestWindowPackets;++phase){
  clear();open();assert(api->brightness(80));frame(RISC_CO5300_STRIPE_ROWS-1);
  fault(End,phase);assert(!api->row(RISC_CO5300_STRIPE_ROWS-1,line,TestWidth)&&flight&&!state.frameComplete&&lit()==0);
  const std::vector<uint8_t> held(state.dma,state.dma+TestPairBytes);const auto descriptorCopy=state.transaction;
  std::fill(std::begin(line),std::end(line),0xbeef);callsBefore=calls;
  assert(!api->row(0,line,TestWidth)&&!api->brightness(100)&&calls==callsBefore);
  assert(std::equal(held.begin(),held.end(),state.dma)&&!std::memcmp(&descriptorCopy,&state.transaction,sizeof(descriptorCopy)));
  fault(End);assert(!api->close()&&allocation&&flight);closed();assert(lit()==0);
 }
 // Final short stripe cannot declare frame completion when its transfer fails.
 clear();open();assert(api->brightness(80));frame(TestHeight-1);fault(End,TestWindowPackets);
 assert(!api->row(TestHeight-1,line,TestWidth)&&!state.frameComplete&&lit()==0);closed();assert(lit()==0);
 // Partial frames may be cancelled/closed without falsely opening visibility.
 for(unsigned n=1;n<RISC_CO5300_STRIPE_ROWS;++n){clear();open();assert(api->brightness(80));frame(n);assert(!state.frameComplete&&lit()==0);closed();}
 for(auto op:{Init,Add,Acquire,Allocate,Configure,Level}){clear();fault(op);const auto c=config();assert(!api->open(hardware(c),2)&&state.owned);closed();}
 // Completed DMA error is reclaimable, not an unknown outstanding transaction.
 clear();open();frame(RISC_CO5300_STRIPE_ROWS-1);dmaFailure=true;fault(End,TestWindowPackets);
 assert(!api->row(RISC_CO5300_STRIPE_ROWS-1,line,TestWidth)&&!flight&&state.failed);closed();
 // Unclassified completion error: no buffer reuse, repeat polling or cleanup.
 clear();open();frame(RISC_CO5300_STRIPE_ROWS-1);endError=ESP_FAIL;fault(End);
 assert(!api->row(RISC_CO5300_STRIPE_ROWS-1,line,TestWidth)&&state.uncertain);
 const std::vector<uint8_t> held(state.dma,state.dma+TestPairBytes);callsBefore=calls;
 assert(!api->row(0,line,TestWidth)&&!api->brightness(100)&&!api->close()&&!api->close()&&calls==callsBefore);
 assert(std::equal(held.begin(),held.end(),state.dma));
 std::free(allocation);allocation=nullptr;state=PanelState{};bus=device=acquired=flight=false;
 std::printf("Native stripes: %ux%u rows=%u windows=%u rotation=%u exact frame/window/copy/brightness/fault custody PASS\n",TestWidth,TestHeight,RISC_CO5300_STRIPE_ROWS,TestWindows,RISC_CO5300_PIXEL_ROTATION);
}

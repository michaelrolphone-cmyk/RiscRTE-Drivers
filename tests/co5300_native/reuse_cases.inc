// Full physical panel is the oracle, not cache memory or transaction count.
static std::vector<uint16_t> panel(TestWidth*TestHeight);
static void replayPanel(size_t first){
 unsigned x0=0,x1=TestWidth-1,y0=0,y1=TestHeight-1;
 for(size_t i=first;i<model::packets.size();++i){const auto&p=model::packets[i];if(!p.complete)continue;
  if(p.command==2&&(p.address==0x2a00||p.address==0x2b00)){
   assert(p.bytes.size()==4);unsigned lo=(p.bytes[0]<<8)|p.bytes[1],hi=(p.bytes[2]<<8)|p.bytes[3];
   if(p.address==0x2a00){x0=lo-TestOffsetX;x1=hi-TestOffsetX;}else{y0=lo-TestOffsetY;y1=hi-TestOffsetY;}
  }else if(p.command==0x32){assert(x0==0&&x1==TestWidth-1&&y1<TestHeight&&y0<=y1);assert(p.bytes.size()==(y1-y0+1)*TestWidth*2);
   for(unsigned y=y0;y<=y1;++y)for(unsigned x=0;x<TestWidth;++x){size_t k=(y-y0)*TestWidth+x;panel[y*TestWidth+x]=uint16_t(p.bytes[k*2]<<8|p.bytes[k*2+1]);}
  }
 }
}
static void sendImage(const std::vector<uint16_t>&image,unsigned first=0,unsigned end=TestHeight){
 for(unsigned y=first;y<end;++y)assert(api->row(y,image.data()+size_t(y)*TestWidth,TestWidth));
}
static void assertImage(const std::vector<uint16_t>&image){
 for(size_t i=0;i<image.size();++i){size_t p=RISC_CO5300_PIXEL_ROTATION==180?image.size()-1-i:i;assert(panel[p]==image[i]);}
}
static void resetReuse(){model::clear();assert(!model::shadowAllocation);model::shadowOom=false;model::shadowAllocations=0;std::fill(panel.begin(),panel.end(),0xdead);}
int main(){using namespace model;
 std::vector<uint16_t> image(TestWidth*TestHeight);for(size_t i=0;i<image.size();++i)image[i]=uint16_t(i*73+11);
 resetReuse();open();assert(delays==bootDelays());checkPixels(ExpectedInit.size(),true);assert(!state.shadowComplete&&!shadowAllocation);assert(api->brightness(91));
 size_t mark=packets.size();sendImage(image);replayPanel(mark);assertImage(image);assert(state.shadowComplete&&state.frameComplete&&lit()==1&&shadowAllocations==1);
 unsigned pixels=pixelCount();mark=packets.size();sendImage(image);assert(packets.size()==mark&&pixelCount()==pixels);assertImage(image);
#if RISC_PERFORMANCE_TRACE
 assert(RiscPerf::data.counts[34]==2&&RiscPerf::data.values[34]==2*TestHeight);
 assert(RiscPerf::data.values[35]==TestWindows&&RiscPerf::data.values[36]==2*TestWindows);
 assert(RiscPerf::data.durations_us[34]==RiscPerf::data.durations_us[35]+RiscPerf::data.durations_us[36]);
#endif
 // First, interior and final short stripe; restoring old bytes is a real write.
 for(unsigned y:{0u,TestHeight/2u,TestHeight-1u}){size_t p=size_t(y)*TestWidth+TestWidth/2;image[p]^=0x55aa;mark=packets.size();pixels=pixelCount();sendImage(image);assert(pixelCount()==pixels+1);replayPanel(mark);assertImage(image);}
 // Partial windows beginning at any accepted even row use physical coordinates,
 // even if not aligned to configured stripe groups.
 for(unsigned first:{2u,TestHeight/2u&~1u,TestHeight-2u}){unsigned end=std::min(TestHeight,first+unsigned(RISC_CO5300_STRIPE_ROWS));
  for(unsigned y=first;y<end;++y)image[size_t(y)*TestWidth]^=0x3210;
  mark=packets.size();sendImage(image,first,end);replayPanel(mark);assertImage(image);
  mark=packets.size();sendImage(image,first,end);assert(mark==packets.size());
 }
 // Internal fill invalidates old shadow; subsequent complete image restores it.
 assert(fill(0));assert(!state.shadowComplete);mark=packets.size();pixels=pixelCount();sendImage(image);assert(pixelCount()==pixels+TestWindows);replayPanel(mark);assertImage(image);closed();assert(!shadowAllocation);
 // Partial initial frames never authorize skipping, even repeated identical ones.
 resetReuse();open();mark=packets.size();sendImage(image,0,RISC_CO5300_STRIPE_ROWS);pixels=pixelCount();assert(!state.shadowComplete);sendImage(image,0,RISC_CO5300_STRIPE_ROWS);assert(pixelCount()==pixels+1&&!state.shadowComplete);closed();
 // Allocation failure is attempted once and leaves original full transfer path.
 resetReuse();shadowOom=true;open();sendImage(image);assert(!shadowAllocation&&!state.shadowComplete&&state.frameComplete);pixels=pixelCount();sendImage(image);assert(pixelCount()==pixels+TestWindows&&shadowAllocations==1);closed();
 // Failure after a previously reusable frame must not update shadow or mutate
 // held DMA on later refused rows, including unknown completion authority.
 for(unsigned phase=1;phase<=TestWindowPackets;++phase)for(bool unknown:{false,true}){resetReuse();open();sendImage(image);auto shadow=std::vector<uint8_t>(state.completedPixels,state.completedPixels+image.size()*2);image[0]^=0x77;
  sendImage(image,0,RISC_CO5300_STRIPE_ROWS-1);endError=unknown?ESP_FAIL:ESP_ERR_TIMEOUT;fault(End,phase);
  assert(!api->row(RISC_CO5300_STRIPE_ROWS-1,image.data()+(RISC_CO5300_STRIPE_ROWS-1)*TestWidth,TestWidth));
  assert(!std::memcmp(shadow.data(),state.completedPixels,shadow.size()));auto dma=std::vector<uint8_t>(state.dma,state.dma+TestPairBytes);auto before=calls;
  assert(!api->row(0,image.data(),TestWidth)&&!api->brightness(99)&&calls==before);assert(!std::memcmp(dma.data(),state.dma,dma.size()));
  if(unknown){assert(!api->close()&&!api->close()&&calls==before);std::free(shadowAllocation);shadowAllocation=nullptr;std::free(allocation);allocation=nullptr;state=PanelState{};bus=device=acquired=flight=false;}
  else {fault(End);assert(!api->close()&&shadowAllocation&&allocation);closed();assert(!shadowAllocation);}
 }
 // Rejected owner/order calls leave both buffers and allocation authority alone.
 resetReuse();open();sendImage(image);sendImage(image,0,1);{auto cache=std::vector<uint8_t>(state.completedPixels,state.completedPixels+image.size()*2);auto dma=std::vector<uint8_t>(state.dma,state.dma+TestPairBytes);auto before=calls;unsigned allocated=shadowAllocations;
 assert(!api->row(2,image.data(),TestWidth));core=1;assert(!api->row(1,image.data(),TestWidth)&&!api->close());core=0;
 assert(calls==before&&allocated==shadowAllocations&&!std::memcmp(cache.data(),state.completedPixels,cache.size())&&!std::memcmp(dma.data(),state.dma,dma.size()));}closed();
 // Cleanup retries retain shadow until the existing bus/pad custody is closed.
 for(auto op:{Remove,Free,Reset}){resetReuse();open();sendImage(image);fault(op);assert(!api->close()&&shadowAllocation);closed();assert(!shadowAllocation);}
 // A fill invalidates before any command, including a partial failing fill.
 resetReuse();open();sendImage(image);fault(End,TestWindowPackets);assert(!fill(0)&&!state.shadowComplete);closed();assert(!shadowAllocation);
 // Reopen cannot reuse prior panel/shadow authority.
 resetReuse();open();pixels=pixelCount();frame();assert(pixelCount()==pixels+TestWindows);closed();
 std::printf("Exact stripe reuse: %ux%u rows=%u rotation=%u panel/partial/remainder/OOM/fill/retained/lifecycle PASS\n",TestWidth,TestHeight,RISC_CO5300_STRIPE_ROWS,RISC_CO5300_PIXEL_ROTATION);
}

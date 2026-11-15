#define main native_owning_main
#include "test.cpp"
#undef main
static unsigned compositionRows,compositionPixels,compositionPackets;
static bool compositionReused;
extern "C" bool composition_open(){model::clear();assert(!model::shadowAllocation);open();assert(!state.completedPixels&&!state.shadowComplete);compositionRows=0;compositionPixels=model::pixelCount();compositionPackets=model::packets.size();compositionReused=false;return true;}
extern "C" bool composition_row(uint32_t y,const uint16_t*p,size_t n){
 if(!y){compositionRows=0;compositionPixels=model::pixelCount();compositionPackets=model::packets.size();compositionReused=state.shadowComplete;}
 assert(y==compositionRows&&n==TestWidth);bool ok=api->row(y,p,n);if(!ok)return false;++compositionRows;
 assert(state.frameComplete==(compositionReused||compositionRows==TestHeight));
 unsigned expected=compositionReused?0:(compositionRows==TestHeight?TestWindows:compositionRows/RISC_CO5300_STRIPE_ROWS);
 assert(model::pixelCount()-compositionPixels==expected);return true;
}
extern "C" bool composition_level(uint8_t v){if(v&&compositionRows)assert(compositionRows==TestHeight&&state.frameComplete);return api->brightness(v);}
extern "C" bool composition_close(){return api->close();}
extern "C" void composition_check_complete(){
 assert(compositionRows==TestHeight&&state.frameComplete&&state.shadowComplete);
 if(compositionReused)assert(model::packets.size()==compositionPackets);
 else{checkPixels(compositionPackets,false);unsigned last=0;for(const auto&p:model::packets)if(p.command==0x32){assert(p.complete);last=p.bytes.size();}
 const unsigned tail=TestHeight%RISC_CO5300_STRIPE_ROWS;assert(last==TestWidth*2*(tail?tail:RISC_CO5300_STRIPE_ROWS));}
 assert(model::lit()>=1);
}
extern "C" void composition_assert_closed(){assert(!state.owned&&!state.completedPixels&&!state.shadowComplete&&!model::shadowAllocation);}
extern "C" void composition_fail_close(){model::fault(model::End);}
extern "C" void composition_assert_retained(){assert(state.owned&&state.closing&&state.completedPixels&&model::shadowAllocation&&state.shadowComplete&&state.inflight);}

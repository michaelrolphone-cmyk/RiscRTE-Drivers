#include <unistd.h>
#include <sys/wait.h>
#define main historical_native_suite
#include "test.cpp"
#undef main
// Independent register/panel-memory model: column state persists, row commands
// do not reset it, and RAMWR/RAMWRC obey their distinct cursor semantics.
static std::vector<uint16_t> reconstructedPanel(){
 unsigned xs=0,xe=0,ys=0,ye=0,x=0,y=0;bool cx=false,cy=false,cursor=false;
 std::vector<uint16_t> result(size_t(TestWidth)*TestHeight,0xbeef);
 for(const auto&p:model::packets){
  if(!p.complete)continue;
  if(p.command==2){
   unsigned reg=unsigned(p.address>>8);
   if(reg==1){cx=cy=cursor=false;continue;}
   if(reg==0x2a||reg==0x2b){assert(p.bytes.size()==4);unsigned first=(unsigned(p.bytes[0])<<8)|p.bytes[1],last=(unsigned(p.bytes[2])<<8)|p.bytes[3];assert(first<=last);
    if(reg==0x2a){xs=first;xe=last;cx=true;}else{ys=first;ye=last;cy=true;}}
   if(reg==0x2c){assert(cx&&cy);x=xs;y=ys;cursor=true;}
   continue;
  }
  assert(p.command==0x32&&cx&&cy);
  if(p.address==0x2c00){x=xs;y=ys;cursor=true;}else assert(p.address==0x3c00&&cursor);
  assert(xs==TestOffsetX&&xe==TestOffsetX+TestWidth-1&&ys>=TestOffsetY&&ye<TestOffsetY+TestHeight);
  for(size_t i=0;i<p.bytes.size();i+=2){assert(y<=ye);result[size_t(y-TestOffsetY)*TestWidth+x-TestOffsetX]=uint16_t(unsigned(p.bytes[i])<<8|p.bytes[i+1]);if(++x>xe){x=xs;++y;}}
 }
 return result;
}
static void checkPanel(unsigned salt=0,bool firstDirty=false){auto actual=reconstructedPanel();for(unsigned y=0;y<TestHeight;y++)for(unsigned x=0;x<TestWidth;x++){
 unsigned ly=RISC_CO5300_PIXEL_ROTATION==180?TestHeight-1-y:y,lx=RISC_CO5300_PIXEL_ROTATION==180?TestWidth-1-x:x;
 unsigned extra=firstDirty&&ly<state.stripeRows?1:0;assert(actual[size_t(y)*TestWidth+x]==uint16_t(ly*TestWidth+lx+salt+extra));}}
static unsigned dmaBuffers(){
#if RISC_CO5300_QUEUED_PIXELS
 return state.doubleDma?2:1;
#else
 return 1;
#endif
}
static bool send(unsigned salt=0,bool firstDirty=false){std::vector<uint16_t> pixels(TestWidth);for(unsigned y=0;y<TestHeight;y++){for(unsigned x=0;x<TestWidth;x++)pixels[x]=uint16_t(y*TestWidth+x+salt+(firstDirty&&y<state.stripeRows?1:0));if(!api->row(y,pixels.data(),pixels.size()))return false;}return true;}
int main(){using namespace model;
 clear();open();assert(state.columnsKnown);auto startupCols=registerCount(0x2a);assert(startupCols==1+unsigned(std::count_if(ExpectedInit.begin(),ExpectedInit.end(),[](const auto&i){return i.reg==0x2a;})));
 {const auto boot=reconstructedPanel();assert(std::all_of(boot.begin(),boot.end(),[](uint16_t v){return v==0;}));}
 auto n=packets.size();assert(send());checkPanel();assert(packets.size()-n==TestWindows*(2+RISC_CO5300_RAMWR_BEFORE_PIXELS));assert(registerCount(0x2a)==startupCols);
 auto unchanged=packets.size();assert(send());checkPanel();assert(packets.size()==unchanged);
 assert(send(3));checkPanel(3);assert(send(3,true));checkPanel(3,true);assert(registerCount(0x2a)==startupCols);
 assert(api->brightness(42)&&state.columnsKnown);assert(send(7));checkPanel(7);
 // Any unrecognized register/config mutation drops cached extent authority.
 assert(byte(0x36,0)&&!state.columnsKnown);auto columns=registerCount(0x2a);assert(send(8));checkPanel(8);assert(registerCount(0x2a)==columns+1&&state.columnsKnown);
 assert(command(0x01)&&!state.columnsKnown);columns=registerCount(0x2a);assert(send(9));checkPanel(9);assert(registerCount(0x2a)==columns+1);closed();assert(!state.columnsKnown);
 clear();open();assert(state.columnsKnown);assert(send(11));checkPanel(11);closed();
 // Effective smaller single-buffer fallback preserves complete byte mapping.
 #if RISC_CO5300_QUEUED_PIXELS
 for(unsigned limit=RISC_CO5300_STRIPE_ROWS;limit>=2;limit=(limit/2)&~1u){clear();maxAllocation=size_t(TestWidth)*limit*2;open();assert(state.stripeRows<=limit);auto c=registerCount(0x2a);assert(send(19));checkPanel(19);assert(registerCount(0x2a)==c);closed();if(limit==2)break;}
 #endif
 // Shadow allocation is optional: CASET authority is independent of pixel reuse.
 clear();shadowOom=true;open();auto shadowCols=registerCount(0x2a);assert(send(17));checkPanel(17);assert(!state.completedPixels);auto repeat=packets.size();assert(send(17));checkPanel(17);assert(packets.size()-repeat==TestWindows*(2+RISC_CO5300_RAMWR_BEFORE_PIXELS));assert(registerCount(0x2a)==shadowCols);closed();shadowOom=false;
 // Noncontiguous accepted stripe updates preserve every other panel pixel.
 clear();open();assert(send(15));auto expected=reconstructedPanel();auto partialCols=registerCount(0x2a);
 for(unsigned first:{2u,(TestHeight/2)&~1u,TestHeight-2u}){unsigned rows=std::min(state.stripeRows,TestHeight-first);std::vector<uint16_t> part(TestWidth,0x1234);
  for(unsigned y=first;y<first+rows;y++)assert(api->row(y,part.data(),part.size()));assert(finish());
  for(unsigned y=first;y<first+rows;y++)for(unsigned x=0;x<TestWidth;x++){unsigned py=RISC_CO5300_PIXEL_ROTATION==180?TestHeight-1-y:y;expected[size_t(py)*TestWidth+x]=0x1234;}
  assert(reconstructedPanel()==expected);assert(registerCount(0x2a)==partialCols);
 }closed();
 // Every actual command/submission/reap failure during a full changed frame.
 for(auto op:{Start,End})for(unsigned at=1;at<=TestWindows*(2+RISC_CO5300_RAMWR_BEFORE_PIXELS);at++){
  clear();open();fault(op,at);assert(!send(23)&&state.failed&&!state.columnsKnown);auto before=std::vector<uint8_t>(static_cast<uint8_t*>(allocation),static_cast<uint8_t*>(allocation)+dmaBuffers()*state.stripeBytes);unsigned starts=calls[Start];std::vector<uint16_t> row(TestWidth);assert(!api->row(0,row.data(),row.size()));assert(starts==calls[Start]&&!memcmp(allocation,before.data(),before.size()));closed();assert(!state.columnsKnown);
 }
 // CASET authority is not established by start, reap timeout or CS failure.
 for(auto op:{Start,End,Level}){clear();open();assert(byte(0x36,0));fault(op,op==Level?2:1);assert(!send(30)&&!state.columnsKnown&&state.failed);closed();}
 // Initial fill CASET failure must not become valid on a later cleanup reap.
 for(auto op:{Start,End}){clear();fault(op,unsigned(ExpectedInit.size())+1);const auto c=config();assert(!api->open(hardware(c),2)&&!state.columnsKnown);closed();}
 // Known/unknown completion fencing remains: unknown result cannot be retried away.
 pid_t child=fork();assert(child>=0);if(!child){clear();open();fault(End);endError=ESP_FAIL;assert(!send(40)&&state.uncertain&&!state.columnsKnown);auto starts=calls[Start];assert(!api->close()&&!state.columnsKnown&&calls[Start]==starts);_exit(0);}int status=0;assert(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0);
 printf("PASS persistent column register/panel oracle, full/dirty/rotation/fallback/reset/reopen, exhaustive transaction failures and retained authority; fullframe transactions=%u vsbaseline=%u\n",TestWindows*(2+RISC_CO5300_RAMWR_BEFORE_PIXELS),TestWindows*TestWindowPackets);
}

// Compile the actual bridge, SDK SPI/GPIO declarations and shared Runtime ABI.
#include "../../components/co5300/native/Co5300Display.cpp"
#include <cassert>
#include <vector>
#include <array>
#include <cstdlib>
#include <algorithm>
#include <utility>

// Expectations derive from the selected build profile, not private backend state.
static constexpr unsigned TestWidth=RISC_CO5300_WIDTH,TestHeight=RISC_CO5300_HEIGHT;
static constexpr unsigned TestOffsetX=RISC_CO5300_OFFSET_X,TestOffsetY=RISC_CO5300_OFFSET_Y;
static constexpr unsigned TestPairPixels=TestWidth*RISC_CO5300_STRIPE_ROWS,TestPairBytes=TestPairPixels*2;
static constexpr unsigned TestWindows=(TestHeight+RISC_CO5300_STRIPE_ROWS-1)/RISC_CO5300_STRIPE_ROWS,TestWindowPackets=3+RISC_CO5300_RAMWR_BEFORE_PIXELS;
// Independent expected programs, including intentional startup brightness-zero
// replacements. Do not derive these expected bytes from InitCommands.
struct TestInitCommand {uint8_t reg;std::vector<uint8_t> bytes;unsigned delay=0;};
#if RISC_CO5300_TEST_INIT_175C
static const std::vector<TestInitCommand> ExpectedInit={
 {0x36,{0}}, {0x3a,{0x55}}, {0xfe,{0x20}}, {0x19,{0x10}}, {0x1c,{0xa0}},
 {0xfe,{0}}, {0xc4,{0x80}}, {0x3a,{0x55}}, {0x35,{0}}, {0x53,{0x20}},
 {0x51,{0}}, {0x63,{0xff}}, {0x2a,{0,6,1,0xd7}}, {0x2b,{0,0,1,0xd1},600},
 {0x11,{},600}, {0x29,{}}, {0x29,{}}
};
#else
static const std::vector<TestInitCommand> ExpectedInit={
 {0x11,{},120}, {0xfe,{0}}, {0xc4,{0x80}}, {0x3a,{0x55}}, {0x53,{0x20}},
 {0x63,{0xff}}, {0x51,{0}}, {0x29,{}}, {0x51,{0}}, {0x58,{0},10},
 {0x20,{}}, {0x36,{0}}
};
#endif
static const unsigned TestBootPackets=ExpectedInit.size()+TestWindows*TestWindowPackets;
static_assert(TestHeight>=6,"The lifecycle model exercises three distinct row pairs");
static std::vector<uint8_t> addressBytes(unsigned first,unsigned last){
 return {uint8_t(first>>8),uint8_t(first),uint8_t(last>>8),uint8_t(last)};
}
static std::vector<TickType_t> bootDelays(){
 std::vector<TickType_t> result={pdMS_TO_TICKS(10)+1,pdMS_TO_TICKS(RISC_CO5300_RESET_LOW_MS)+1,
                               pdMS_TO_TICKS(RISC_CO5300_RESET_RECOVERY_MS)+1};
 for(const auto& item:ExpectedInit)if(item.delay)result.push_back(pdMS_TO_TICKS(item.delay)+1);
 return result;
}

namespace RiscDiagnostics {
void line(const char* text){
 assert(text&&std::strlen(text)<112&&!std::strchr(text,'\n'));
 std::printf("DIAGNOSTIC_SINK %s\n",text);
}
}

struct spi_device_t{};
namespace model {
enum Op { Init,Add,Acquire,Allocate,Configure,Level,Start,End,Remove,Free,Reset,Count };
std::array<unsigned,Count> calls{},failAt{};
esp_err_t endError=ESP_ERR_TIMEOUT;
bool wrongResult=false;unsigned allocationFailures=0;size_t maxAllocation=SIZE_MAX;
bool dmaFailure=false,partialAdd=false,bus=false,device=false,acquired=false,flight=false,psramReady=true;
int core=0;TaskHandle_t task=reinterpret_cast<TaskHandle_t>(1);
spi_device_t handle;
spi_transaction_t* descriptor=nullptr;
void* allocation=nullptr;
#if RISC_CO5300_REUSE_UNCHANGED
void* shadowAllocation=nullptr;bool shadowOom=false;unsigned shadowAllocations=0;
#endif
std::array<uint32_t,49> levels{};
std::vector<int> resets;
std::vector<TickType_t> delays;
std::vector<std::pair<uint32_t,uint64_t>> resetLevels;
struct Packet {uint16_t command;uint64_t address;uint32_t flags;std::vector<uint8_t> bytes;bool complete=false;};
std::vector<Packet> packets;
uint64_t clockUs=0;unsigned clockReads=0;
bool failed(Op op){return ++calls[op]==failAt[op];}
void fault(Op op,unsigned after=1){failAt[op]=calls[op]+after;}
void correctOwner(){assert(core==0&&task==reinterpret_cast<TaskHandle_t>(1));}
uint64_t now(){++clockReads;return ++clockUs;}
bool owner(){return true;}
unsigned lit(){return std::count_if(packets.begin(),packets.end(),[](const auto& p){return p.command==2&&p.address==0x5100&&!p.bytes.empty()&&p.bytes[0]!=0;});}
unsigned registerCount(uint8_t reg){return std::count_if(packets.begin(),packets.end(),[reg](const auto& p){return p.command==2&&p.address==(uint64_t(reg)<<8);});}
unsigned pixelCount(){return std::count_if(packets.begin(),packets.end(),[](const auto& p){return p.command==0x32;});}
void clear(){
 assert(!state.owned&&!bus&&!device&&!acquired&&!flight&&!allocation);
 calls={};failAt={};wrongResult=false;allocationFailures=0;maxAllocation=SIZE_MAX;endError=ESP_ERR_TIMEOUT;dmaFailure=partialAdd=false;
 levels={};levels[12]=1;packets.clear();resets.clear();delays.clear();resetLevels.clear();clockUs=clockReads=0;
 RiscPerf::configure(now,owner,true);
}
}
bool esp_psram_is_initialized(){return model::psramReady;}
BaseType_t xPortGetCoreID(){return model::core;}
TaskHandle_t xTaskGetCurrentTaskHandle(){return model::task;}
void vTaskDelay(TickType_t ticks){model::correctOwner();model::delays.push_back(ticks);model::clockUs+=ticks*1000;}
esp_err_t spi_bus_initialize(spi_host_device_t host,const spi_bus_config_t* b,spi_dma_chan_t dma){
 using namespace model;correctOwner();assert(!bus&&host==SPI2_HOST&&dma==SPI_DMA_CH_AUTO);
 assert(b->sclk_io_num==11&&b->mosi_io_num==4&&b->miso_io_num==5&&b->quadwp_io_num==6&&b->quadhd_io_num==7);
 assert(b->data4_io_num==-1&&b->data5_io_num==-1&&b->data6_io_num==-1&&b->data7_io_num==-1);
 assert(b->max_transfer_sz==TestPairBytes&&(b->flags&SPICOMMON_BUSFLAG_QUAD)==SPICOMMON_BUSFLAG_QUAD);
 assert(b->isr_cpu_id==ESP_INTR_CPU_AFFINITY_AUTO);
 if(failed(Init))return ESP_ERR_INVALID_STATE;bus=true;return ESP_OK;
}
esp_err_t spi_bus_add_device(spi_host_device_t host,const spi_device_interface_config_t* d,spi_device_handle_t* out){
 using namespace model;correctOwner();assert(bus&&!device&&host==SPI2_HOST);
 assert(d->command_bits==8&&d->address_bits==24&&!d->dummy_bits&&!d->mode&&d->clock_speed_hz==40000000);
 assert(d->clock_source==SPI_CLK_SRC_DEFAULT&&d->spics_io_num==-1&&d->flags==SPI_DEVICE_HALFDUPLEX&&d->queue_size==1);
 const bool bad=failed(Add);if(bad&&!partialAdd)return ESP_ERR_NO_MEM;
 device=true;*out=&handle;return bad?ESP_ERR_NO_MEM:ESP_OK;
}
esp_err_t spi_device_acquire_bus(spi_device_handle_t d,uint32_t wait){
 using namespace model;correctOwner();assert(device&&d==&handle&&!acquired&&wait==portMAX_DELAY);
 if(failed(Acquire))return ESP_FAIL;acquired=true;return ESP_OK;
}
esp_err_t spi_device_polling_start(spi_device_handle_t d,spi_transaction_t* t,uint32_t wait){
 using namespace model;correctOwner();assert(bus&&device&&acquired&&!flight&&d==&handle&&wait==portMAX_DELAY&&levels[12]==0);
 if(failed(Start))return ESP_ERR_NO_MEM;
 assert(!t->rxlength&&!t->rx_buffer&&t->length%8==0);
 if(t->cmd==2)assert((t->flags&(SPI_TRANS_MULTILINE_CMD|SPI_TRANS_MULTILINE_ADDR))==(SPI_TRANS_MULTILINE_CMD|SPI_TRANS_MULTILINE_ADDR)&&!(t->flags&SPI_TRANS_MODE_QIO));
 else assert(t->cmd==0x32&&t->addr==RISC_CO5300_PIXEL_WRITE_ADDRESS&&t->flags==SPI_TRANS_MODE_QIO&&t->length<=TestPairBytes*8&&t->length%(TestWidth*2*16)==0&&(t->tx_buffer==allocation
#if RISC_CO5300_QUEUED_PIXELS
 || t->tx_buffer==static_cast<uint8_t*>(allocation)+TestPairBytes
#endif
 ));
 Packet p{t->cmd,t->addr,t->flags,{}};
 const auto* bytes=static_cast<const uint8_t*>((t->flags&SPI_TRANS_USE_TXDATA)?static_cast<void*>(t->tx_data):t->tx_buffer);
 if(t->length)p.bytes.assign(bytes,bytes+t->length/8);
 packets.push_back(std::move(p));descriptor=t;flight=true;clockUs+=3;return ESP_OK;
}
esp_err_t spi_device_polling_end(spi_device_handle_t d,uint32_t wait){
 using namespace model;correctOwner();assert(flight&&device&&acquired&&d==&handle&&wait<=21);
 assert(descriptor==&state.transaction&&levels[12]==0);
 const auto& p=packets.back();assert(p.command==descriptor->cmd&&p.address==descriptor->addr&&p.bytes.size()==descriptor->length/8);
 const void* bytes=(descriptor->flags&SPI_TRANS_USE_TXDATA)?static_cast<void*>(descriptor->tx_data):descriptor->tx_buffer;
 if(!p.bytes.empty())assert(std::memcmp(p.bytes.data(),bytes,p.bytes.size())==0);
 if(failed(End)){
  if(dmaFailure){descriptor->flags|=SPI_TRANS_DMA_TX_FAIL;flight=false;packets.back().complete=true;return ESP_ERR_INVALID_STATE;}
  return endError;
 }
 flight=false;packets.back().complete=true;clockUs+=7;return ESP_OK;
}
#if RISC_CO5300_QUEUED_PIXELS
esp_err_t spi_device_queue_trans(spi_device_handle_t d,spi_transaction_t* t,TickType_t wait){assert(wait<=21);return spi_device_polling_start(d,t,portMAX_DELAY);}
esp_err_t spi_device_get_trans_result(spi_device_handle_t d,spi_transaction_t** out,TickType_t wait){
 const auto result=spi_device_polling_end(d,wait);
 if(result==ESP_OK || (result==ESP_ERR_INVALID_STATE&&model::dmaFailure))*out=model::wrongResult?nullptr:model::descriptor;
 return result;
}
#endif
void spi_device_release_bus(spi_device_handle_t d){using namespace model;correctOwner();assert(device&&acquired&&!flight&&d==&handle);acquired=false;}
esp_err_t spi_bus_remove_device(spi_device_handle_t d){using namespace model;correctOwner();assert(device&&!acquired&&!flight&&d==&handle);if(failed(Remove))return ESP_FAIL;device=false;return ESP_OK;}
esp_err_t spi_bus_free(spi_host_device_t h){using namespace model;correctOwner();assert(bus&&!device&&!flight&&h==SPI2_HOST);if(failed(Free))return ESP_FAIL;bus=false;return ESP_OK;}
esp_err_t gpio_set_level(gpio_num_t pin,uint32_t value){
 using namespace model;correctOwner();assert((pin==12||pin==8)&&value<=1);
 if(pin==12&&value)assert(!flight);
 if(failed(Level))return ESP_FAIL;
 if(pin==8)resetLevels.emplace_back(value,clockUs);
 levels[pin]=value;return ESP_OK;
}
esp_err_t gpio_config(const gpio_config_t* c){
 using namespace model;correctOwner();assert(c->mode==GPIO_MODE_OUTPUT&&!c->pull_up_en&&!c->pull_down_en&&c->intr_type==GPIO_INTR_DISABLE);
 assert(c->pin_bit_mask==(UINT64_C(1)<<12)||c->pin_bit_mask==(UINT64_C(1)<<8));
 return failed(Configure)?ESP_FAIL:ESP_OK;
}
esp_err_t gpio_reset_pin(gpio_num_t pin){using namespace model;correctOwner();assert(!bus&&!device&&!flight);resets.push_back(pin);return failed(Reset)?ESP_FAIL:ESP_OK;}
esp_err_t gpio_dump_io_configuration(FILE*,uint64_t){return ESP_OK;}
void* heap_caps_aligned_alloc(size_t alignment,size_t bytes,uint32_t caps){
 using namespace model;correctOwner();assert(!allocation&&alignment==16&&(bytes==TestPairBytes
#if RISC_CO5300_QUEUED_PIXELS
 || (bytes>=TestWidth*4&&bytes<=2*TestPairBytes&&bytes%(TestWidth*4)==0)
#endif
 )&&caps==(MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA|MALLOC_CAP_8BIT));
 const bool allocationFault=failed(Allocate);if(allocationFailures){--allocationFailures;return nullptr;}if(allocationFault||bytes>maxAllocation)return nullptr;return posix_memalign(&allocation,alignment,bytes)?nullptr:allocation;
}
#if RISC_CO5300_REUSE_UNCHANGED
void* heap_caps_malloc(size_t bytes,uint32_t caps){using namespace model;correctOwner();assert(!shadowAllocation&&bytes==TestWidth*TestHeight*2&&caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));++shadowAllocations;if(shadowOom)return nullptr;return shadowAllocation=std::malloc(bytes);}
#endif
void heap_caps_free(void* p){using namespace model;correctOwner();assert(!flight&&!bus&&!device);
#if RISC_CO5300_REUSE_UNCHANGED
 if(p==shadowAllocation){std::free(p);shadowAllocation=nullptr;return;}
#endif
 assert(p==allocation);std::free(p);allocation=nullptr;}
static risc_hw_spi_display_v2 config(){
 risc_hw_spi_display_v2 c{};auto& b=c.base;b.struct_size=sizeof(c);b.width=TestWidth;b.height=TestHeight;b.offset_x=TestOffsetX;b.offset_y=TestOffsetY;
 b.reset=8;b.cs=12;b.dc=b.backlight=b.busy=-1;b.bus.sclk=11;b.bus.mosi=4;b.bus.miso=5;b.bus.frequency_hz=40000000;
 c.data2=6;c.data3=7;return c;
}
static risc_hardware_device_v1 hardware(const risc_hw_spi_display_v2& c){
 risc_hardware_device_v1 h{};h.api_version=1;h.struct_size=sizeof(h);h.compatible="chipone,co5300";
 h.config_type="display.spi";h.config_version=2;h.config_size=sizeof(c);h.config=&c;return h;
}
static auto* api=RiscCpu::selectedDisplayBackend();
static void open(){const auto c=config();const auto h=hardware(c);assert(api->accepts(h,2)&&api->open(h,2));}
static void closed(){assert(api->close());assert(api->close());assert(!state.owned&&!model::allocation&&!model::bus&&!model::device&&!model::flight);}
static void checkPixels(size_t firstPacket,bool zero,unsigned firstRow=0,unsigned endRow=TestHeight,unsigned stripeRows=RISC_CO5300_STRIPE_ROWS){
 using namespace model;unsigned expectedY=firstRow;
 for(size_t i=firstPacket;i<packets.size();++i){const auto& p=packets[i];if(p.command!=0x32)continue;
  const unsigned rows=std::min(stripeRows,endRow-expectedY);
  const unsigned pixels=rows*TestWidth;
  assert(i>=TestWindowPackets-1&&p.complete&&p.bytes.size()==pixels*2);
  const auto& x=packets[i-TestWindowPackets+1];const auto& y=packets[i-TestWindowPackets+2];
  assert(x.address==0x2a00&&x.bytes==addressBytes(TestOffsetX,TestOffsetX+TestWidth-1));
  const unsigned wireY=RISC_CO5300_PIXEL_ROTATION==180?TestHeight-rows-expectedY:expectedY;
  assert(y.address==0x2b00&&y.bytes==addressBytes(TestOffsetY+wireY,TestOffsetY+wireY+rows-1));
  if(RISC_CO5300_RAMWR_BEFORE_PIXELS){const auto& ram=packets[i-1];assert(ram.address==0x2c00&&ram.bytes.empty());}
  for(unsigned k=0;k<pixels;++k){const uint16_t value=zero?0:uint16_t(expectedY*TestWidth+(RISC_CO5300_PIXEL_ROTATION==180?pixels-1-k:k));assert(p.bytes[2*k]==value>>8&&p.bytes[2*k+1]==(value&255));}
  expectedY+=rows;
 }
 assert(expectedY==endRow);
}
static void frame(unsigned end=TestHeight){
 uint16_t line[TestWidth];for(unsigned y=0;y<end;++y){for(unsigned x=0;x<TestWidth;++x)line[x]=uint16_t(y*TestWidth+x);assert(api->row(y,line,TestWidth));std::fill(std::begin(line),std::end(line),0xffff);}
}
#ifdef RISC_VENDOR_DISPLAY_PROBE
static void checkProbe(){
 using namespace model;
 assert(lit()==1&&registerCount(0x23)==1&&registerCount(0x13)==1&&pixelCount()==TestWindows*5);
 const uint16_t colors[]={0,0xf800,0x07e0,0x001f,0};unsigned pixels=0;
 for(const auto& p:packets){
  if(p.command==2&&p.address==0x5100&&!p.bytes.empty()&&p.bytes[0])assert(pixels==TestWindows&&p.bytes[0]==80);
  if(p.command!=0x32)continue;
  assert(p.complete&&p.bytes.size()==TestPairBytes);const auto color=colors[pixels/TestWindows];
  for(unsigned k=0;k<TestPairPixels;++k)assert(p.bytes[2*k]==color>>8&&p.bytes[2*k+1]==(color&255));
  ++pixels;
 }
 assert(packets.back().command==0x32&&state.requestedBrightness==0&&!state.frameComplete);
 auto expected=bootDelays();expected.insert(expected.end(),{801,1201,1201,1201});assert(delays==expected);
}
#endif
#if defined(RISC_CO5300_TEST_REUSE)
#include "reuse_cases.inc"
#elif !defined(RISC_CO5300_TEST_STRIPES)
int main(int argc,char** argv){
 using namespace model;
#ifdef RISC_VENDOR_DISPLAY_PROBE
 if(argc==2){
  clear();fault(End,std::strtoul(argv[1],nullptr,10));const auto c=config();
  assert(!api->open(hardware(c),2)&&flight&&!state.frameComplete);auto* retained=allocation;
  fault(End);assert(!api->close()&&flight&&allocation==retained&&resets.empty());closed();
  // A failed explicit probe is retried after cleanup, not marked completed.
  clear();open();checkProbe();closed();
  std::puts("Explicit diagnostic timeout retains DMA custody and retries full probe: PASS");return 0;
 }
#endif
 assert(argc==1);(void)argv;
 clear();const auto valid=config();auto h=hardware(valid);uint16_t line[TestWidth]{};
 assert(!api->row(0,line,TestWidth)&&!api->brightness(80));
 assert(!api->accepts(h,3));h.config=nullptr;assert(!api->accepts(h,2));h=hardware(valid);h.compatible=nullptr;assert(!api->accepts(h,2));
 for(unsigned which=0;which<10;++which){auto c=config();
  switch(which){case 0:c.base.width=TestWidth+2;break;case 1:c.base.height=TestHeight-2;break;case 2:c.base.offset_x=TestOffsetX+2;break;case 3:c.base.rotation=1;break;case 4:c.base.bus.frequency_hz=80000000;break;case 5:c.base.bus.mode=1;break;case 6:c.data3=c.data2;break;case 7:c.base.cs=-1;break;case 8:c.base.reset_active_high=true;break;case 9:c.base.offset_y=TestOffsetY+2;break;}
  auto bad=hardware(c);assert(!api->accepts(bad,2)&&!api->open(bad,2));
 }
 assert(std::all_of(calls.begin(),calls.end(),[](unsigned n){return n==0;}));
 psramReady=false;assert(!api->open(hardware(valid),2)&&!state.owned);
 assert(std::all_of(calls.begin(),calls.end(),[](unsigned n){return n==0;}));
 psramReady=true;
 open();
 // Check reset edges as well as waits, so a shortened recovery cannot move
 // ahead of reset release while preserving the same list of sleep calls.
 assert(resetLevels.size()==3&&resetLevels[0]==std::make_pair(uint32_t(1),uint64_t(0)));
 assert(resetLevels[1]==std::make_pair(uint32_t(0),uint64_t(pdMS_TO_TICKS(10)+1)*1000));
 assert(resetLevels[2]==std::make_pair(uint32_t(1),uint64_t(pdMS_TO_TICKS(10)+1+pdMS_TO_TICKS(RISC_CO5300_RESET_LOW_MS)+1)*1000));
 // Verify every initial register byte independently, including brightness zero
 // before DISPON and the selected profile's exact command count and order.
 for(size_t i=0;i<ExpectedInit.size();++i){
  const auto& expected=ExpectedInit[i];const auto& actual=packets[i];
  assert(actual.complete&&actual.command==2&&actual.address==(uint32_t(expected.reg)<<8)&&actual.bytes==expected.bytes);
 }
 assert(packets[ExpectedInit.size()].address==0x2a00);
 if(!RISC_CO5300_RAMWR_BEFORE_PIXELS)assert(registerCount(0x2c)==0);
#ifdef RISC_VENDOR_DISPLAY_PROBE
 checkProbe();
#else
 assert(lit()==0&&pixelCount()==TestWindows);checkPixels(0,true);
 assert(registerCount(0x23)==0&&registerCount(0x13)==0);
 assert(delays==bootDelays());
#endif
 // A brightness request cannot illuminate boot clearing or an incomplete frame.
 const auto before=packets.size();const auto beforeLit=lit();assert(api->brightness(80)&&packets.size()==before);
 assert(!api->row(1,line,TestWidth)&&!api->row(0,line,TestWidth-1)&&!api->row(TestHeight,line,TestWidth));
 frame(TestHeight-2);assert(lit()==beforeLit&&!state.frameComplete);
 for(unsigned x=0;x<TestWidth;++x)line[x]=uint16_t((TestHeight-2)*TestWidth+x);assert(api->row(TestHeight-2,line,TestWidth));
 std::fill(std::begin(line),std::end(line),0xffff);
 assert(!api->row(TestHeight-4,line,TestWidth)&&lit()==beforeLit);
 for(unsigned x=0;x<TestWidth;++x)line[x]=uint16_t((TestHeight-1)*TestWidth+x);assert(api->row(TestHeight-1,line,TestWidth));
 assert(lit()==beforeLit+1&&state.frameComplete);checkPixels(before,false);
 if(!RISC_CO5300_RAMWR_BEFORE_PIXELS)assert(registerCount(0x2c)==0);
 assert(packets.back().address==0x5100&&packets.back().bytes[0]==80&&packets[packets.size()-2].complete);
#if RISC_PERFORMANCE_TRACE
 assert(clockReads==TestHeight+1&&RiscPerf::data.counts[34]==1&&RiscPerf::data.values[34]==TestHeight&&RiscPerf::data.values[35]==TestWindows);
 assert(RiscPerf::data.durations_us[34]==RiscPerf::data.durations_us[35]+RiscPerf::data.durations_us[36]);
#else
 assert(clockReads==0);
#endif
 auto callsBefore=calls;core=1;assert(!api->brightness(0)&&!api->row(0,line,TestWidth)&&!api->close()&&calls==callsBefore);core=0;
 task=reinterpret_cast<TaskHandle_t>(2);assert(!api->close()&&calls==callsBefore);task=reinterpret_cast<TaskHandle_t>(1);
 assert(api->brightness(0)&&packets.back().bytes[0]==0);closed();
 assert(registerCount(0x28)==1&&registerCount(0x10)==1&&resets.size()==7);
 // Application ordering: blank before rendering, complete at zero, then
 // restore the selected level. Repeat a retained-provider child return and a
 // fresh provider reopen; no implicit/default nonzero command may win.
 clear();open();assert(api->brightness(0));frame();
 assert(state.frameComplete&&state.requestedBrightness==0&&lit()==0);
 assert(api->brightness(102)&&packets.back().address==0x5100&&packets.back().bytes[0]==102);
 assert(api->brightness(0));frame();assert(packets.back().command==0x32);
 assert(api->brightness(163)&&packets.back().bytes[0]==163);closed();
 // An early requested level is superseded by explicit blanking until the app
 // requests visibility again. An already complete frame permits that restore.
 clear();open();assert(api->brightness(102)&&api->brightness(0));frame();
 assert(state.frameComplete&&state.requestedBrightness==0&&lit()==0);
 assert(api->brightness(26)&&packets.back().bytes[0]==26);closed();
 std::puts("Native brightness lifecycle: first frame at zero, selected restore, retained return and fresh reopen PASS");
 // No full-frame gate from an out-of-order set of pairs.
 clear();open();assert(api->brightness(90));assert(api->row(2,line,TestWidth)&&api->row(3,line,TestWidth));
 for(unsigned y=4;y<TestHeight;++y)assert(api->row(y,line,TestWidth));assert(!state.frameComplete&&lit()==0);closed();
 // Every setup boundary can fail without losing exact cleanup custody.
 for(auto op:{Init,Add,Acquire,Allocate,Configure,Level}){
  clear();fault(op);const auto c=config();const auto hw=hardware(c);assert(!api->open(hw,2)&&state.owned);closed();
  if(op==Init)assert(calls[Free]==0&&resets==std::vector<int>({12}));
 }
 clear();partialAdd=true;fault(Add);{const auto c=config();assert(!api->open(hardware(c),2)&&device);closed();}
 // Register, reset and early/late boot clearing failures stay dark.
 for(unsigned n:{2u,3u,4u}){clear();fault(Level,n);const auto c=config();assert(!api->open(hardware(c),2));closed();}
 for(unsigned n:{1u,7u,12u,13u,16u,TestBootPackets}){
  clear();fault(Start,n);const auto c=config();assert(!api->open(hardware(c),2)&&lit()==0);closed();
 }
 for(unsigned n:{1u,7u,16u,TestBootPackets}){
  clear();fault(End,n);const auto c=config();assert(!api->open(hardware(c),2)&&flight&&lit()==0);
  auto* kept=allocation;const auto descriptorCopy=state.transaction;fault(End);assert(!api->close()&&flight&&allocation==kept&&resets.empty());
  assert(std::memcmp(&descriptorCopy,&state.transaction,sizeof(descriptorCopy))==0);closed();
 }
 // A timed-out final pair never opens the visibility gate, even after close
 // later proves that transfer completed. Caller pixels and descriptor stay held.
 clear();open();assert(api->brightness(80));frame(TestHeight-1);fault(End,TestWindowPackets);assert(!api->row(TestHeight-1,line,TestWidth)&&flight&&!state.frameComplete&&lit()==0);
 const std::vector<uint8_t> retainedDma(state.dma,state.dma+TestPairBytes);
 assert(!api->brightness(255)&&!api->row(TestHeight-1,line,TestWidth));
 for(unsigned x=0;x<TestWidth;++x)line[x]^=0xffff;
 assert(!api->row(0,line,TestWidth)&&!api->row(1,line,TestWidth));
 assert(std::equal(retainedDma.begin(),retainedDma.end(),state.dma));
 closed();assert(lit()==0);
 // IDF6's completed-DMA-error status is distinct from still-in-flight timeout.
 clear();open();assert(api->row(0,line,TestWidth));dmaFailure=true;fault(End,TestWindowPackets);assert(!api->row(1,line,TestWidth)&&!flight&&state.failed);closed();
 // CS deassertion failure retains the completed transaction until safely high.
 clear();open();fault(Level,2);assert(!api->brightness(0)&&!flight&&state.selected);closed();
 // Sleep command stages resume after completion without duplicate commands.
 for(unsigned n:{1u,2u}){clear();open();fault(End,n);assert(!api->close()&&flight&&device&&bus&&allocation);closed();assert(registerCount(0x28)==1&&registerCount(0x10)==1);}
 for(auto op:{Remove,Free}){clear();open();fault(op);assert(!api->close()&&state.owned&&allocation&&resets.empty());closed();assert(registerCount(0x28)==1&&registerCount(0x10)==1);}
 for(unsigned n=1;n<=7;++n){clear();open();fault(Reset,n);assert(!api->close()&&allocation&&!bus&&!device&&resets.size()==n);const auto held=resets.back();closed();assert(resets[n]==held&&resets.size()==8);}
 // Partial frames produce exactly one abort across failed cleanup retries.
 clear();open();assert(api->row(0,line,TestWidth));fault(Remove);assert(!api->close());
#if RISC_PERFORMANCE_TRACE
 assert(RiscPerf::data.counts[39]==1&&RiscPerf::data.values[39]==1);
#endif
 closed();
#if RISC_PERFORMANCE_TRACE
 assert(RiscPerf::data.counts[39]==1);
#endif
 // Partial row-pair updates preserve logical-to-physical mapping on reopen.
 for(unsigned cycle=0;cycle<3;++cycle){
  clear();open();
  for(unsigned first:{0u,TestHeight/2u&~1u,TestHeight-2u}){
   const auto mark=packets.size();
   for(unsigned y=first;y<first+2;++y){
    uint16_t padded[TestWidth+4];padded[0]=padded[TestWidth+3]=0xabcd;
    for(unsigned x=0;x<TestWidth;++x)padded[x+2]=uint16_t(y*TestWidth+x);
    assert(api->row(y,padded+2,TestWidth));
    assert(padded[0]==0xabcd&&padded[TestWidth+3]==0xabcd);
   }
   checkPixels(mark,false,first,first+2);
  }
  closed();
 }
 // An unclassified poll error is irreversible uncertainty: no re-poll, pad
 // reset, resource release or new open. The following reset is test-process only.
 clear();open();assert(api->row(0,line,TestWidth));endError=ESP_FAIL;fault(End);assert(!api->row(1,line,TestWidth)&&state.uncertain);
 const std::vector<uint8_t> uncertainDma(state.dma,state.dma+TestPairBytes);
 callsBefore=calls;assert(!api->row(0,line,TestWidth)&&!api->row(1,line,TestWidth));
 assert(std::equal(uncertainDma.begin(),uncertainDma.end(),state.dma));
 assert(!api->close()&&!api->close()&&calls==callsBefore&&resets.empty());
 {const auto c=config();assert(!api->open(hardware(c),2));}
 std::free(allocation);allocation=nullptr;state=PanelState{};bus=device=acquired=flight=false;
 std::printf("Native IDF6 CO5300: real SDK/ABI, %ux%u offset %u/%u, 40MHz framing, %u paired windows, dark frame gate, setup/transfer/cleanup faults, retained custody and trace=%d PASS\n",TestWidth,TestHeight,TestOffsetX,TestOffsetY,TestWindows,RISC_PERFORMANCE_TRACE);
}

#else
#include "stripe_cases.inc"
#endif

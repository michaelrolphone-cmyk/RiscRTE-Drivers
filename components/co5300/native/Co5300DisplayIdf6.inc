// Adapted and relocated native implementation; vendor-derived wire behavior.
// See ../THIRD_PARTY_NOTICES.md (THIRD_PARTY_NOTICES.md in a staged bundle).
// Native ESP-IDF6 CO5300 bridge. Register sequence and memory-write policy are
// selected by the shared build profile; defaults preserve Waveshare vendor
// b099739ad0e33b34e5fbaae77f02bd84805d79a3. See co5300-native-provenance.json in this directory.
// No Arduino or app-facing hardware API is used.
#include "ports/esp32s3/CpuPort.h"
#include "ports/esp32s3/SleepDiagnostics.h"
#include "diagnostics/Performance.h"
#include "RiscCo5300Profile.h"
#include <driver/spi_master.h>
#include <driver/gpio.h>
#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <esp_psram.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstdio>
#include <cstring>

namespace {
constexpr uint16_t Width=RISC_CO5300_WIDTH,Height=RISC_CO5300_HEIGHT;
constexpr uint16_t OffsetX=RISC_CO5300_OFFSET_X,OffsetY=RISC_CO5300_OFFSET_Y;
constexpr unsigned StripeRows=RISC_CO5300_STRIPE_ROWS;
constexpr size_t StripePixels=size_t(Width)*StripeRows,StripeBytes=StripePixels*sizeof(uint16_t);
static_assert(StripeRows<=Height && uint64_t(Width)*StripeRows*sizeof(uint16_t)<=UINT32_MAX/8u,"CO5300 stripe must fit panel and SDK transfer size");
constexpr spi_host_device_t Host=SPI2_HOST;
struct InitCommand {uint8_t reg,count,data[4];uint16_t delay;};
#define RISC_CO5300_INIT_ENTRY(reg,count,a,b,c,d,delay) {reg,count,{a,b,c,d},delay},
constexpr InitCommand InitCommands[]={RISC_CO5300_INIT_COMMANDS(RISC_CO5300_INIT_ENTRY)};
#undef RISC_CO5300_INIT_ENTRY
constexpr bool validInitialization(){
 bool dark=false,displayOn=false;
 for(const auto& item:InitCommands){
  if(item.count>sizeof(item.data))return false;
  if(item.reg==0x51){if(item.count!=1 || item.data[0])return false;dark=true;}
  if(item.reg==0x29){if(item.count || !dark)return false;displayOn=true;}
 }
 return displayOn;
}
static_assert(validInitialization(),"CO5300 init must keep brightness zero before display-on and use at most four data bytes");
enum class Shutdown { DisplayOff,Sleep,Done };
enum class Action { None,DisplayOff,Sleep };
struct PanelState {
 bool owned=false,busOwned=false,acquired=false,inflight=false,selected=false;
 bool ready=false,closing=false,failed=false,uncertain=false,frameComplete=false;
#if RISC_CO5300_REUSE_COLUMNS
 bool columnsKnown=false;
#endif
 int core=-1;TaskHandle_t task=nullptr;
 int16_t pins[7]={-1,-1,-1,-1,-1,-1,-1};
 bool dirty[7]{};
 spi_device_handle_t device=nullptr;
 spi_transaction_t transaction{};
 uint8_t* dma=nullptr;
 unsigned stripeRows=StripeRows;size_t stripeBytes=StripeBytes;
#if RISC_CO5300_QUEUED_PIXELS
 uint8_t* dmaBase=nullptr;bool doubleDma=false,queued=false;
 uint8_t* queuedShadow=nullptr;size_t queuedBytes=0;
 uint64_t queuedBegan=0;bool queuedComplete=false;
#endif
#if RISC_CO5300_REUSE_UNCHANGED
 uint8_t* completedPixels=nullptr;
 bool shadowAttempted=false,shadowComplete=false;
#endif
 uint32_t pendingRow=UINT32_MAX,nextFrameRow=UINT32_MAX;
 uint32_t pendingRows=0;
 uint8_t requestedBrightness=0;
 Shutdown shutdown=Shutdown::DisplayOff;
 Action action=Action::None;
};
// The descriptor outlives a timed-out poll; the DMA buffer is never caller-owned.
static DRAM_ATTR PanelState state;
void markFailed(){
 state.failed=true;
#if RISC_CO5300_REUSE_COLUMNS
 state.columnsKnown=false;
#endif
}

void evidence(const char* format,unsigned value,unsigned frame=0,unsigned extra=0){
#if RISC_DIAGNOSTIC_ADAPTER
 // Use the Runtime's selected bounded owner
 // diagnostic adapter, never a second USB writer or a libc console fallback.
 char text[112];
 const int count=std::snprintf(text,sizeof(text),format,value,frame,extra);
 if(count>0 && size_t(count)<sizeof(text))RiscDiagnostics::line(text);
#else
 (void)format;(void)value;(void)frame;(void)extra;
#endif
}
// Pack accepted rows directly into the owned internal DMA stripe. No caller
// storage is submitted; failed/in-flight transactions remain fenced by row().
void packRow(unsigned row,unsigned rows,const uint16_t* pixels){
 for(size_t x=0;x<Width;++x){
  const size_t logical=size_t(row)*Width+x;
  const size_t wire=RISC_CO5300_PIXEL_ROTATION==180?size_t(rows)*Width-1-logical:logical;
  const uint16_t pixel=pixels[x];
  state.dma[wire*2]=uint8_t(pixel>>8);state.dma[wire*2+1]=uint8_t(pixel);
 }
}
#if RISC_PERFORMANCE_TRACE
// Owner-task-only bounded aggregates. No allocation, formatting or transport
// in row/SPI work. The existing `perf` replay drains at most64bytes per poll.
// Phases34..39 are device-build diagnostic aggregates, not hardware ABI.
static constexpr char riscVendorDisplayTimingMarker[]="RISCRTE_VENDOR_DISPLAY_TIMING_V1";
struct FrameTiming {
 uint64_t start=0,lastEnd=0,transfer=0,gap=0,maxTransfer=0,maxGap=0;
 uint32_t rows=0,windows=0,stripes=0;bool active=false;
 void accepted(uint32_t y){
  if(!y){
   // Referenced in live code so --gc-sections retains binary-bound build proof.
   asm volatile("" : : "r"(riscVendorDisplayTimingMarker));
   *this={};if(!RiscPerf::allowed())return;start=lastEnd=RiscPerf::now();active=true;
  }
  if(active)++rows;
 }
 uint64_t begin(){return active?RiscPerf::now():0;}
 void end(uint64_t began,bool complete,bool transferred=true){
  if(!active)return;
  const auto ended=RiscPerf::now();
  const auto elapsed=ended>=began?ended-began:0;
  const auto idle=began>=lastEnd?began-lastEnd:0;
  const auto gapElapsed=idle+(transferred?0:elapsed);
  if(transferred){transfer+=elapsed;++windows;if(elapsed>maxTransfer)maxTransfer=elapsed;}
  gap+=gapElapsed;++stripes;lastEnd=ended;
  if(gapElapsed>maxGap)maxGap=gapElapsed;
  if(complete){
   RiscPerf::aggregate(34,rows,ended>=start?ended-start:0);
   RiscPerf::aggregate(35,windows,transfer);
   RiscPerf::aggregate(36,stripes,gap);
   RiscPerf::aggregate(37,1,maxTransfer);
   RiscPerf::aggregate(38,1,maxGap);
   active=false;
  }
 }
 void abort(){if(active){const auto ended=RiscPerf::now();RiscPerf::aggregate(39,rows,ended>=start?ended-start:0);active=false;}}
} frameTiming;
#endif
bool accepts(const risc_hardware_device_v1& hw,uint8_t physical){
 if(physical!=2 || !hw.compatible || !hw.config_type ||
    std::strcmp(hw.compatible,"chipone,co5300") || std::strcmp(hw.config_type,"display.spi") ||
    hw.config_version!=2 || hw.config_size!=sizeof(risc_hw_spi_display_v2) || !hw.config)return false;
 const auto& c=*static_cast<const risc_hw_spi_display_v2*>(hw.config);const auto& b=c.base;
 if(b.width!=Width || b.height!=Height || b.offset_x!=OffsetX || b.offset_y!=OffsetY || b.rotation ||
    b.reset_active_high || b.dc!=-1 || b.backlight!=-1 || b.busy!=-1 || b.power_count ||
    b.bus.frequency_hz!=40000000 || b.bus.mode)return false;
 const int16_t pins[]={b.bus.sclk,b.bus.mosi,b.bus.miso,c.data2,c.data3,b.cs,b.reset};
 for(unsigned i=0;i<7;++i){
  if(!GPIO_IS_VALID_OUTPUT_GPIO(pins[i]))return false;
  for(unsigned j=0;j<i;++j)if(pins[i]==pins[j])return false;
 }
 return true;
}
void pads(const char* stage){
#ifdef RISC_VENDOR_DISPLAY_PROBE
 std::printf("PANEL_PAD stage=%s native-idf=6\n",stage);
 uint64_t mask=0;for(auto pin:state.pins)if(pin>=0)mask|=UINT64_C(1)<<pin;
 (void)gpio_dump_io_configuration(stdout,mask);
#else
 (void)stage;
#endif
}
bool owner(){return state.owned && state.core==xPortGetCoreID() && state.task==xTaskGetCurrentTaskHandle();}
void delayMs(uint32_t ms){vTaskDelay(pdMS_TO_TICKS(ms)+1);}
bool output(unsigned index,uint32_t value){
 state.dirty[index]=true;
 const auto pin=static_cast<gpio_num_t>(state.pins[index]);
 // Stage the latch before the pin becomes an output.
 if(gpio_set_level(pin,value)!=ESP_OK)return false;
 gpio_config_t config{};config.pin_bit_mask=UINT64_C(1)<<pin;config.mode=GPIO_MODE_OUTPUT;
 config.pull_up_en=GPIO_PULLUP_DISABLE;config.pull_down_en=GPIO_PULLDOWN_DISABLE;
 config.intr_type=GPIO_INTR_DISABLE;
 return gpio_config(&config)==ESP_OK;
}
bool unselect(){
 if(!state.selected)return true;
 if(gpio_set_level(static_cast<gpio_num_t>(state.pins[5]),1)!=ESP_OK)return false;
 state.selected=false;return true;
}
bool finish(){
 if(state.uncertain)return false;
 bool completedError=false;
 if(state.inflight){
  // Bound each poll. IDF only permits portMAX_DELAY for polling_start and bus
  // acquisition; this private, already-acquired bus has no queued competitors.
#if RISC_CO5300_QUEUED_PIXELS
  spi_transaction_t* completed=nullptr;
  const auto result=state.queued?spi_device_get_trans_result(state.device,&completed,pdMS_TO_TICKS(20)+1):spi_device_polling_end(state.device,pdMS_TO_TICKS(20)+1);
  if(state.queued && result!=ESP_ERR_TIMEOUT && (result==ESP_OK || result==ESP_ERR_INVALID_STATE) && completed!=&state.transaction){markFailed();state.uncertain=true;return false;}
#else
  const auto result=spi_device_polling_end(state.device,pdMS_TO_TICKS(20)+1);
#endif
  if(result!=ESP_OK){
   markFailed();
   if(result==ESP_ERR_TIMEOUT)return false;
   // The pinned SDK returns INVALID_STATE after finalizing a DMA failure.
   // Other errors do not prove custody returned: retain without another poll.
   const uint32_t dmaError=SPI_TRANS_DMA_RX_FAIL|SPI_TRANS_DMA_TX_FAIL;
   if(result!=ESP_ERR_INVALID_STATE || !(state.transaction.flags&dmaError)){
    state.uncertain=true;return false;
   }
   completedError=true;state.action=Action::None;
  }
  state.inflight=false;
 }
 if(!unselect()){markFailed();return false;}
#if RISC_CO5300_QUEUED_PIXELS
 if(state.queued){
  if(!completedError){
   if(state.queuedShadow)std::memcpy(state.queuedShadow,state.transaction.tx_buffer,state.queuedBytes);
#if RISC_PERFORMANCE_TRACE
   frameTiming.end(state.queuedBegan,state.queuedComplete,true);
#endif
  }
  state.queued=false;state.queuedShadow=nullptr;state.queuedBytes=0;
 }
#endif
 if(state.action!=Action::None){
  const auto action=state.action;state.action=Action::None;
  state.shutdown=action==Action::DisplayOff?Shutdown::Sleep:Shutdown::Done;
  delayMs(120);
 }
 return !completedError;
}
bool transfer(const spi_transaction_t& transaction,Action action=Action::None){
 if(!state.device || !state.acquired || state.inflight || state.selected || state.uncertain)return false;
 state.transaction=transaction;
 state.selected=true;
 if(gpio_set_level(static_cast<gpio_num_t>(state.pins[5]),0)!=ESP_OK){markFailed();return false;}
 const auto result=spi_device_polling_start(state.device,&state.transaction,portMAX_DELAY);
 if(result!=ESP_OK){markFailed();(void)unselect();return false;}
 state.inflight=true;state.action=action;
 return finish();
}
bool command(uint8_t reg,const uint8_t* data=nullptr,size_t bytes=0,Action action=Action::None){
#if RISC_CO5300_REUSE_COLUMNS
 // Only these commands are known not to change the stored column extent.
 // Any other register operation, including resets/init/MADCTL, drops authority
 // before submission; a failed or incomplete write cannot bless the cache.
 if(reg!=0x2b && reg!=0x2c && reg!=0x51)state.columnsKnown=false;
#endif
 if(bytes>4)return false;
 spi_transaction_t transaction{};
 transaction.flags=SPI_TRANS_MULTILINE_CMD|SPI_TRANS_MULTILINE_ADDR;
 transaction.cmd=0x02;transaction.addr=uint32_t(reg)<<8;transaction.length=bytes*8;
 if(bytes){transaction.flags|=SPI_TRANS_USE_TXDATA;std::memcpy(transaction.tx_data,data,bytes);}
 return transfer(transaction,action);
}
bool byte(uint8_t reg,uint8_t value){return command(reg,&value,1);}
bool initialize(){
 for(const auto& item:InitCommands){
  if(!command(item.reg,item.data,item.count))return false;
  if(item.delay)delayMs(item.delay);
 }
 return true;
}
bool window(uint8_t reg,uint16_t first,uint16_t last){
 const uint8_t data[]={uint8_t(first>>8),uint8_t(first),uint8_t(last>>8),uint8_t(last)};
 return command(reg,data,sizeof(data));
}
#if RISC_CO5300_QUEUED_PIXELS
bool queuePixels(const spi_transaction_t& transaction){
 if(!state.device || !state.acquired || state.inflight || state.selected || state.uncertain)return false;
 state.transaction=transaction;state.selected=true;
 if(gpio_set_level(static_cast<gpio_num_t>(state.pins[5]),0)!=ESP_OK){markFailed();return false;}
 const auto result=spi_device_queue_trans(state.device,&state.transaction,pdMS_TO_TICKS(20)+1);
 if(result!=ESP_OK){markFailed();(void)unselect();return false;}
 state.inflight=true;state.queued=true;return true;
}
#endif
bool columnWindow(){
#if RISC_CO5300_REUSE_COLUMNS
 if(state.columnsKnown && !state.failed && !state.uncertain)return true;
#endif
 if(!window(0x2a,OffsetX,OffsetX+Width-1))return false;
#if RISC_CO5300_REUSE_COLUMNS
 // window() has reaped completion and successfully released manual CS.
 state.columnsKnown=!state.failed&&!state.uncertain;
#endif
 return true;
}
bool stripe(uint32_t y,unsigned rows,bool queued=false){
 // Rotate the selected logical raster inside the same proven physical window.
 // Reversing rows and columns preserves even stripe alignment and does not
 // change MADCTL, color order, controller gaps, or raw touch coordinates.
 const uint32_t wireY=RISC_CO5300_PIXEL_ROTATION==180?Height-rows-y:y;
 if(!columnWindow() || !window(0x2b,OffsetY+wireY,OffsetY+wireY+rows-1))return false;
 if(RISC_CO5300_RAMWR_BEFORE_PIXELS && !command(0x2c))return false;
 spi_transaction_t transaction{};transaction.flags=SPI_TRANS_MODE_QIO;
 transaction.cmd=0x32;transaction.addr=RISC_CO5300_PIXEL_WRITE_ADDRESS;
 transaction.length=size_t(rows)*Width*sizeof(uint16_t)*8;transaction.tx_buffer=state.dma;
#if RISC_CO5300_QUEUED_PIXELS
 if(queued)return queuePixels(transaction);
#else
 (void)queued;
#endif
 return transfer(transaction);
}
bool fill(uint16_t color){
#if RISC_CO5300_REUSE_UNCHANGED
 state.shadowComplete=false;state.nextFrameRow=UINT32_MAX;
#endif
#if RISC_CO5300_QUEUED_PIXELS
 if(!finish())return false;
#endif
 for(size_t i=0;i<size_t(Width)*state.stripeRows;++i){state.dma[i*2]=uint8_t(color>>8);state.dma[i*2+1]=uint8_t(color);}
 for(unsigned y=0;y<Height;y+=state.stripeRows){const unsigned rows=Height-y<state.stripeRows?Height-y:state.stripeRows;if(!stripe(y,rows))return false;}
 return true;
}
bool closePanel(){
 if(!state.owned)return true;
 if(!owner())return false;
 state.closing=true;
#if RISC_CO5300_REUSE_COLUMNS
 state.columnsKnown=false;
#endif
#if RISC_PERFORMANCE_TRACE
 frameTiming.abort();
#endif
 // Never free/reuse a descriptor or buffer until SDK completion is proven.
 if(!finish())return false;
 if(state.ready){
  if(state.shutdown==Shutdown::DisplayOff && !command(0x28,nullptr,0,Action::DisplayOff))return false;
  if(state.shutdown==Shutdown::Sleep && !command(0x10,nullptr,0,Action::Sleep))return false;
 }
 if(state.device){
  if(state.acquired){spi_device_release_bus(state.device);state.acquired=false;}
  if(spi_bus_remove_device(state.device)!=ESP_OK)return false;
  state.device=nullptr;
 }
 if(state.busOwned){if(spi_bus_free(Host)!=ESP_OK)return false;state.busOwned=false;}
 // SPI bus release does not prove a full pad reset. Check every owned pad;
 // failed bus initialization never claims or resets another user's bus pads.
 for(unsigned i=0;i<7;++i)if(state.dirty[i]){
  if(gpio_reset_pin(static_cast<gpio_num_t>(state.pins[i]))!=ESP_OK)return false;
  state.dirty[i]=false;state.pins[i]=-1;
 }
 #if RISC_CO5300_REUSE_UNCHANGED
 if(state.completedPixels)heap_caps_free(state.completedPixels);
 #endif
#if RISC_CO5300_QUEUED_PIXELS
 if(state.dmaBase)heap_caps_free(state.dmaBase);
#else
 if(state.dma)heap_caps_free(state.dma);
#endif
 state=PanelState{};return true;
}
bool openPanel(const risc_hardware_device_v1& hw,uint8_t physical){
 if(state.owned || !accepts(hw,physical) || !esp_psram_is_initialized())return false;
 const auto& c=*static_cast<const risc_hw_spi_display_v2*>(hw.config);const auto& b=c.base;
 state.owned=true;state.core=xPortGetCoreID();state.task=xTaskGetCurrentTaskHandle();
 const int16_t pins[]={b.bus.sclk,b.bus.mosi,b.bus.miso,c.data2,c.data3,b.cs,b.reset};
 std::memcpy(state.pins,pins,sizeof(pins));
 pads("before-begin");
 if(!output(5,1))return false;
 spi_bus_config_t bus{};bus.sclk_io_num=b.bus.sclk;bus.mosi_io_num=b.bus.mosi;bus.miso_io_num=b.bus.miso;
 bus.quadwp_io_num=c.data2;bus.quadhd_io_num=c.data3;
 bus.data4_io_num=bus.data5_io_num=bus.data6_io_num=bus.data7_io_num=-1;
 bus.max_transfer_sz=StripeBytes;bus.flags=SPICOMMON_BUSFLAG_MASTER|SPICOMMON_BUSFLAG_GPIO_PINS|SPICOMMON_BUSFLAG_QUAD;
 bus.isr_cpu_id=ESP_INTR_CPU_AFFINITY_AUTO;
 if(spi_bus_initialize(Host,&bus,SPI_DMA_CH_AUTO)!=ESP_OK)return false;
 state.busOwned=true;for(unsigned i=0;i<5;++i)state.dirty[i]=true;
 spi_device_interface_config_t device{};device.command_bits=8;device.address_bits=24;
 device.mode=0;device.clock_source=SPI_CLK_SRC_DEFAULT;device.clock_speed_hz=40000000;
 device.spics_io_num=-1;device.flags=SPI_DEVICE_HALFDUPLEX;device.queue_size=1;
 if(spi_bus_add_device(Host,&device,&state.device)!=ESP_OK)return false;
 if(spi_device_acquire_bus(state.device,portMAX_DELAY)!=ESP_OK)return false;
 state.acquired=true;
#if RISC_CO5300_QUEUED_PIXELS
 state.dmaBase=static_cast<uint8_t*>(heap_caps_aligned_alloc(16,2*StripeBytes,MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA|MALLOC_CAP_8BIT));
 state.doubleDma=state.dmaBase!=nullptr;
 // Fixed for this ownership epoch; preserve an ordinary smaller-window path
 // when the configured large staging allocation cannot fit internal DMA RAM.
 while(!state.dmaBase){
  state.dmaBase=static_cast<uint8_t*>(heap_caps_aligned_alloc(16,state.stripeBytes,MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA|MALLOC_CAP_8BIT));
  if(state.dmaBase || state.stripeRows==2)break;
  state.stripeRows=(state.stripeRows/2)&~1u;if(state.stripeRows<2)state.stripeRows=2;
  state.stripeBytes=size_t(Width)*state.stripeRows*2;
 }
 state.dma=state.dmaBase;
#else
 state.dma=static_cast<uint8_t*>(heap_caps_aligned_alloc(16,StripeBytes,MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA|MALLOC_CAP_8BIT));
#endif
 if(!state.dma || !output(6,1))return false;
 delayMs(10);
 if(gpio_set_level(static_cast<gpio_num_t>(b.reset),0)!=ESP_OK)return false;
 delayMs(RISC_CO5300_RESET_LOW_MS);
 if(gpio_set_level(static_cast<gpio_num_t>(b.reset),1)!=ESP_OK)return false;
 delayMs(RISC_CO5300_RESET_RECOVERY_MS);
 // The selected vendor sequence keeps normal brightness zero instead of its
 // transient nonzero value. HBM configuration and checked custody are unchanged.
 if(!initialize())return false;
 state.ready=true;
 if(!fill(0))return false;
#if RISC_CO5300_QUEUED_PIXELS
 evidence("PANEL_TRANSPORT stripe_rows=%u queued=%u column_reuse=%u",state.stripeRows,unsigned(state.doubleDma),unsigned(RISC_CO5300_REUSE_COLUMNS));
#elif RISC_CO5300_REUSE_COLUMNS
 evidence("PANEL_TRANSPORT stripe_rows=%u queued=0 column_reuse=%u",state.stripeRows,1);
#endif
 pads("after-begin");
#ifdef RISC_VENDOR_DISPLAY_PROBE
 static bool probed=false;
 if(!probed){
  // Explicit diagnostic exception: checked all-pixels and RGB probe.
  if(!byte(0x51,80) || !command(0x23))return false;
  delayMs(800);
  if(!command(0x13) || !fill(0xf800))return false;
  delayMs(1200);
  if(!fill(0x07e0))return false;
  delayMs(1200);
  if(!fill(0x001f))return false;
  delayMs(1200);
  if(!byte(0x51,0) || !fill(0))return false;
  probed=true;
 }
#endif
 return true;
}
bool row(uint32_t y,const uint16_t* pixels,size_t count){
 if(!owner() || !state.ready || state.closing || state.failed || !pixels || count!=Width || y>=Height)return false;
 if(state.pendingRow==UINT32_MAX){
  if(y&1u)return false;
#if RISC_CO5300_REUSE_UNCHANGED
  // Optional PSRAM only: never consume scarce internal DMA storage for cache.
  if(!y&&!state.shadowAttempted){
   state.shadowAttempted=true;
   state.completedPixels=static_cast<uint8_t*>(heap_caps_malloc(size_t(Width)*Height*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  }
#endif
  if(!y)state.nextFrameRow=0;
  state.pendingRow=y;state.pendingRows=0;
 }else if(y!=state.pendingRow+state.pendingRows)return false;
 const unsigned rows=Height-state.pendingRow<state.stripeRows?Height-state.pendingRow:state.stripeRows;
#if RISC_PERFORMANCE_TRACE
 frameTiming.accepted(y);
#endif
 packRow(state.pendingRows,rows,pixels);++state.pendingRows;
 if(state.pendingRows==rows){
#if RISC_CO5300_QUEUED_PIXELS
  // Prior DMA owns the other buffer; reap before any new panel window.
  if(!finish())return false;
#endif
#if RISC_PERFORMANCE_TRACE
  const auto began=frameTiming.begin();
#endif
  bool transferred=true;
#if RISC_CO5300_REUSE_UNCHANGED
  const auto wireY=RISC_CO5300_PIXEL_ROTATION==180?Height-rows-state.pendingRow:state.pendingRow;
  const auto bytes=size_t(rows)*Width*2;
  uint8_t* previous=state.completedPixels?state.completedPixels+size_t(wireY)*Width*2:nullptr;
  transferred=!state.shadowComplete||!previous||std::memcmp(previous,state.dma,bytes)!=0;
#endif
#if RISC_CO5300_QUEUED_PIXELS
  const bool queued=transferred&&state.doubleDma;
#else
  const bool queued=false;
#endif
  if(transferred&&!stripe(state.pendingRow,rows,queued)){markFailed();return false;}
#if RISC_CO5300_QUEUED_PIXELS
  if(queued){
#if RISC_CO5300_REUSE_UNCHANGED
   state.queuedShadow=previous;
#endif
   state.queuedBytes=size_t(rows)*Width*2;state.queuedComplete=y+1==Height;
#if RISC_PERFORMANCE_TRACE
   state.queuedBegan=began;
#endif
   state.dma=state.dma==state.dmaBase?state.dmaBase+state.stripeBytes:state.dmaBase;
  }
#endif
#if RISC_CO5300_REUSE_UNCHANGED
  // Only proven completed writes become comparison authority. No cache write
  // follows timeout/uncertain completion; row() then fences all later calls.
  if(transferred&&!queued&&previous)std::memcpy(previous,state.dma,bytes);
#endif
#if RISC_CO5300_QUEUED_PIXELS
  if(y+1==Height&&!finish())return false;
#endif
  state.pendingRow=UINT32_MAX;state.pendingRows=0;
#if RISC_PERFORMANCE_TRACE
  if(!queued)frameTiming.end(began,y+1==Height,transferred);
#endif
 }
 state.nextFrameRow=state.nextFrameRow==y?y+1:UINT32_MAX;
#if RISC_CO5300_REUSE_UNCHANGED
 if(state.nextFrameRow==Height&&state.completedPixels)state.shadowComplete=true;
#endif
 if(!state.frameComplete && state.nextFrameRow==Height){
  state.frameComplete=true;
  if(state.requestedBrightness && !byte(0x51,state.requestedBrightness)){markFailed();return false;}
  // One owner-task record per native open. Completion proves SPI custody, not
  // visible pixels; preserve that distinction when diagnosing a dark launch.
  evidence("PANEL_FRAME completed=1 brightness=%u visibility=unverified",unsigned(state.requestedBrightness));
 }
 return true;
}
bool brightness(uint8_t value){
 if(!owner() || !state.ready || state.closing || state.failed)return false;
#if RISC_CO5300_QUEUED_PIXELS
 if(!finish())return false;
#endif
 state.requestedBrightness=value;
 // Boot black clearing cannot open this gate; only a complete checked frame.
 if(!state.frameComplete && value){
  evidence("PANEL_BRIGHTNESS value=%u state=deferred frame=0 visibility=unverified",unsigned(value));
  return true;
 }
 if(!byte(0x51,value)){markFailed();return false;}
 // Restore the old backend's brightness evidence, including zero, only after
 // the checked command completes. Do not log successful writes on refusal.
 evidence("PANEL_BRIGHTNESS value=%u state=applied frame=%u visibility=unverified",unsigned(value),unsigned(state.frameComplete));
 return true;
}
const RiscCpu::DisplayBackend backend={accepts,openPanel,row,brightness,closePanel};
}
namespace RiscCpu {const DisplayBackend* selectedDisplayBackend(){return &backend;}}

// A weak optional Runtime reference does not extract this member from an IDF
// component archive. The consuming build retains this distinct anchor; normal
// strong backend and frame/brightness proof checks still apply to the final ELF.
extern "C" const uint32_t risc_co5300_native_display_anchor=1;

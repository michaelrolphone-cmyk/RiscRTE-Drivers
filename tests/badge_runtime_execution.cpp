/* Execute actual Runtime, ProviderGraphV2, CpuPort, NativeRealtime and shared
 * Home/provider modules. Lowest GPIO, I2C registers, display transport and SDK
 * reset/timer/wall syscalls are modeled, alongside boot owner/health/confirmation/
 * diagnostic callbacks and the host file fixture. Host admission/dlopen is not
 * target Xtensa validation/relocation, native firmware boot, or physical proof. */
#include "ports/esp32s3/CpuPort.h"
#include "RuntimeFileFixture.h"
#include "diagnostics/StageLog.h"
#include <RiscDisplaySpiV2.h>
#include <RiscProviderV2.h>
#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <map>
#include <string>
#include <sys/time.h>
#include <vector>
#include <esp_system.h>

namespace {
struct Model {
    std::string mode;
    uint64_t ms=0,servicesAt=0,selectedAt=0,resetAt=0;
    unsigned health=0,io=0,rows=0,frames=0,unblanks=0,touchReads=0,contactDown=0,contactUp=0;
    unsigned i2cOpens=0,i2cCloses=0,displayOpens=0,displayCloses=0,realtimeReads=0,seedCalls=0,seeds=0,wallReads=0;
    unsigned loads=0,unloads=0,firstFrame=0,firstInput=0,promotions=0,confirmations=0,servicesCount=0;
    unsigned probeInit=0,probeFini=0,probeProgress=0,launchRefusals=0;
    bool pins[49]{},levels[49]{},i2c=false,display=false,ready=false,services=false;
    bool badSupplyOnce=false,rejectCloseOnce=false;
    uint16_t reg=0;uint8_t level=0,requestedLevel=0,touchMode=0;bool frameComplete=false,touchSleeping=false;
    std::vector<uint16_t> pixels=std::vector<uint16_t>(466*466);
    std::vector<std::string> stages,unloaded;
    std::map<void*,std::string> mapped;
} m;
RiscCpu::Port* cpu;
RiscBoot::Runtime* running;
bool owner(){return true;}
uint64_t now(){return m.ms;}
void delay(uint32_t ms){assert(ms<=5000);m.ms+=ms;assert(m.ms<120000);}
bool mapped(const char* id){for(const auto& e:m.mapped)if(e.second==id)return true;return false;}
bool home(){return m.mode.compare(0,5,"home-")==0 || m.mode=="home";}
bool retainedCase(){return m.mode=="probe-start-retained" || m.mode=="probe-i2c-retained" || m.mode=="home-shutdown-retained";}
/* Native clock availability is not the fixture's normal stop signal.
 * Preserve every scenario observation/bound and stop predicate below, but
 * end Home through the explicit host-only observation loop gate. Runtime
 * controlExitRequested remains unbound: its native Home navigation action is
 * not a stop-run request. This is not a product/user return or target execution. */
static bool fixtureContinue(){
    assert(++m.health<1000000);
    /* A fixture lifetime bound, not a public capability replacement. The app
     * owns its normal exit and Runtime performs its real checked teardown. */
    if(m.services && m.ms-m.servicesAt>=1100 && m.rows%466==0)return false;
    return true;
}
static bool fixtureStopRequested;
bool fixtureMonotonic(uint32_t* out){*out=static_cast<uint32_t>(m.ms);return true;}
bool fixtureObservationStop(){
 if(!fixtureStopRequested)fixtureStopRequested=!fixtureContinue();
 return fixtureStopRequested;
}
extern "C" int badge_fixture_home_continue(){
 const bool keep=!fixtureObservationStop();
 if(!keep)puts("FIXTURE_CONTROLLED_HOME_EXIT completed observation; not a user return or target instruction execution");
 return keep?1:0;
}
bool health(risc_runtime_health_v1* out){
 out->uptime_ms=static_cast<uint32_t>(m.ms);return true;
}
bool log(const char* line){
    m.stages.emplace_back(line);puts(line);
    if(!strcmp(line,"WATCH_CLOCK ready crown=enabled"))m.ready=true;
    if(strstr(line,"stage=first-frame ")){++m.firstFrame;assert(m.frames&&m.unblanks&&m.level&&m.rows%466==0);}
    if(strstr(line,"stage=first-input-poll ")){++m.firstInput;assert(m.ready&&m.firstFrame&&m.touchReads);}
    if(strstr(line,"stage=services-ready ")){
        ++m.servicesCount;
        assert(m.servicesCount<=2&&m.ready&&m.firstFrame==m.servicesCount&&m.firstInput==m.servicesCount&&
               m.promotions==1&&m.confirmations==m.servicesCount);
        const auto usage=running->grantUsage();assert(usage.bootPins==4&&usage.selectedProviders==4);
        if(!m.services)m.servicesAt=m.ms;m.services=true;
    }
    if(!strcmp(line,"WATCH_CLOCK launch=failed action=visible-status"))++m.launchRefusals;
    return true;
}
bool confirm(){assert(m.firstFrame&&m.firstInput&&m.promotions==1);++m.confirmations;return true;}
bool gpioOpen(uint8_t pin,bool output,bool initial,bool pullup){
    ++m.io;assert((pin==1||pin==2)&&output&&!initial&&!pullup&&!m.pins[pin]);
    m.pins[pin]=true;m.levels[pin]=initial;if(pin==2)m.resetAt=m.ms;return true;
}
bool gpioWrite(uint8_t pin,bool high){
    ++m.io;assert(pin==2&&m.pins[pin]);
    if(high){assert(!m.levels[pin]&&m.ms-m.resetAt>=10);m.touchMode=0;m.touchSleeping=false;}
    else m.resetAt=m.ms;
    m.levels[pin]=high;return true;
}
bool gpioRead(uint8_t pin,bool* out){++m.io;assert(pin==2&&m.pins[pin]);*out=m.levels[pin];return true;}
bool gpioPwm(uint8_t,uint32_t,uint16_t,uint16_t){assert(false);return false;}
bool gpioClose(uint8_t pin){++m.io;assert((pin==1||pin==2)&&m.pins[pin]);m.pins[pin]=false;return true;}
bool i2cOpen(uint8_t physical,uint8_t sda,uint8_t scl,uint32_t hz){
    ++m.io;++m.i2cOpens;assert(physical==0&&sda==15&&scl==14&&hz==400000&&!m.i2c);
    m.i2c=true;return m.mode!="probe-i2c-retained";
}
bool i2cClose(uint8_t physical){
    ++m.io;++m.i2cCloses;assert(physical==0&&m.i2c);
    if(m.mode=="probe-i2c-retained")return false;
    m.i2c=false;return true;
}
bool i2cTransfer(uint8_t physical,uint8_t address,const uint8_t* tx,size_t tn,uint8_t* rx,size_t rn,uint32_t timeout){
    ++m.io;assert(physical==0&&m.i2c&&timeout==20);
    if(address==0x34){
        /* Every PMIC access must be a register read. No rail writes exist. */
        assert(tx&&tn==1&&rx&&rn==1);
        if(tx[0]==3){*rx=(m.mode=="home-supply-failure"||m.badSupplyOnce)?0:0x4a;m.badSupplyOnce=false;}
        else if(tx[0]==0x80)*rx=1;
        else {assert(tx[0]==0x82);*rx=18;}
        return true;
    }
    assert(address==0x5a&&m.pins[2]&&m.levels[2]&&m.ms-m.resetAt>=60);
    assert(!(tn&&rn));
    if(tn){assert(tx&&tn==2&&!rx);m.reg=uint16_t((tx[0]<<8)|tx[1]);if(m.reg==0xd11e)m.touchMode=0x1e;if(m.reg==0xd101)m.touchMode=1;if(m.reg==0xd105)m.touchSleeping=true;m.selectedAt=m.ms;return true;}
    assert(!tx&&rx&&m.ms-m.selectedAt>=2);
    if(m.reg==0x0002){assert(rn==4||rn==2);memset(rx,0,rn);rx[1]=m.touchMode;return true;}
    if(m.reg==0xd1f8){assert(rn==4);unsigned width=m.mode=="home-touch-failure"?465:466;rx[0]=width;rx[1]=width>>8;rx[2]=466&255;rx[3]=466>>8;return true;}
    if(m.reg==0xd1fc||m.reg==0xd204){assert(rn==4);memset(rx,0,rn);return true;}
    assert(m.reg==0xd000&&rn==10);++m.touchReads;memset(rx,0,rn);rx[6]=0xab;
    const auto age=m.services?m.ms-m.servicesAt:0;const bool down=m.servicesCount==1&&age>=100&&age<360;
    const unsigned x=age<200?310:100,y=179;
    rx[5]=down;rx[0]=down?6:0;rx[1]=x>>4;rx[2]=y>>4;rx[3]=uint8_t((x&15)<<4|(y&15));
    if(down)++m.contactDown;else if(m.contactDown)++m.contactUp;
    return true;
}
bool displayAccepts(const risc_hardware_device_v1& d,uint8_t physical){
    if(physical!=2||!d.config||d.config_version!=2||d.config_size!=sizeof(risc_hw_spi_display_v2)||
       strcmp(d.compatible,"chipone,co5300")||strcmp(d.config_type,"display.spi"))return false;
    const auto& c=*static_cast<const risc_hw_spi_display_v2*>(d.config);
    return c.base.width==466&&c.base.height==466&&c.base.offset_x==6&&!c.base.offset_y&&!c.base.rotation&&
        c.base.reset==1&&c.base.bus.sclk==38&&c.base.bus.mosi==4&&c.base.bus.miso==5&&c.data2==6&&c.data3==7&&c.base.cs==12;
}
bool displayOpen(const risc_hardware_device_v1& d,uint8_t physical){
    ++m.io;++m.displayOpens;assert(displayAccepts(d,physical)&&!m.display&&!m.pins[1]);m.display=true;m.frameComplete=false;m.level=m.requestedLevel=0;
    return m.mode!="home-display-failure"&&m.mode!="probe-start-retained";
}
bool displayRow(uint32_t y,const uint16_t* row,size_t width){
    ++m.io;assert(m.display&&row&&width==466&&y==m.rows%466);
    std::copy(row,row+width,m.pixels.begin()+y*width);++m.rows;if(y==465){
        ++m.frames;
        if(!m.frameComplete){m.frameComplete=true;m.level=m.requestedLevel;if(m.level)++m.unblanks;}
    }return true;
}
bool displayBrightness(uint8_t level){
    ++m.io;assert(m.display);m.requestedLevel=level;
    // Selected native backend queues nonzero startup brightness until the
    // first complete checked frame, preserving the dark-start contract.
    if(!m.frameComplete&&level){assert(!m.level);return true;}
    if(level){assert(m.frames&&m.rows%466==0);++m.unblanks;}
    m.level=level;return true;
}
bool displayClose(){
    ++m.io;++m.displayCloses;assert(m.display);
    if(m.mode=="probe-start-retained"||m.mode=="home-shutdown-retained")return false;
    if(m.rejectCloseOnce){m.rejectCloseOnce=false;return false;}
    m.display=false;return true;
}
bool bind(RiscBoot::Runtime& runtime){return cpu->bind(runtime);}
}

/* Execute the actual NativeRealtime implementation, with only SDK syscalls
 * modeled. Wall-clock reads/seeds are forbidden, even when host time is valid. */
esp_reset_reason_t esp_reset_reason(){return ESP_RST_POWERON;}
int64_t esp_timer_get_time(){return static_cast<int64_t>(m.ms*1000);}
static int badgeWallRead(timeval*,void*){++m.wallReads;assert(false);return -1;}
static int badgeWallSet(const timeval*,const void*){++m.seeds;assert(false);return -1;}
#define gettimeofday badgeWallRead
#define settimeofday badgeWallSet
#include "ports/esp32s3/NativeRealtime.cpp"
#undef gettimeofday
#undef settimeofday

namespace RiscDiagnostics {
uint64_t monotonicUs(){return m.ms*1000;}
void timestamped(const char* format,...){
    char line[1024];va_list args;va_start(args,format);vsnprintf(line,sizeof(line),format,args);va_end(args);
    m.stages.emplace_back(line);puts(line);
    if(!strncmp(line,"providers retention armed ",26)){
        ++m.promotions;assert(m.mapped.size()==5);
        if(home())assert(m.firstFrame&&m.firstInput&&m.ready);
    }
}
}

/* Observe actual host loader calls, without changing their outcome, files,
 * symbols or visibility. The Runtime test shim creates independent images,
 * so checking the original on-disk path with RTLD_NOLOAD would be misleading. */
extern "C" void* __real_dlopen(const char*,int);
extern "C" int __real_dlclose(void*);
extern "C" void* __wrap_dlopen(const char* path,int flags){
    void* handle=__real_dlopen(path,flags);if(!handle)return handle;
    std::string id="default";
    auto get=reinterpret_cast<risc_driver_get_v2_fn>(dlsym(handle,"t5_driver_get"));
    if(get){const auto* driver=get(2);assert(driver);id=driver->driver_id;}
    else assert(dlsym(handle,"app_main"));
    assert(!m.mapped.count(handle));m.mapped.emplace(handle,id);++m.loads;return handle;
}
extern "C" int __wrap_dlclose(void* handle){
    auto found=m.mapped.find(handle);assert(found!=m.mapped.end());const auto id=found->second;
    if(id=="co5300-display")assert(!m.display);
    if(id=="cst9217-touch")assert(!m.pins[2]);
    if(id=="twatch-i2c")assert(!m.i2c);
    if(id=="default"&&m.mode=="probe-start-retained")assert(false);
    int rc=__real_dlclose(handle);if(!rc){++m.unloads;m.unloaded.push_back(id);m.mapped.erase(found);}return rc;
}
extern "C" const char* badge_probe_mode(){return m.mode.c_str();}
extern "C" void badge_probe_event(const char* event){
    if(!strcmp(event,"init")){++m.probeInit;return;}
    if(!strcmp(event,"fini")){++m.probeFini;return;}
    ++m.probeProgress;const auto usage=running->grantUsage();
    if(!strcmp(event,"start-refused-clean")){
        assert(cpu->quiescent()&&!running->retained()&&!m.i2c&&!m.display);
        assert(m.i2cOpens==1&&m.i2cCloses==1&&m.mapped.size()==1&&!usage.graphLive);
    }else if(!strcmp(event,"reject-next-close"))m.rejectCloseOnce=true;
    else if(!strcmp(event,"release-refused-mapped")){
        assert(!cpu->quiescent()&&m.display&&m.i2c&&!running->retained());
        assert(mapped("co5300-display")&&mapped("axp2101-dcdc1-observer")&&mapped("twatch-i2c"));
        assert(usage.graphLive==1&&usage.hostProviders==1);
    }else if(!strcmp(event,"release-retried-clean")){
        assert(cpu->quiescent()&&!m.i2c&&!m.display&&m.mapped.size()==1&&!usage.graphLive);
    }else if(!strcmp(event,"promoted"))assert(usage.bootPins==4&&usage.hostProviders==2);
    else if(!strcmp(event,"released-pinned"))assert(usage.bootPins==4&&!usage.hostLive&&m.mapped.size()==5);
    else {assert(!strcmp(event,"retained")&&running->retained()&&!cpu->quiescent());}
}

int main(int argc,char** argv){
    setvbuf(stdout,nullptr,_IONBF,0);assert(argc==3);m.mode=argv[2];m.badSupplyOnce=m.mode=="probe-start-retry";
    RiscCpu::NativeRealtime::configure(owner);RiscCpu::NativeRealtime::start();
    RiscCpu::Hardware h{};h.owner=owner;h.now=now;h.sleep=delay;
    h.gpioOpen=gpioOpen;h.gpioWrite=gpioWrite;h.gpioRead=gpioRead;h.gpioPwm=gpioPwm;h.gpioClose=gpioClose;
    h.i2cOpen=i2cOpen;h.i2cTransfer=i2cTransfer;h.i2cClose=i2cClose;
    h.spiOpen=[](uint8_t,int16_t,int16_t,int16_t){assert(false);return false;};
    h.spiBegin=[](uint8_t,uint8_t,uint32_t,uint8_t,uint32_t){assert(false);return false;};
    h.spiTransfer=[](uint8_t,const uint8_t*,uint8_t*,size_t,uint32_t){assert(false);return false;};
    h.spiEnd=[](uint8_t,uint8_t,uint32_t){assert(false);return false;};h.spiClose=[](uint8_t){assert(false);return false;};
    h.realtimeRead=[](risc_realtime_snapshot_v1* out){++m.realtimeReads;const auto rc=RiscCpu::NativeRealtime::read(out);assert(rc==RISC_REALTIME_OK&&out->validity==RISC_REALTIME_UNSET&&!out->epoch_seconds&&!out->nanoseconds);return rc;};
    h.realtimeSeed=[](int64_t epoch,uint32_t nanos){++m.seedCalls;return RiscCpu::NativeRealtime::seed(epoch,nanos);};
    static const RiscCpu::DisplayBackend backend={displayAccepts,displayOpen,displayRow,displayBrightness,displayClose};h.displayBackend=&backend;
    RiscCpu::Port port(h);cpu=&port;RiscBoot::Port boot{owner,health,delay,log,bind};boot.monotonicMs=fixtureMonotonic;boot.confirmBoot=confirm;
    RiscTest::FileSources files({std::string(argv[1])});RiscBoot::Runtime runtime(boot);running=&runtime;
    assert(files.bind(runtime));
    if(!runtime.prepare(argv[1])){fprintf(stderr,"prepare failed: %s\n",runtime.error());return 1;}
    assert(port.quiescent()&&!m.io&&!m.ms&&!m.health&&!m.loads&&!m.realtimeReads);
    const bool result=runtime.run();const auto usage=runtime.grantUsage();
    assert(result!=retainedCase()&&runtime.retained()==retainedCase());
    assert(!m.seedCalls&&!m.seeds&&!m.wallReads);
    if(m.mode=="home"||m.mode=="home-shutdown-retained"){
        assert(m.ready&&m.servicesCount==2&&m.firstFrame==2&&m.firstInput==2&&m.promotions==1&&m.confirmations==2);
        assert(m.rows>=466&&m.rows%466==0&&m.contactDown&&m.contactUp&&m.launchRefusals==1&&m.realtimeReads);
        assert(m.i2cOpens==1&&m.displayOpens==1&&m.loads==6);
    }else if(home())assert(!m.ready&&!m.firstFrame&&!m.promotions&&!m.confirmations&&!m.rows);
    if(retainedCase()){
        assert(!port.quiescent()&&m.i2c&&mapped("twatch-i2c"));
        if(m.mode=="probe-i2c-retained")assert(!port.appExitSafe()&&m.i2cCloses==1);
        else assert(m.display&&mapped("co5300-display")&&mapped("axp2101-dcdc1-observer"));
        if(m.mode=="home-shutdown-retained")assert(!mapped("default")&&m.unloads==3); /* Both Home invocations and touch cleanly exited. */
        else assert(mapped("default")&&m.probeInit==1&&!m.probeFini&&!m.unloads);
        const auto io=m.io,loads=m.loads,unloads=m.unloads;
        assert(!runtime.run()&&m.io==io&&m.loads==loads&&m.unloads==unloads);
        printf("BADGE_RUNTIME mode=%s result=PASS retained=true loaded=%u unloaded=%u rows=%u touch=%u boot_pins=%zu\n",m.mode.c_str(),m.loads,m.unloads,m.rows,m.touchReads,usage.bootPins);
        /* Production intentionally retains these images, tables and physical
         * custody until restart. Destruction is forbidden, not cleanup. */
        fflush(stdout);std::_Exit(0);
    }
    assert(port.quiescent()&&port.appExitSafe()&&!m.display&&!m.i2c&&m.mapped.empty()&&m.loads==m.unloads);
    for(bool pin:m.pins)assert(!pin);
    assert(!usage.graphLive&&!usage.bootPins&&!usage.hostLive&&!usage.hostProviders);
    if(!home())assert(m.probeInit==1&&m.probeFini==1&&m.probeProgress>=3);
    if(m.mode=="probe-start-retry")assert(m.i2cOpens==2&&m.i2cCloses==2&&m.promotions==1);
    if(m.mode=="probe-release-retry")assert(m.displayCloses==2&&!m.promotions);
    printf("BADGE_RUNTIME mode=%s result=PASS retained=false loaded=%u unloaded=%u rows=%u touch=%u boot_pins=%zu\n",m.mode.c_str(),m.loads,m.unloads,m.rows,m.touchReads,usage.bootPins);
}

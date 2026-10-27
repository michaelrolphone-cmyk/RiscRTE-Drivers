// Production Runtime::prepare and CpuPort::bind, with a deliberately inert
// lowest hardware boundary. No provider activation may occur during admission.
#include "bootstrap/Runtime.h"
#include "ports/esp32s3/CpuPort.h"
#include "badge_time_sample_fixture.h"
#include "RuntimeFileFixture.h"
#include <cstdio>
#include <cstring>
#include "RiscKeyValueV2.h"
#ifdef BADGE_IMAGE_ADMISSION
#include "cohort_elf_admission.h"
#endif
static unsigned ioCalls;
static uint32_t settingsCapacity=RISC_KEY_VALUE_V2_BLOB_MAX;
static RiscCpu::Port* cpu;
static bool owner(){return true;}
static bool bind(RiscBoot::Runtime& runtime){return cpu->bind(runtime);}
static RiscCpu::Hardware hardware(){
    RiscCpu::Hardware h{};
    h.owner=owner;
    h.now=[]()->uint64_t{++ioCalls;return 0;};
    h.sleep=[](uint32_t){++ioCalls;};
    h.gpioOpen=[](uint8_t,bool,bool,bool){++ioCalls;return false;};
    h.gpioWrite=[](uint8_t,bool){++ioCalls;return false;};
    h.gpioRead=[](uint8_t,bool*){++ioCalls;return false;};
    h.gpioPwm=[](uint8_t,uint32_t,uint16_t,uint16_t){++ioCalls;return false;};
    h.gpioClose=[](uint8_t){++ioCalls;return false;};
    h.i2cOpen=[](uint8_t,uint8_t,uint8_t,uint32_t){++ioCalls;return false;};
    h.i2cTransfer=[](uint8_t,uint8_t,const uint8_t*,size_t,uint8_t*,size_t,uint32_t){++ioCalls;return false;};
    h.i2cClose=[](uint8_t){++ioCalls;return false;};
    h.spiOpen=[](uint8_t,int16_t,int16_t,int16_t){++ioCalls;return false;};
    h.spiBegin=[](uint8_t,uint8_t,uint32_t,uint8_t,uint32_t){++ioCalls;return false;};
    h.spiTransfer=[](uint8_t,const uint8_t*,uint8_t*,size_t,uint32_t){++ioCalls;return false;};
    h.spiEnd=[](uint8_t,uint8_t,uint32_t){++ioCalls;return false;};
    h.spiClose=[](uint8_t){++ioCalls;return false;};
    h.realtimeRead=[](risc_realtime_snapshot_v1*)->int32_t{++ioCalls;return RISC_REALTIME_IO;};
    h.realtimeSeed=[](int64_t,uint32_t)->int32_t{++ioCalls;return RISC_REALTIME_IO;};
    // The transport has its own native-backend qualification. Admission only
    // models its pure acceptance hook, never an I/O or public provider table.
    static const RiscCpu::DisplayBackend display={
        [](const risc_hardware_device_v1& d,uint8_t physical){
            if(physical!=2 || !d.config || d.config_version!=2 ||
               d.config_size!=sizeof(risc_hw_spi_display_v2) ||
               strcmp(d.compatible,"chipone,co5300") || strcmp(d.config_type,"display.spi"))return false;
            const auto& c=*static_cast<const risc_hw_spi_display_v2*>(d.config);
            return c.base.width==466 && c.base.height==466 && c.base.offset_x==6 &&
                !c.base.offset_y && !c.base.rotation && c.base.reset==1 &&
                c.base.bus.sclk==38 && c.base.bus.mosi==4 && c.base.bus.miso==5 &&
                c.data2==6 && c.data3==7 && c.base.cs==12;
        },
        [](const risc_hardware_device_v1&,uint8_t){++ioCalls;return false;},
        [](uint32_t,const uint16_t*,size_t){++ioCalls;return false;},
        [](uint8_t){++ioCalls;return false;},
        [](){++ioCalls;return false;}};
    h.displayBackend=&display;
#ifdef BADGE_PROTOTYPE_ADMISSION
    h.radioJoin=[](const char*,const char*){++ioCalls;return false;};
    h.radioState=[](uint8_t*,int8_t*){++ioCalls;return false;};
    h.radioLeave=[](){++ioCalls;return false;};h.radioIdle=[](){++ioCalls;return false;};
    h.radioAddresses=[](uint8_t*,uint8_t*){++ioCalls;return false;};
    h.radioScanStart=[](){++ioCalls;return false;};h.radioScanCancel=[](){++ioCalls;return false;};
    h.radioScanPoll=[](garden_radio_scan_result_v1*){++ioCalls;return false;};
#endif
#ifdef BADGE_TIME_SERVER_SYNC
    badgeTimeSampleHardware(h);
#endif
    return h;
}
#ifdef BADGE_PROTOTYPE_ADMISSION
/* Semantic native-storage boundary for graph admission only. Every data call
 * increments ioCalls and fails; prepare must never read or mutate user data. */
class AdmissionSettings final: public RiscBoot::SettingsSession {
 public:
 uint32_t maxBlobSize()const override{return settingsCapacity;}
 bool configure(uint32_t,uint32_t,bool checkpoints,const uint32_t*,uint32_t shared)override{return !checkpoints&&!shared;}
 uint32_t checkpointCount()const override{return 0;} uint32_t format()const override{return 1;}
 uint32_t sharedCount()const override{return 0;}uint32_t sharedNamespace(uint32_t)const override{return 0;}
 uint32_t status(uint32_t)const override{return 0;}
 int32_t get(uint32_t,const char*,void*,uint32_t,uint32_t*,uint32_t,bool)override{++ioCalls;return -1;}
 int32_t put(uint32_t,const char*,const void*,uint32_t,bool)override{++ioCalls;return -1;}
 int32_t restore(uint32_t,const char*,const void*,uint32_t,bool)override{++ioCalls;return -1;}
 int32_t snapshot(uint32_t,void*,uint32_t,uint32_t*,uint64_t*)override{++ioCalls;return -1;}
 int32_t acknowledge(uint32_t,uint64_t)override{++ioCalls;return -1;}
 bool launchReady(uint32_t,bool*)const override{return true;}
};
static const RiscBoot::SettingsProvider settings{nullptr,[](void*){return std::unique_ptr<RiscBoot::SettingsSession>(new AdmissionSettings);}};
static const RiscBoot::AppDataBackend appData{nullptr,
 [](void*,uint32_t,const char*,uint32_t*,uint64_t*)->int32_t{++ioCalls;return -1;},
 [](void*,uint32_t,const char*,uint64_t,void*,uint32_t,uint32_t*,uint64_t*)->int32_t{++ioCalls;return -1;},
 [](void*,uint32_t,const char*,uint64_t,const void*,uint32_t)->int32_t{++ioCalls;return -1;},
 [](void*){return true;}};
#endif
#ifndef BADGE_EXPECTED_IMAGES
#define BADGE_EXPECTED_IMAGES 5
#endif
int main(int argc,char** argv){
    if(argc!=2 && argc!=3)return 2;
    if(argc==3){if(std::strcmp(argv[2],"small-kv"))return 2;settingsCapacity=64;}
    RiscTest::FileSources files({std::string(argv[1])});
    RiscCpu::Port port(hardware());cpu=&port;
    RiscBoot::Port runtimePort{owner,[](risc_runtime_health_v1*){++ioCalls;return false;},
        [](uint32_t){++ioCalls;},[](const char*){return true;},bind};
#ifdef BADGE_PROTOTYPE_ADMISSION
    runtimePort.settings=&settings;runtimePort.appData=&appData;
#endif
    RiscBoot::Runtime runtime(runtimePort);
    if(!files.bind(runtime))return 2;
    const bool prepared=runtime.prepare(argv[1]);
    bool images=false;unsigned count=0;
#ifdef BADGE_IMAGE_ADMISSION
    images=prepared && runtime.inspectImages(CohortElf::file,&count);
#endif
    std::printf("{\"prepared\":%s,\"images_validated\":%s,\"elf_count\":%u,\"hardware_calls\":%u,\"error\":\"%s\"}\n",
        prepared?"true":"false",images?"true":"false",count,ioCalls,runtime.error());
    if(ioCalls)return 3;
#ifdef BADGE_IMAGE_ADMISSION
    return prepared && images && count==BADGE_EXPECTED_IMAGES?0:4;
#else
    return prepared?0:4;
#endif
}

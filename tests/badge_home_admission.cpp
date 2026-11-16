// Production Runtime::prepare and CpuPort::bind, with a deliberately inert
// lowest hardware boundary. No provider activation may occur during admission.
#include "bootstrap/Runtime.h"
#include "ports/esp32s3/CpuPort.h"
#include "RuntimeFileFixture.h"
#include <cstdio>
#include <cstring>
#ifdef BADGE_IMAGE_ADMISSION
#include "cohort_elf_admission.h"
#endif
static unsigned ioCalls;
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
    return h;
}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    RiscTest::FileSources files({std::string(argv[1])});
    RiscCpu::Port port(hardware());cpu=&port;
    RiscBoot::Runtime runtime({owner,[](risc_runtime_health_v1*){++ioCalls;return false;},
        [](uint32_t){++ioCalls;},[](const char*){return true;},bind});
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
    return prepared && images && count==5?0:4;
#else
    return prepared?0:4;
#endif
}

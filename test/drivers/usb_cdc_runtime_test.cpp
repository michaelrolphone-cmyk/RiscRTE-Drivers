#include "bootstrap/Runtime.h"
#include "runtime/streams/ProviderQueueHost.h"
#include "RuntimeFileFixture.h"
#include "RiscPlatformClockV1.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>
static std::string mode;static unsigned phase;static std::vector<std::string>events;
extern "C" const char*cdc_test_mode(){return mode.c_str();}
extern "C" unsigned cdc_test_phase(){return phase;}
extern "C" void cdc_test_set_phase(unsigned p){phase=p;}
extern "C" void cdc_test_event(const char*p){events.emplace_back(p);}
static size_t count(const char*p){return std::count(events.begin(),events.end(),p);}
extern "C" unsigned cdc_test_transfers(){return unsigned(count("transfer"));}
static void file(const std::string&r,const char*p,const std::string&s){std::ofstream(r+"/"+p)<<s;}
static void manifest(const std::string&r,const char*p,const char*id,const char*elf,const char*cap,const char*deps){file(r,p,std::string("{\"type\":\"driver\",\"id\":\"")+id+"\",\"version\":\"1.0.0\",\"driver_abi\":2,\"architecture\":\"xtensa-esp32s3\",\"file_name\":\""+elf+"\",\"requires\":"+deps+",\"provides\":[{\"capability\":\""+cap+"\",\"api\":1}]}");}
int main(int argc,char**argv){
 assert(argc==4);std::string root=argv[1];mode=argv[2];std::string activation=argv[3];
 file(root,"board.json",R"({"schema":"riscrte.board-hardware","schema_version":1,"board_id":"test","revision":"unspecified","buses":[],"devices":[]})");
 manifest(root,"controller.json","cdc-test-controller","controller.elf","usb.controller","[]");
 file(root,"app.json",R"({"type":"application","id":"cdc-native-test","version":"1.0.0","architecture":"xtensa-esp32s3","file_name":"default.elf","entry":"app_main","requires":[{"capability":"serial.port","api":1},{"capability":"usb.host","api":1}]})");
 file(root,"boot.json",std::string(R"({"board":"board.json","default_app":"default.elf","provider_activation":")")+activation+R"(","drivers":[{"manifest":"serial/manifest.json"},{"manifest":"host/manifest.json"},{"manifest":"controller.json"}],"app_capabilities":[{"manifest":"app.json","grants":[{"capability":"serial.port","api":1,"instance_id":0},{"capability":"usb.host","api":1,"instance_id":0}]}]})");
 static uint32_t now=0;static RiscTest::FileSources files({root.c_str()});
 auto*r=new RiscBoot::Runtime({[](){return true;},[](risc_runtime_health_v1*h){h->uptime_ms=++now;return true;},[](uint32_t){},[](const char*){return true;}});assert(files.bind(*r));static risc_platform_clock_api_v1 clock={1,sizeof(clock),nullptr,[](void*)->uint64_t{return 0;},nullptr};assert(r->registerPlatform("platform.clock",1,RiscBoot::Runtime::Scope::Global,0,&clock));if(!r->prepare(root.c_str())){fprintf(stderr,"prepare: %s\n",r->error());return 2;}
 bool retained=mode=="open-retained"||mode=="close-retained";bool result=r->run();assert(result==!retained&&r->retained()==retained);
 if(retained){assert(!count("client:fini")&&!count("controller:stop"));assert(RuntimeStreams::Testing::allocatedBytes()==(mode=="open-retained"?0:4096));printf("PASS actual Runtime/CDC/host/client %s/%s retained\n",mode.c_str(),activation.c_str());fflush(stdout);std::_Exit(0);}
 assert(count("client:fini")==1&&count("controller:stop")==1&&!RuntimeStreams::Testing::allocatedBytes());delete r;
 printf("PASS actual Runtime/CDC/host/client %s/%s\n",mode.c_str(),activation.c_str());
}

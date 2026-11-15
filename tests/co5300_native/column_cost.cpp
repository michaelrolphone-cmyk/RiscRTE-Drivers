#include <time.h>
#define main historical_native_suite
#include "test.cpp"
#undef main
static uint64_t cpu(){timespec t{};assert(clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&t)==0);return uint64_t(t.tv_sec)*1000000000+t.tv_nsec;}
int main(){model::clear();open();std::vector<uint16_t> image(size_t(TestWidth)*TestHeight);unsigned count=0;uint64_t total=0;
 for(unsigned frame=0;frame<35;frame++){for(size_t i=0;i<image.size();i++)image[i]=uint16_t(i+frame);model::packets.clear();auto start=cpu();for(unsigned y=0;y<TestHeight;y++)assert(api->row(y,image.data()+size_t(y)*TestWidth,TestWidth));auto ns=cpu()-start;if(frame>=5){total+=ns;count+=model::packets.size();}}
 closed();printf("COST reuse=%u frames=30 transactions=%u cpu_ns=%llu\n",unsigned(RISC_CO5300_REUSE_COLUMNS),count,(unsigned long long)total);
}

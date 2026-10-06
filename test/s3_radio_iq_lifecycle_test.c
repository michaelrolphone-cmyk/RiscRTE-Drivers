#include <assert.h>
#include <stdio.h>
#include <string.h>
#define RISC_IQ_HOST_TEST
#include "../Drivers/s3_radio_iq_v1/driver.c"
static struct {uint32_t address,value;} registers[64];
static unsigned register_count, accesses, claims, releases, tx_writes;
static uint32_t bank_words[RING_PAIRS], cycles;
static uint8_t analog_regs[128][16];
static bool native_lease, admission=true, release_ok=true, pll_ok=true, pbus_ok=true, dump_ok=true, wrap;
static uint32_t *raw_register(uint32_t address) {
    for (unsigned i=0;i<register_count;++i) if(registers[i].address==address)return &registers[i].value;
    assert(register_count<64);registers[register_count].address=address;
    return &registers[register_count++].value;
}
volatile uint32_t *iq_test_register(uint32_t address) {
    assert(native_lease);++accesses;
    if(address==PBUS_STATUS_REG) {
        uint32_t command=*raw_register(PBUS_CTRL_REG);
        if(command&2) {
            unsigned group=(command>>2)&15;
            if(group==4 || group==5){assert(((command>>6)&511)==0);++tx_writes;}
        }
        if(pbus_ok)*raw_register(address)&=~0x80000000u;
        else *raw_register(address)|=0x80000000u;
    }
    return raw_register(address);
}
volatile uint32_t *iq_test_bank(void){assert(native_lease);return bank_words;}
uint32_t iq_test_cycles(void){
    assert(native_lease);cycles+=24001;
    if(dump_ok && (*raw_register(DUMP_CTRL_REG)&DUMP_CTRL_RUN)) {
        unsigned end=wrap?128:512;
        for(unsigned i=0;i<(wrap?RING_PAIRS:512);++i)bank_words[i]=i&0xfffff;
        *raw_register(DUMP_WRITE_INDEX_REG)=end;
    }
    return cycles;
}
uint8_t iq_test_analog_read(uint8_t block,uint8_t host,uint8_t reg){
    assert(native_lease && host==1);++accesses;
    if(block==I2C_RFPLL && reg==7)return pll_ok?2:0;
    if(block==I2C_RFPLL && reg==12)return 0;
    return analog_regs[block][reg];
}
void iq_test_analog_write(uint8_t block,uint8_t host,uint8_t reg,uint8_t value){
    assert(native_lease && host==1);++accesses;analog_regs[block][reg]=value;
}
unsigned iq_test_pbus_read(unsigned block,unsigned index){assert(native_lease && block<=3 && index<=2);return 0;}
void iq_test_delay(uint32_t us){assert(native_lease && us<=3000);++accesses;}
static bool claim_resource(void *context,uint64_t *token){
    (void)context;*token=0;++claims;assert(!native_lease);
    if(!admission)return false;native_lease=true;*token=claims;return true;
}
static bool release_resource(void *context,uint64_t token){
    (void)context;assert(native_lease && token);++releases;
    assert(!(*raw_register(DUMP_CTRL_REG)&DUMP_CTRL_RUN));
    assert(!(*raw_register(DUMP_BANK_SELECT_REG)&15));
    if(!release_ok)return false;native_lease=false;return true;
}
int main(void){
    risc_radio_iq_resource_v1 guard={1,sizeof(guard),NULL,claim_resource,release_resource,CAPTURE_BANK_BASE,CAPTURE_BANK_BYTES};
    risc_hw_radio_v1 config={sizeof(config),0,1};
    risc_hardware_device_v1 hw={1,sizeof(hw),17,"espressif,esp32s3-iq","unspecified","radio.integrated",1,sizeof(config),&config};
    risc_provider_dependency_v1 deps[]={{"hardware.device",1,&hw},{RISC_RADIO_IQ_RESOURCE_CAPABILITY,1,&guard}};
    uint32_t pairs[258];for(unsigned i=0;i<258;++i)pairs[i]=0xdeadbeef;
    assert(!start(NULL,0));guard.bank_bytes=1024;assert(!start(deps,2));guard.bank_bytes=CAPTURE_BANK_BYTES;
    config.features=3;assert(!start(deps,2));config.features=1;
    hw.compatible="other,soc";assert(!start(deps,2));hw.compatible="espressif,esp32s3-iq";
    assert(!accesses && !claims && start(deps,2));assert(!start(deps,2));assert(!accesses && !claims);
    assert(capture_burst(NULL,NULL,1)==RISC_RADIO_IQ_BAD_ARGUMENT);
    assert(capture_burst(NULL,pairs,0)==RISC_RADIO_IQ_BAD_ARGUMENT);
    assert(capture_burst(NULL,pairs,257)==RISC_RADIO_IQ_BAD_ARGUMENT);assert(!claims);
    admission=false;assert(capture_burst(NULL,pairs+1,256)==RISC_RADIO_IQ_BUSY);assert(!accesses);
    admission=true;
    for(unsigned i=0;i<sizeof(saved_addresses)/sizeof(saved_addresses[0]);++i)*raw_register(saved_addresses[i])=0;
    *raw_register(RTC_CNTL_DIG_PWC_REG)=RTC_CNTL_WIFI_FORCE_PD;
    *raw_register(RTC_CNTL_DIG_ISO_REG)=RTC_CNTL_WIFI_FORCE_ISO;
    *raw_register(SYSTEM_WIFI_CLK_EN_REG)=0x10000000;
    analog_regs[I2C_SDM][0]=0xab;
    for(unsigned n=0;n<3;++n){
        wrap=n==1;*raw_register(DUMP_WRITE_INDEX_REG)=12345;
        assert(capture_burst(NULL,pairs+1,256)==RISC_RADIO_IQ_OK);
        assert(!native_lease && !lease && pairs[0]==0xdeadbeef && pairs[257]==0xdeadbeef);
        assert(pairs[1]==(wrap?RING_PAIRS-128:256));assert(pairs[256]==(wrap?127:511));
        assert(*raw_register(RTC_CNTL_DIG_PWC_REG)==RTC_CNTL_WIFI_FORCE_PD);
        assert(*raw_register(RTC_CNTL_DIG_ISO_REG)==RTC_CNTL_WIFI_FORCE_ISO);
        assert(*raw_register(SYSTEM_WIFI_CLK_EN_REG)==0x10000000);
        assert(analog_regs[I2C_SDM][0]==0xab);
    }
    pll_ok=false;assert(capture_burst(NULL,pairs+1,256)==RISC_RADIO_IQ_PLL_FAILED);assert(!native_lease);pll_ok=true;
    dump_ok=false;assert(capture_burst(NULL,pairs+1,256)==RISC_RADIO_IQ_DUMP_TIMEOUT);assert(!native_lease);dump_ok=true;
    pbus_ok=false;assert(capture_burst(NULL,pairs+1,256)==RISC_RADIO_IQ_CLEANUP_RETAINED);assert(native_lease && lease);
    assert(!quiesce());stop();assert(running && lease);pbus_ok=true;assert(suspend_receiver(NULL));assert(!lease);
    release_ok=false;assert(capture_burst(NULL,pairs+1,256)==RISC_RADIO_IQ_CLEANUP_RETAINED);assert(lease && restored);
    unsigned before=accesses;assert(!suspend_receiver(NULL));assert(accesses==before);
    release_ok=true;assert(suspend_receiver(NULL));assert(accesses==before && !lease);
    assert(capture_burst(NULL,pairs+1,256)==RISC_RADIO_IQ_OK);assert(quiesce());stop();assert(!running && !resource);
    assert(capture_burst(NULL,pairs+1,256)==RISC_RADIO_IQ_NOT_RUNNING);assert(start(deps,2));stop();
    assert(tx_writes>=12);puts("IQ lifecycle: lazy admission, exclusive custody, bounded failures, retry, ring reset/wrap, restore, receive-only and restart passed");
}

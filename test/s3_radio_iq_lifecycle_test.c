#define _GNU_SOURCE
#include <assert.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#define RISC_IQ_HOST_TEST
#include "../Drivers/s3_radio_iq_v1/driver.c"
static struct {uint32_t address,value;} registers[64];
static unsigned register_count, accesses, claims, releases, tx_writes;
static uint32_t *bank_words, cycles;
static bool bank_protected,dump_was_running;
static bool reset_on_stop=true;
static unsigned pipeline_tail=3;
static uint32_t dump_cursor;
static unsigned pairs_per_poll=64, delay_polls, active_polls, dump_starts;
static uint32_t cycles_per_poll=24001, active_control, active_gain, active_width;
static unsigned active_rf, active_bb, active_filter;
static unsigned fail_pbus_command, pbus_commands;
static bool stale_reset, corrupt_pair, native_dirty_refusal, ckgen_reject, pll_lock=true;
static unsigned pbus_words[6][3];
static uint32_t pbus_command_seen;
static bool configuring_gain;
static uint8_t analog_regs[128][16];
static bool native_lease, admission=true, release_ok=true, pll_ok=true, pbus_ok=true, dump_ok=true, wrap;
static uint32_t *raw_register(uint32_t address) {
    for (unsigned i=0;i<register_count;++i) if(registers[i].address==address)return &registers[i].value;
    assert(register_count<64);registers[register_count].address=address;
    return &registers[register_count++].value;
}
static void protect_bank(bool locked){
    if(locked==bank_protected)return;
    assert(mprotect(bank_words,CAPTURE_BANK_BYTES,locked?PROT_NONE:(PROT_READ|PROT_WRITE))==0);
    bank_protected=locked;
}
static void sync_bank_owner(void){protect_bank((*raw_register(DUMP_BANK_SELECT_REG)&15u)!=0 && (*raw_register(DUMP_CTRL_REG)&DUMP_CTRL_RUN)!=0);}
volatile uint32_t *iq_test_register(uint32_t address) {
    assert(native_lease);sync_bank_owner();++accesses;
    if(address==AGC_GAIN_FORCE_REG && diagnostic_state.stage==RISC_RADIO_IQ_STAGE_RECEIVER && analog_saved && !configuring_gain){
        assert(!(analog_regs[ESP32S3_CKGEN_BLOCK][ESP32S3_CKGEN_REG]&ESP32S3_CKGEN_5_6_BIT));
        configuring_gain=true;
    }
    if(address==PBUS_CTRL_REG && !(*raw_register(address)&2))pbus_command_seen=0;
    if(address==PBUS_STATUS_REG) {
        uint32_t command=*raw_register(PBUS_CTRL_REG);
        if(command&2) {
            unsigned group=(command>>2)&15;
            if(group==4 || group==5){assert(((command>>6)&511)==0);++tx_writes;}
            if(command!=pbus_command_seen) {
                ++pbus_commands;pbus_command_seen=command;
                unsigned index=(command>>15)&3;
                if(group<6 && index<3)pbus_words[group][index]=(command>>6)&511;
            }
        }
        else pbus_command_seen=0;
        if(pbus_ok && (!fail_pbus_command || pbus_commands!=fail_pbus_command))*raw_register(address)&=~0x80000000u;
        else *raw_register(address)|=0x80000000u;
    }
    return raw_register(address);
}
volatile uint32_t *iq_test_bank(void){assert(native_lease);return bank_words;}
uint32_t iq_test_cycles(void){
    assert(native_lease);cycles+=cycles_per_poll;
    bool active=(*raw_register(DUMP_CTRL_REG)&DUMP_CTRL_RUN)!=0;
    if(dump_ok && (*raw_register(SYSTEM_WIFI_CLK_EN_REG)&WIFI_MAC_CLK_BIT6) && active) {
        if(!dump_was_running){
            ++dump_starts;active_polls=0;dump_cursor=wrap?RING_PAIRS-384:0;
            active_control=*raw_register(DUMP_CTRL_REG);
            active_gain=*raw_register(AGC_GAIN_FORCE_REG);
            active_width=*raw_register(FE_WIDTH_REG);
            active_rf=pbus_words[1][2];active_bb=pbus_words[0][1];
            unsigned filter=(active_width&0x003f0000u)?6:4;
            active_filter=analog_regs[I2C_BB_FILTER][filter]|((unsigned)analog_regs[I2C_BB_FILTER][filter+1]<<8);
            if(stale_reset)*raw_register(DUMP_WRITE_INDEX_REG)=dump_cursor;
        }
        protect_bank(false); /* emulate the ADC owner, then revoke CPU access */
        if(active_polls++>=delay_polls){
            for(unsigned i=0;i<pairs_per_poll;++i){bank_words[dump_cursor]=dump_cursor&0xfffff;dump_cursor=(dump_cursor+1)&RING_MASK;}
            *raw_register(DUMP_WRITE_INDEX_REG)=dump_cursor;
        }
    }
    dump_was_running=active;sync_bank_owner();return cycles;
}

uint8_t iq_test_analog_read(uint8_t block,uint8_t host,uint8_t reg){
    assert(native_lease && host==1);++accesses;
    if(block==I2C_RFPLL && reg==7)return pll_ok?2:0;
    if(block==I2C_RFPLL && reg==12)return pll_lock?0:12;
    if(ckgen_reject && analog_saved && block==ESP32S3_CKGEN_BLOCK && reg==ESP32S3_CKGEN_REG)
        return analog_regs[block][reg]&~ESP32S3_CKGEN_5_6_BIT;
    return analog_regs[block][reg];
}
void iq_test_analog_write(uint8_t block,uint8_t host,uint8_t reg,uint8_t value){
    assert(native_lease && host==1);++accesses;analog_regs[block][reg]=value;
}
unsigned iq_test_pbus_read(unsigned block,unsigned index){assert(native_lease && block<=3 && index<=2);return pbus_words[block][index];}
void iq_test_delay(uint32_t us){
    assert(native_lease && us<=3000);sync_bank_owner();
    if(!(*raw_register(DUMP_CTRL_REG)&DUMP_CTRL_RUN)) {
        if(dump_was_running && dump_ok) {
            for(unsigned n=0;n<pipeline_tail;++n){bank_words[dump_cursor]=dump_cursor&0xfffff;dump_cursor=(dump_cursor+1)&RING_MASK;}
            if(corrupt_pair)bank_words[(dump_cursor-7)&RING_MASK]=0xa5c33c5a;
            *raw_register(DUMP_WRITE_INDEX_REG)=reset_on_stop?0:dump_cursor;
        }
        dump_was_running=false;
    }
    ++accesses;
}
static bool claim_resource(void *context,uint64_t *token){
    (void)context;*token=0;++claims;assert(!native_lease);
    if(!admission){if(native_dirty_refusal){native_lease=true;*token=claims;}return false;}
    native_lease=true;configuring_gain=false;*token=claims;return true;
}
static bool release_resource(void *context,uint64_t token){
    (void)context;assert(native_lease && token);++releases;
    assert(!(*raw_register(DUMP_CTRL_REG)&DUMP_CTRL_RUN));
    assert(!(*raw_register(DUMP_BANK_SELECT_REG)&15));
    if(!release_ok)return false;native_lease=false;return true;
}
static unsigned trace_count;
static bool trace(void *context,const char *stage){
 assert(context==&trace_count && stage && strlen(stage)<80);
 assert(!(*raw_register(DUMP_CTRL_REG)&DUMP_CTRL_RUN));
 assert(!(*raw_register(DUMP_BANK_SELECT_REG)&15u));
 ++trace_count;return true;
}
static void assert_empty(const uint32_t *pairs,unsigned count,const risc_radio_iq_format_v1 *format){
    for(unsigned i=0;i<count;++i)assert(pairs[i]==0);
    risc_radio_iq_format_v1 empty={.struct_size=sizeof(empty)};
    assert(memcmp(format,&empty,sizeof(empty))==0);
}
static void assert_complete(const uint32_t *pairs,unsigned count,const risc_radio_iq_format_v1 *format){
    for(unsigned i=0;i<count;++i)assert(pairs[i]==((dump_cursor-count+i)&RING_MASK));
    assert(format->pair_count==count && format->component_bits==10 && format->component_full_scale==512);
    assert(format->sample_format==RISC_RADIO_IQ_FORMAT_S10_I0_Q10);
    assert(format->flags==(RISC_RADIO_IQ_FLAG_COHERENT_BURST|RISC_RADIO_IQ_FLAG_NOMINAL_FREQUENCIES|
                          RISC_RADIO_IQ_FLAG_UNCALIBRATED_AMPLITUDE));
    assert(!native_lease && !lease && !changed_dc);
}
static uint32_t digital_snapshot[sizeof(saved_addresses)/sizeof(saved_addresses[0])];
static uint8_t analog_snapshot[sizeof(saved_analog)];
static void snapshot(void){
    for(unsigned i=0;i<sizeof(digital_snapshot)/sizeof(digital_snapshot[0]);++i)
        digital_snapshot[i]=*raw_register(saved_addresses[i]);
    for(unsigned i=0;i<sizeof(analog_snapshot);++i)
        analog_snapshot[i]=analog_regs[analog_addresses[i][0]][analog_addresses[i][1]];
}
static void assert_restored(void){
    for(unsigned i=0;i<sizeof(digital_snapshot)/sizeof(digital_snapshot[0]);++i)
        assert(digital_snapshot[i]==*raw_register(saved_addresses[i]));
    for(unsigned i=0;i<sizeof(analog_snapshot);++i)
        assert(analog_snapshot[i]==analog_regs[analog_addresses[i][0]][analog_addresses[i][1]]);
    assert(!native_lease && !lease && !changed_dc);
}
static bool reentrant_trace(void *context,const char *stage){
    (void)context;(void)stage;
    uint32_t words[1];risc_radio_iq_format_v1 detail={.struct_size=sizeof(detail)};
    assert(capture_configured(NULL,words,1,NULL,&detail)==RISC_RADIO_IQ_BUSY);
    assert_empty(words,1,&detail);
    assert(!suspend_receiver(NULL) && !quiesce());stop();assert(running);
    return true;
}

static void extension_regressions(void){
    const risc_radio_iq_api_v1 *prefix=driver.capability;
    assert(prefix->api_version==1 && prefix->struct_size==sizeof(risc_radio_iq_temporal_api_v1));
    assert((const void *)&api.base.base==(const void *)&api);
    assert(prefix->capture_burst==capture_burst && api.base.base.capture_burst_traced==capture_burst_traced);
    risc_radio_iq_capabilities_v1 caps={.struct_size=sizeof(caps)};
    unsigned before=accesses;assert(api.base.capabilities(NULL,&caps) && accesses==before);
    assert(caps.min_pairs==1 && caps.max_pairs==8192 && caps.defaults.gain_selector==24);
    assert(caps.center_min_hz==1841666667u && caps.center_max_hz==2790000000u);
    assert(caps.sample_rates_hz[0]==16000000u && caps.sample_rates_hz[1]==80000000u);
    assert(caps.bandwidths_hz[0]==20000000u && caps.bandwidths_hz[1]==40000000u);
    assert(caps.gain_selector_max==127 && caps.rf_gain_max==511 && caps.bb_gain_max==127);
    assert(caps.filter_mask==0x3f3f && caps.dc_max==511 && caps.iq_correction_mask==0x3f1f);
    assert(caps.controls==511 && caps.automatic_value==RISC_RADIO_IQ_AUTO);
    assert(!api.base.capabilities(NULL,NULL));caps.struct_size=1;assert(!api.base.capabilities(NULL,&caps));
    uint32_t words[RISC_RADIO_IQ_MAX_PAIRS+2];
    risc_radio_iq_format_v1 format={.struct_size=sizeof(format)};
    risc_radio_iq_settings_v1 settings=RISC_RADIO_IQ_SETTINGS_DEFAULT;
    words[0]=words[RISC_RADIO_IQ_MAX_PAIRS+1]=0xdeadbeef;
    cycles_per_poll=192;pipeline_tail=3;delay_polls=0;stale_reset=true;
    const unsigned lengths[]={1,256,257,512,1024,2048,4096,8192};
    for(unsigned mode=0;mode<4;++mode){
        settings.sample_rate_hz=(mode&1)?16000000u:80000000u;
        settings.bandwidth_hz=(mode&2)?20000000u:40000000u;
        for(unsigned n=0;n<sizeof(lengths)/sizeof(lengths[0]);++n){
            unsigned count=lengths[n];wrap=(n&1)!=0;reset_on_stop=(n&2)!=0;
            delay_polls=n;pipeline_tail=n%4;*raw_register(DUMP_WRITE_INDEX_REG)=12345;
            words[count+1]=0xdeadbeef;unsigned starts=dump_starts;snapshot();
            assert(api.base.capture_configured(NULL,words+1,count,&settings,&format)==RISC_RADIO_IQ_OK);
            assert(dump_starts==starts+1);assert_complete(words+1,count,&format);assert_restored();
            assert(words[0]==0xdeadbeef && words[count+1]==0xdeadbeef);
            assert(format.sample_rate_hz==settings.sample_rate_hz && format.bandwidth_hz==settings.bandwidth_hz);
            assert((active_control&DUMP_CTRL_16MSPS)==((mode&1)?DUMP_CTRL_16MSPS:0));
            assert((active_width&0x003f0000u)==((mode&2)?0:0x00120000u));
        }
    }
    /* Bounds are rejected before the native claim, with no partially applied setting. */
    settings=(risc_radio_iq_settings_v1)RISC_RADIO_IQ_SETTINGS_DEFAULT;
    const risc_radio_iq_settings_v1 defaults=settings;
    const unsigned bad_values[]={1841666666u,2790000001u,0u,40000000u,10000000u,128u,512u,128u,0x40u,512u,0x20u};
    uint32_t *fields[]={&settings.center_hz,&settings.center_hz,&settings.center_hz,
        &settings.sample_rate_hz,&settings.bandwidth_hz,&settings.gain_selector,&settings.rf_gain,
        &settings.bb_gain,&settings.filter,&settings.dc[0],&settings.iq_correction};
    for(unsigned i=0;i<sizeof(bad_values)/sizeof(bad_values[0]);++i){
        settings=defaults;*fields[i]=bad_values[i];before=accesses;unsigned claimed=claims;
        memset(words+1,0xa5,8192*sizeof(*words));
        assert(capture_configured(NULL,words+1,8192,&settings,&format)==RISC_RADIO_IQ_BAD_ARGUMENT);
        assert_empty(words+1,8192,&format);assert(accesses==before && claims==claimed);
    }
    settings=defaults;settings.struct_size=1;
    assert(capture_configured(NULL,words+1,1,&settings,&format)==RISC_RADIO_IQ_BAD_ARGUMENT);
    settings=defaults;
    for(unsigned r=0;r<4;++r){settings.dc[r]=512;
        assert(capture_configured(NULL,words+1,1,&settings,&format)==RISC_RADIO_IQ_BAD_ARGUMENT);settings.dc[r]=RISC_RADIO_IQ_AUTO;}
    assert(capture_configured(NULL,words+1,0,NULL,&format)==RISC_RADIO_IQ_BAD_ARGUMENT);
    assert(capture_configured(NULL,words+1,8193,NULL,&format)==RISC_RADIO_IQ_BAD_ARGUMENT);
    assert(capture_configured(NULL,NULL,1,NULL,&format)==RISC_RADIO_IQ_BAD_ARGUMENT);
    assert(capture_configured(NULL,words+1,1,NULL,NULL)==RISC_RADIO_IQ_BAD_ARGUMENT);
    format.struct_size=1;assert(capture_configured(NULL,words+1,1,NULL,&format)==RISC_RADIO_IQ_BAD_ARGUMENT);
    format.struct_size=sizeof(format);

    /* Both LO conversion ranges and all raw controls use the upstream encodings. */
    const uint32_t centers[]={1841666667u,2000000000u,2209999999u,2210000000u,2440000000u,2790000000u};
    settings=defaults;settings.gain_selector=127;settings.rf_gain=511;settings.bb_gain=127;
    settings.filter=0x3f3f;settings.iq_correction=0x3f1f;
    for(unsigned r=0;r<4;++r){pbus_words[dc_block[r]][dc_index[r]]=r+11;settings.dc[r]=511-r;}
    for(unsigned i=0;i<sizeof(analog_snapshot);++i)analog_regs[analog_addresses[i][0]][analog_addresses[i][1]]=17+i;
    *raw_register(IQ_CORRECTION_REG)=0x40123456u;
    for(unsigned n=0;n<sizeof(centers)/sizeof(centers[0]);++n){
        settings.center_hz=centers[n];settings.bandwidth_hz=(n&1)?20000000u:40000000u;
        settings.gain_selector=(n&1)?0:127;settings.rf_gain=(n&1)?0:511;
        settings.bb_gain=(n&1)?0:127;settings.filter=(n&1)?0:0x3f3f;
        settings.iq_correction=(n&1)?0:0x3f1f;
        for(unsigned r=0;r<4;++r)settings.dc[r]=(n&1)?0:511-r;
        struct esp32s3_lo_plan plan;assert(esp32s3_plan_lo(settings.center_hz,ESP32S3_LO_AUTO,&plan));
        snapshot();assert(capture_configured(NULL,words+1,8192,&settings,&format)==RISC_RADIO_IQ_OK);
        assert_complete(words+1,8192,&format);assert_restored();
        assert(format.center_hz==plan.lo_hz && format.lo_mode==(unsigned)plan.mode);
        assert(format.rf_gain==settings.rf_gain && format.bb_gain==settings.bb_gain && format.iq_correction==settings.iq_correction);
        assert((active_gain>>24)==settings.gain_selector);
        assert(active_rf==settings.rf_gain && active_bb==(0x180u|settings.bb_gain) && active_filter==settings.filter);
        for(unsigned r=0;r<4;++r){assert(format.dc[r]==settings.dc[r]);assert(pbus_words[dc_block[r]][dc_index[r]]==r+11);}
    }
    /* Failed manual DC writes are treated as potentially committed, restored and retried. */
    snapshot();fail_pbus_command=pbus_commands+7; /* first manual DC write */
    assert(capture_configured(NULL,words+1,8192,&settings,&format)==RISC_RADIO_IQ_PBUS_FAILED);
    assert_empty(words+1,8192,&format);assert_restored();fail_pbus_command=0;
    for(unsigned r=0;r<4;++r)assert(pbus_words[dc_block[r]][dc_index[r]]==r+11);
    /* Failed native release clears returned data and retries only that release. */
    release_ok=false;snapshot();assert(capture_configured(NULL,words+1,8192,&settings,&format)==RISC_RADIO_IQ_CLEANUP_RETAINED);
    assert_empty(words+1,8192,&format);assert(lease && restored && !changed_dc);
    before=accesses;release_ok=true;assert(suspend_receiver(NULL));assert(accesses==before);assert_restored();
    snapshot();fail_pbus_command=pbus_commands+13; /* first manual DC restore */
    assert(capture_configured(NULL,words+1,8192,&settings,&format)==RISC_RADIO_IQ_CLEANUP_RETAINED);
    assert_empty(words+1,8192,&format);assert(lease && !restored && changed_dc==15u);
    fail_pbus_command=0;assert(suspend_receiver(NULL));assert_restored();
    for(unsigned r=0;r<4;++r)assert(pbus_words[dc_block[r]][dc_index[r]]==r+11);

    /* Progress polling tolerates startup delay and cycle-counter rollover. */
    cycles=0xffff0000u;delay_polls=200;settings=defaults;snapshot();
    assert(capture_configured(NULL,words+1,8192,NULL,&format)==RISC_RADIO_IQ_OK);
    assert_complete(words+1,8192,&format);assert_restored();delay_polls=0;
    for(unsigned fault=0;fault<7;++fault){
        dump_ok=fault!=0;delay_polls=fault==1?20000:0;pairs_per_poll=fault==2?RING_PAIRS+64:64;
        pipeline_tail=fault==3?END_GUARD_PAIRS:3;corrupt_pair=fault==4;pll_ok=fault!=5;pll_lock=fault!=6;
        snapshot();int status=capture_configured(NULL,words+1,8192,NULL,&format);
        assert(status==(fault>=5?RISC_RADIO_IQ_PLL_FAILED:RISC_RADIO_IQ_DUMP_TIMEOUT));
        assert_empty(words+1,8192,&format);assert_restored();
    }
    dump_ok=pll_ok=pll_lock=true;delay_polls=0;pairs_per_poll=64;pipeline_tail=3;corrupt_pair=false;
    settings.center_hz=2000000000u;ckgen_reject=true;snapshot();
    assert(capture_configured(NULL,words+1,8192,&settings,&format)==RISC_RADIO_IQ_PLL_FAILED);
    assert_empty(words+1,8192,&format);assert_restored();ckgen_reject=false;
    admission=false;before=accesses;
    assert(capture_configured(NULL,words+1,8192,NULL,&format)==RISC_RADIO_IQ_BUSY);
    assert_empty(words+1,8192,&format);assert(accesses==before);
    native_dirty_refusal=true;
    assert(capture_configured(NULL,words+1,8192,NULL,&format)==RISC_RADIO_IQ_CLEANUP_RETAINED);
    assert_empty(words+1,8192,&format);assert(accesses==before && lease && restored);
    assert(suspend_receiver(NULL) && accesses==before);native_dirty_refusal=false;admission=true;
    trace_count=0;assert(api.base.capture_configured_traced(NULL,words+1,8192,NULL,&format,trace,&trace_count)==RISC_RADIO_IQ_OK);
    assert(trace_count==13);assert_complete(words+1,8192,&format);
    assert(api.base.capture_configured_traced(NULL,words+1,8192,NULL,&format,reentrant_trace,NULL)==RISC_RADIO_IQ_OK);
    assert_complete(words+1,8192,&format);
    /* A configured call never changes the original capture entry point. */
    assert(capture_burst(NULL,words+1,257)==RISC_RADIO_IQ_BAD_ARGUMENT);
    assert(capture_burst(NULL,words+1,256)==RISC_RADIO_IQ_OK);
    assert((active_control&DUMP_CTRL_16MSPS)==0 && (active_gain>>24)==24);
    puts("IQ extension: full 8192-pair bursts, capability ABI, bounded controls, rollback, delayed/reset/wrapped cursors, no active SRAM access and failure clearing passed");
}
int main(void){
    bank_words=mmap(NULL,CAPTURE_BANK_BYTES,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    assert(bank_words!=MAP_FAILED);
    risc_radio_iq_resource_v1 guard={1,sizeof(guard),NULL,claim_resource,release_resource,CAPTURE_BANK_BASE,CAPTURE_BANK_BYTES};
    risc_provider_dependency_v1 deps[]={{RISC_RADIO_IQ_RESOURCE_CAPABILITY,1,&guard}};
    uint32_t pairs[258];for(unsigned i=0;i<258;++i)pairs[i]=0xdeadbeef;
    assert(!start(NULL,0));guard.bank_bytes=1024;assert(!start(deps,1));guard.bank_bytes=CAPTURE_BANK_BYTES;
    guard.api_version=2;assert(!start(deps,1));guard.api_version=1;
    guard.struct_size=12;assert(!start(deps,1));guard.struct_size=sizeof(guard);
    deps[0].capability_id="other.resource";assert(!start(deps,1));deps[0].capability_id=RISC_RADIO_IQ_RESOURCE_CAPABILITY;
    assert(!accesses && !claims && start(deps,1));assert(!start(deps,1));assert(!accesses && !claims);
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
        wrap=n==1;reset_on_stop=n!=2;pipeline_tail=n==2?0:3;*raw_register(DUMP_WRITE_INDEX_REG)=12345;
        assert(capture_burst(NULL,pairs+1,256)==RISC_RADIO_IQ_OK);
        assert(!native_lease && !lease && pairs[0]==0xdeadbeef && pairs[257]==0xdeadbeef);
        assert(pairs[1]==((dump_cursor-256)&RING_MASK));assert(pairs[256]==((dump_cursor-1)&RING_MASK));
        assert(*raw_register(RTC_CNTL_DIG_PWC_REG)==RTC_CNTL_WIFI_FORCE_PD);
        assert(*raw_register(RTC_CNTL_DIG_ISO_REG)==RTC_CNTL_WIFI_FORCE_ISO);
        assert(*raw_register(SYSTEM_WIFI_CLK_EN_REG)==0x10000000);
        assert(analog_regs[I2C_SDM][0]==0xab);
        risc_radio_iq_diagnostics_v1 detail={.struct_size=sizeof(detail)};
        unsigned before_reads=accesses;
        assert(diagnostics(NULL,&detail) && accesses==before_reads);
        assert(detail.stage==RISC_RADIO_IQ_STAGE_COMPLETE && detail.result==RISC_RADIO_IQ_OK);
        assert(detail.clock_mask==(0x10000000|WIFI_MAC_CLK_BIT6));
        assert(detail.dump_ready && detail.cleanup_ok && detail.requested_pairs==256);
        assert(detail.dump_before==12345 && detail.dump_after==dump_cursor);
        if(reset_on_stop)assert(*raw_register(DUMP_WRITE_INDEX_REG)==0);
    }
    pll_ok=false;assert(capture_burst(NULL,pairs+1,256)==RISC_RADIO_IQ_PLL_FAILED);assert(!native_lease);pll_ok=true;
    dump_ok=false;assert(capture_burst(NULL,pairs+1,256)==RISC_RADIO_IQ_DUMP_TIMEOUT);assert(!native_lease);dump_ok=true;
    assert(diagnostic_state.stage==RISC_RADIO_IQ_STAGE_DUMP && !diagnostic_state.dump_ready && diagnostic_state.cleanup_ok);
    assert(diagnostic_state.result==RISC_RADIO_IQ_DUMP_TIMEOUT);
    assert(diagnostic_state.elapsed_cycles>PBUS_TIMEOUT_CYCLES*100u);
    assert(!diagnostics(NULL,NULL));risc_radio_iq_diagnostics_v1 short_record={.struct_size=1};assert(!diagnostics(NULL,&short_record));
    pbus_ok=false;assert(capture_burst(NULL,pairs+1,256)==RISC_RADIO_IQ_CLEANUP_RETAINED);assert(native_lease && lease);
    assert(!quiesce());stop();assert(running && lease);pbus_ok=true;assert(suspend_receiver(NULL));assert(!lease);
    release_ok=false;assert(capture_burst(NULL,pairs+1,256)==RISC_RADIO_IQ_CLEANUP_RETAINED);assert(lease && restored);
    unsigned before=accesses;assert(!suspend_receiver(NULL));assert(accesses==before);
    release_ok=true;assert(suspend_receiver(NULL));assert(accesses==before && !lease);
    assert(capture_burst(NULL,pairs+1,256)==RISC_RADIO_IQ_OK);assert(quiesce());stop();assert(!running && !resource);
    assert(capture_burst(NULL,pairs+1,256)==RISC_RADIO_IQ_NOT_RUNNING);assert(start(deps,1));stop();
    assert(start(deps,1));trace_count=0;
    assert(capture_burst_traced(NULL,pairs+1,256,trace,&trace_count)==RISC_RADIO_IQ_OK);
    assert(trace_count==13);extension_regressions();stop();
    assert(tx_writes>=12);puts("IQ lifecycle: lazy admission, exclusive custody, bounded failures, retry, ring reset/wrap, restore, receive-only and restart passed");
}

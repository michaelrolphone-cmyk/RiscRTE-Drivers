#include "hardware_model.h"
#include "provider_log_mock.h"
#include "../Drivers/qmi8658_idle/driver.c"
static tw_hw_i2c_device_v1 selected;
static bool release_fail, claim_fail, claim_retained, claim_zero;
static unsigned io_nth, io_fault, clock_nth, clock_calls, sleep_nth, sleep_calls, sleep_fault;
static unsigned releases, claims;
static uint64_t before_sleep, stopped_at, oscillator_on_at;
static bool stop_pending, oscillator_started;
static bool claim_(void*c,uint8_t address,uint64_t*out) {
    ++claims; *out=0;
    if(claim_zero)return true;
    if(claim_fail&&!claim_retained)return false;
    bool ok=bus_claim_(c,address,out);return ok&&!claim_fail;
}
static bool release_(void*c,uint64_t token) {
    ++releases;if(release_fail)return false;return bus_release_(c,token);
}
static bool io_(void*c,uint64_t token,const uint8_t*tx,size_t n,uint8_t*rx,size_t rn,uint32_t ms) {
    const unsigned next=transactions_+1;
    if(next==io_nth&&io_fault==1){++transactions_;return false;}
    if(!rn){
        assert(n==2&&(tx[0]==2||tx[0]==8));
        uint8_t mask=tx[0]==2?1:3;
        assert((registers_[tx[0]]&~mask)==(tx[1]&~mask));
        if(tx[0]==8&&(registers_[8]&3)&&!(tx[1]&3)){stop_pending=true;stopped_at=tick_;}
        if(tx[0]==2&&((registers_[2]^tx[1])&1)){
            assert(!(registers_[8]&3));
            if(tx[1]&1){if(stop_pending)assert(tick_-stopped_at>=667);stop_pending=false;}
            else{oscillator_started=true;oscillator_on_at=tick_;}
        }
        if(tx[0]==8&&(tx[1]&3)){
            assert(!(registers_[2]&1));
            if(oscillator_started)assert(tick_-oscillator_on_at>=15);
        }
    }
    bool ok=transact_(c,token,tx,n,rx,rn,ms);
    if(next==io_nth&&io_fault==2)return false; /* Write may have acted. */
    if(next==io_nth&&io_fault==3&&writes_&&rn&&tx[0]==8)rx[0]^=1;
    if(next==io_nth&&io_fault==3&&writes_&&rn&&tx[0]==2)rx[0]^=1;
    return ok;
}
static uint64_t now_(void*c) {
    (void)c;++clock_calls;
    return clock_calls==clock_nth?UINT64_MAX:tick_;
}
static void delay_(void*c,uint32_t ms) {
    (void)c;assert(ms==667||ms==1||ms==15);assert(!ready(NULL));
    ++sleep_calls;before_sleep=tick_;
    if(sleep_calls==sleep_nth&&sleep_fault==1)return;
    if(sleep_calls==sleep_nth&&sleep_fault==2){tick_=tick_?tick_-1:0;return;}
    tick_+=ms;
}
static void clear_faults(void) {
    release_fail=claim_fail=claim_retained=claim_zero=false;
    io_nth=io_fault=clock_nth=sleep_nth=sleep_fault=0;
}
static void setup(uint8_t one,uint8_t seven,uint8_t accel,uint8_t gyro) {
    clear_faults();assert(quiesce());no_claims_();reset_model_();
    busy=false;stop_pending=oscillator_started=false;clock_calls=sleep_calls=releases=claims=0;tick_=100;
    memset(diagnostics_,0,sizeof(diagnostics_));
    bus_.claim_device=claim_;bus_.release_device=release_;bus_.transact=io_;
    clock_.monotonic_ms=now_;clock_.sleep_ms=delay_;
    hardware_.compatible="qst,qmi8658-idle";hardware_.config_type="peripheral.i2c";
    selected=(tw_hw_i2c_device_v1){.struct_size=sizeof(selected),.bus=i2c_config_,.address=0x6b,.chip_id=5,.irq=-1};
    hardware_.config=&selected;hardware_.config_size=sizeof(selected);
    registers_[0]=5;registers_[2]=one;registers_[3]=accel;registers_[4]=gyro;registers_[8]=seven;
}
static risc_provider_dependency_v1 *dependencies(void) {
    static risc_provider_dependency_v1 d[3];d[0]=deps_[0];d[1]=deps_[1];d[2]=deps_[3];return d;
}
static void restored(uint8_t one,uint8_t seven) {
    clear_faults();assert(quiesce());assert(!ready(NULL));
    if(registers_[2]!=one||registers_[8]!=seven)fprintf(stderr,"restore wanted=%02x/%02x got=%02x/%02x original=%02x/%02x\n",one,seven,registers_[2],registers_[8],original_one,original_seven);
    assert(registers_[2]==one&&registers_[8]==seven);no_claims_();
}
static void masks(void) {
    unsigned accepted=0,rejected=0;
    for(unsigned one=0;one<256;++one)for(unsigned seven=0;seven<256;++seven){
        setup(one,seven,0x16,0x16);
        bool idle=(one&1)&&!(seven&3);
        bool normal=!(one&3)&&!(seven&0xcc);
        bool ok=start(dependencies(),3);assert(ok==(idle||normal));
        if(ok){
            ++accepted;assert(ready(NULL)&&!(registers_[8]&3)&&(registers_[2]&1)&&!bus_token_);
            unsigned io=transactions_, sleeps=sleep_calls, closes=releases;
            assert(ready(NULL)&&ready(NULL));assert(io==transactions_&&sleeps==sleep_calls&&closes==releases);
            if(idle)assert(!writes_&&tick_==100);
            else assert(tick_==100+((seven&3)?667:0)+1);
            registers_[2]^=0x20;registers_[8]^=0x20; /* Unowned changes survive restoration. */
            restored(one^0x20,seven^0x20);
        }else{++rejected;assert(!writes_&&!ready(NULL));restored(one,seven);}
    }
    printf("QMI idle masks: %u admitted, %u pre-write refusals; exact existing power-down preserved PASS\n",accepted,rejected);
}
static void odrs(void) {
    for(unsigned en=0;en<4;++en)for(unsigned a=0;a<16;++a)for(unsigned g=0;g<16;++g){
        setup(0,en,a,g);
        bool expected=(!(en&1)||(a<=8&&((en&2)||a>=3)))&&(!(en&2)||g<=8);
        assert(start(dependencies(),3)==expected);
        if(expected)assert(ready(NULL));else assert(!writes_&&!ready(NULL));
        restored(0,en);
    }
    for(unsigned bit=0;bit<8;++bit){
        for(unsigned reg=0;reg<5;++reg){
            setup(0,1,0x16,0x16);
            const uint8_t addresses[]={3,4,9,10,20};registers_[addresses[reg]]|=(uint8_t)(1u<<bit);
            bool reject=(reg<2&&bit==7)||(reg==2&&bit<6)||(reg==3)||(reg==4&&(bit<2||bit>=4));
            bool ok=start(dependencies(),3);
            if(reject)assert(!ok&&!writes_&&!ready(NULL));
            clear_faults();assert(quiesce());
        }
    }
    for(unsigned a=0;a<8;++a)for(unsigned g=0;g<8;++g){
        setup(0,3,(a<<4)|6,(g<<4)|6);
        bool ok=start(dependencies(),3);assert(ok==(a<4&&g<7));restored(0,3);
    }
    /* Existing exact power-down preserves even unsupported arbitrary config. */
    for(unsigned value=0;value<256;++value){
        setup(0xff,0xfc,value,value);registers_[9]=registers_[10]=registers_[20]=value;
        assert(start(dependencies(),3)&&ready(NULL)&&!writes_&&tick_==100);
        assert(quiesce()&&!writes_&&registers_[9]==value&&registers_[10]==value&&registers_[20]==value);
    }
    puts("QMI idle ODR: all1024 enable/ODR combinations and unsupported feature controls PASS");
}
static void faults(void) {
    setup(0,3,0x16,0x16);assert(start(dependencies(),3));const unsigned steps=transactions_;restored(0,3);
    for(unsigned kind=1;kind<=3;++kind)for(unsigned n=1;n<=steps;++n){
        setup(0,3,0x16,0x16);io_nth=n;io_fault=kind;
        bool ok=start(dependencies(),3);
        if(!ok)assert(!ready(NULL));
        restored(0,3);
    }
    for(unsigned n=1;n<=4;++n){setup(0,3,0x16,0x16);clock_nth=n;assert(!start(dependencies(),3)&&!ready(NULL));restored(0,3);}
    for(unsigned n=1;n<=2;++n)for(unsigned kind=1;kind<=2;++kind){
        setup(0,3,0x16,0x16);sleep_nth=n;sleep_fault=kind;assert(!start(dependencies(),3)&&!ready(NULL));restored(0,3);
    }
    for(unsigned mode=0;mode<3;++mode){
        setup(0,3,0x16,0x16);claim_fail=mode<2;claim_retained=mode==1;claim_zero=mode==2;
        assert(!start(dependencies(),3)&&!ready(NULL)&&!transactions_);restored(0,3);
    }
    setup(0,3,0x16,0x16);release_fail=true;assert(!start(dependencies(),3)&&!ready(NULL)&&session);
    assert(!quiesce()&&session&&!ready(NULL));restored(0,3);
    /* Fault every unload transfer, including after a write acted. */
    setup(0,3,0x16,0x16);assert(start(dependencies(),3));transactions_=0;assert(quiesce());const unsigned cleanup_steps=transactions_;
    for(unsigned kind=1;kind<=2;++kind)for(unsigned n=1;n<=cleanup_steps;++n){
        setup(0,3,0x16,0x16);assert(start(dependencies(),3));transactions_=0;io_nth=n;io_fault=kind;
        assert(!quiesce()&&!ready(NULL)&&dirty);restored(0,3);
    }
    setup(0,3,0x16,0x16);assert(start(dependencies(),3));release_fail=true;assert(!quiesce()&&dirty&&!ready(NULL));restored(0,3);
    setup(0,3,0x16,0x16);assert(start(dependencies(),3));clock_nth=clock_calls+2;assert(!quiesce()&&dirty&&!ready(NULL));restored(0,3);
    puts("QMI idle faults: every start/unload I/O, acted-false writes, clocks, claims and retained releases PASS");
}
int main(void) {
    assert(!ready(NULL));assert(t5_driver_get(2)&&!t5_driver_get(1));
    masks();odrs();faults();
    setup(0,1,0x16,0x16);registers_[0]=0;assert(!start(dependencies(),3)&&!writes_);restored(0,1);
    setup(0,1,0x16,0x16);selected.address=0x34;assert(!start(dependencies(),3)&&!transactions_);assert(quiesce());
    puts("QMI8658 idle: only CTRL7[1:0]/CTRL1[0] writes, cached no-I/O guard, no reset/rail/CTRL9/ODR writes PASS");return 0;
}

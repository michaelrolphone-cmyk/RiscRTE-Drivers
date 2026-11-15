#include "hardware_model.h"
#include "../boards/waveshare-esp32-s3-touch-amoled-1.75c/supply/driver.c"

static risc_hw_i2c_function_v1 selected;
static risc_hardware_device_v1 device = {
    .api_version=1, .struct_size=sizeof(device), .instance_id=13,
    .compatible="x-powers,axp2101-dcdc1", .revision="unspecified",
    .config_type="peripheral.i2c-function", .config_version=1,
    .config_size=sizeof(selected), .config=&selected
};
static const risc_provider_dependency_v1 supply_deps[] = {
    {"hardware.device",1,&device}, {"i2c.bus",1,&bus_}
};
static bool fail_release, fail_claim, retain_claim, zero_claim;
static unsigned claims, releases;
static bool claim(void *c,uint8_t address,uint64_t *out) {
    ++claims;
    if (zero_claim) { *out=0; return true; }
    if (fail_claim && !retain_claim) { *out=0; return false; }
    bool ok=bus_claim_(c,address,out);
    return ok && !fail_claim;
}
static bool release(void *c,uint64_t token) {
    ++releases;
    return !fail_release && bus_release_(c,token);
}
static bool transact(void *c,uint64_t token,const uint8_t *tx,size_t n,
                     uint8_t *rx,size_t rn,uint32_t ms) {
    assert(n==1 && rn==1 && rx && ms==20);
    assert(tx[0]==3 || tx[0]==0x80 || tx[0]==0x82);
    return transact_(c,token,tx,n,rx,rn,ms);
}
static void setup(void) {
    (void)deps_;
    fail_release=fail_claim=retain_claim=zero_claim=false;
    stop(); no_claims_(); reset_model_(); claims=releases=0;
    bus_.claim_device=claim; bus_.transact=transact; bus_.release_device=release;
    selected=(risc_hw_i2c_function_v1){.struct_size=sizeof(selected), .bus=i2c_config_,
        .parent_instance_id=4,.function_id=1,.address=0x34,.chip_id=0x4a};
    registers_[3]=0x4a; registers_[0x80]=0xa5; registers_[0x82]=0xf2;
}
static void unavailable(void) {
    risc_power_rail_info_v1 info={.struct_size=sizeof(info),.configuration_verified=true,
                                .configured_millivolts=3300,.enabled=true};
    assert(!get_info(NULL,&info));
    assert(!info.configuration_verified && !info.configured_millivolts && !info.enabled);
}
int main(void) {
    setup(); uint8_t before[256]; memcpy(before,registers_,sizeof(before));
    assert(start(supply_deps,2) && !bus_token_);
    risc_power_rail_info_v1 info={.struct_size=sizeof(info)};
    assert(get_info(NULL,&info) && info.configuration_verified && info.enabled &&
           info.configured_millivolts==3300);
    unsigned n=transactions_;
    assert(!set_enabled(NULL,true) && !set_enabled(NULL,false) && transactions_==n);
    assert(!memcmp(before,registers_,sizeof(before)) && !writes_);
    stop(); no_claims_();
    for(unsigned code=0;code<32;++code) for(unsigned enabled=0;enabled<2;++enabled) {
        setup(); registers_[0x82]=0xe0|code; registers_[0x80]=0xfe|enabled;
        bool ok=start(supply_deps,2);
        assert(ok==(code<=19));
        if(ok) {
            assert(get_info(NULL,&info) && info.configuration_verified &&
                   info.enabled==(enabled!=0) && info.configured_millivolts==1500+100*code);
        } else unavailable();
        assert(!writes_); stop(); no_claims_();
    }
    for(unsigned failure=1;failure<=3;++failure) {
        setup(); fail_transaction_=failure; assert(!start(supply_deps,2));
        unavailable(); assert(!bus_token_&&!writes_); stop(); no_claims_();
        setup(); assert(start(supply_deps,2)); fail_transaction_=transactions_+failure;
        unavailable(); assert(!bus_token_&&!writes_); stop(); no_claims_();
    }
    setup(); registers_[3]=0; assert(!start(supply_deps,2));
    assert(transactions_==1 && !writes_); stop();
    for(unsigned retained=0;retained<2;++retained) {
        setup(); fail_claim=true; retain_claim=retained;
        assert(!start(supply_deps,2) && !transactions_ && !!session==!!retained);
        unavailable(); stop(); no_claims_();
    }
    setup(); zero_claim=true; assert(!start(supply_deps,2)&&!transactions_&&!session); stop();
    setup(); fail_claim=retain_claim=fail_release=true;
    assert(!start(supply_deps,2) && session && !transactions_);
    assert(!quiesce()); stop(); assert(session && bus && !start(supply_deps,2));
    fail_release=false; assert(quiesce()); stop(); no_claims_();
    setup(); fail_release=true; assert(!start(supply_deps,2) && session);
    unavailable(); assert(!quiesce()); stop(); assert(session && bus);
    fail_release=false; assert(quiesce()); stop(); no_claims_();
    setup(); assert(start(supply_deps,2)); fail_release=true; unavailable();
    unsigned reads=transactions_; unavailable(); assert(reads==transactions_ && session);
    fail_release=false; assert(quiesce()); unavailable(); stop(); no_claims_();
    for(unsigned wrong=0;wrong<5;++wrong) {
        setup();
        switch(wrong) {case 0:selected.function_id=2;break;case 1:selected.address=0x35;break;
        case 2:selected.chip_id=0x49;break;case 3:selected.parent_instance_id=0;break;
        case 4:selected.bus.sda=selected.bus.scl;break;}
        assert(!start(supply_deps,2) && !claims && !transactions_);
    }
    setup(); assert(!start(supply_deps+1,1) && !claims);
    assert(t5_driver_get(2)==&descriptor && !t5_driver_get(1));
    puts("Badge DCDC1: all voltage codes/enables, read-only callbacks, failed acquisition/read/release custody and profile rejection PASS");
}

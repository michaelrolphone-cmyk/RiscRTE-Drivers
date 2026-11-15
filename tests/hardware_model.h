#pragma once
#include "DriverSupport.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t registers_[256];
static uint64_t tick_, bus_token_;
static bool pins_[49], pin_levels_[49];
static unsigned transactions_, fail_transaction_, writes_, fail_gpio_, gpio_calls_;
static uint8_t write_registers_[512];
static bool bus_claim_(void *c,uint8_t address,uint64_t *out) {
    (void)c;assert(address>=8 && address<120);*out=0;
    if(bus_token_)return false;bus_token_=123;*out=bus_token_;return true;
}
static bool transact_(void *c,uint64_t claim,const uint8_t *tx,size_t n,uint8_t *rx,size_t rn,uint32_t ms) {
    (void)c;assert(claim==bus_token_ && claim && tx && n && ms && (size_t)tx[0]+(rn?rn:n-1)<=256);
    ++transactions_;if(transactions_==fail_transaction_)return false;
    if(rn){assert(n==1 && rx);memcpy(rx,registers_+tx[0],rn);}
    else {assert(writes_<512);write_registers_[writes_++]=tx[0];memcpy(registers_+tx[0],tx+1,n-1);}
    return true;
}
static bool bus_release_(void *c,uint64_t claim) {(void)c;assert(claim && claim==bus_token_);bus_token_=0;return true;}
static bool gpio_claim_(void *c,uint8_t pin,uint32_t flags,uint64_t *out) {
    (void)c;assert(pin<=48 && (flags&(RISC_GPIO_INPUT|RISC_GPIO_OUTPUT)));*out=0;
    if(++gpio_calls_==fail_gpio_ || pins_[pin])return false;
    pins_[pin]=true;*out=pin+1;return true;
}
static bool gpio_write_(void *c,uint64_t token,bool value) {(void)c;assert(token && token<=49 && pins_[token-1]);pin_levels_[token-1]=value;return true;}
static bool gpio_read_(void *c,uint64_t token,bool *value) {(void)c;assert(token && token<=49 && pins_[token-1]);*value=pin_levels_[token-1];return true;}
static bool gpio_release_(void *c,uint64_t token) {(void)c;assert(token && token<=49 && pins_[token-1]);pins_[token-1]=false;return true;}
static uint64_t millis_(void *c) {(void)c;return tick_;}
static void sleep_(void *c,uint32_t ms) {(void)c;tick_+=ms;}
static risc_i2c_bus_api_v1 bus_={1,sizeof(bus_),NULL,bus_claim_,transact_,bus_release_};
static risc_gpio_bank_api_v1 gpio_={.api_version=1,.struct_size=sizeof(gpio_),.claim=gpio_claim_,.read=gpio_read_,.write=gpio_write_,.release=gpio_release_};
static risc_platform_clock_api_v1 clock_={1,sizeof(clock_),NULL,millis_,sleep_};
static risc_hw_bus_v1 i2c_config_={.struct_size=sizeof(i2c_config_),.kind=2,.instance_id=101,.controller=0,.frequency_hz=400000,.sda=15,.scl=14,.sclk=-1,.mosi=-1,.miso=-1};
static risc_hardware_device_v1 hardware_={.api_version=1,.struct_size=sizeof(hardware_),.instance_id=4,.revision="unspecified",.config_version=1};
static risc_provider_dependency_v1 deps_[]={ {"hardware.device",1,&hardware_},{"i2c.bus",1,&bus_},{"gpio.bank",1,&gpio_},{"platform.clock",1,&clock_} };
static void no_claims_(void) {assert(!bus_token_);for(unsigned i=0;i<49;i++)assert(!pins_[i]);}
static void reset_model_(void) {no_claims_();memset(registers_,0,sizeof(registers_));tick_=0;transactions_=fail_transaction_=writes_=fail_gpio_=gpio_calls_=0;}

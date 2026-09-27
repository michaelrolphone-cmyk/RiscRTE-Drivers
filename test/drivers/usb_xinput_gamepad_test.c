#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../Drivers/usb_xinput_gamepad/driver.c"

static uint8_t config[] = {
    9,2,32,0,1,1,0,0x80,50,
    9,4,2,0,1,0xff,0x5d,1,0,
    7,5,0x83,3,32,0,4
};
static uint64_t device=51;
static uint8_t packet[64];
static int32_t packet_length;
static uint64_t now_ms;
static unsigned claims,releases,configs,reads;

static uint64_t clock_ms(void *ctx){(void)ctx;return now_ms;}
static bool host_poll(void *ctx,size_t budget,size_t *processed){
    (void)ctx; assert(budget==8); *processed=0; return true;
}
static bool devices(void *ctx,uint64_t *out,size_t *count){
    (void)ctx; assert(*count>=1); *count=device?1:0; if(device) out[0]=device; return true;
}
static bool configuration(void *ctx,uint64_t d,uint8_t *out,size_t *count,uint16_t *vid,uint16_t *pid){
    (void)ctx; assert(d==device && *count>=sizeof(config)); ++configs;
    memcpy(out,config,sizeof(config)); *count=sizeof(config); *vid=0x1234; *pid=0x9876; return true;
}
static bool claim(void *ctx,uint64_t d,uint8_t iface,uint8_t alt,uint64_t *out){
    (void)ctx; assert(d==device && iface==2 && alt==0); ++claims; *out=d+1000; return true;
}
static void release_claim(void *ctx,uint64_t c){(void)ctx; assert(c>1000); ++releases;}
static int32_t interrupt_read(void *ctx,uint64_t c,uint8_t ep,uint8_t *out,size_t cap,uint32_t timeout){
    (void)ctx; assert(c==device+1000 && ep==0x83 && cap==64 && timeout==10); ++reads;
    int32_t n=packet_length; packet_length=0;
    if(n>0) memcpy(out,packet,(size_t)n);
    return n;
}
static risc_platform_clock_api_v1 fake_clock={
    .api_version=1,.struct_size=sizeof(risc_platform_clock_api_v1),.monotonic_ms=clock_ms
};
static risc_usb_host_interrupt_v1 fake_host={
    .discovery={
        .host={.api_version=1,.struct_size=sizeof(risc_usb_host_interrupt_v1),
               .configuration=configuration,.claim=claim,.release=release_claim},
        .poll=host_poll,.devices=devices
    },
    .interrupt_read=interrupt_read
};
static risc_provider_dependency_v1 deps[]={
    {"platform.clock",1,&fake_clock},{"usb.host",1,&fake_host}
};

static void wired(uint8_t dpad,uint8_t buttons){
    memset(packet,0,sizeof(packet)); packet[1]=20; packet[2]=dpad; packet[3]=buttons;
    packet_length=20;
}
int main(void){
    assert(!t5_driver_get(1));
    const risc_driver_v2 *driver=t5_driver_get(2);
    assert(driver && !strcmp(driver->capability_id,"usb.xinput.gamepad"));
    const risc_usb_gamepad_api_v1 *input=driver->capability;
    assert(driver->start(deps,2));
    assert(!driver->start(deps,2));
    uint64_t sub=input->subscribe(0,0); assert(sub);
    assert(input->poll(0,4) && claims==1 && configs==1 && reads==1);

    wired(0x09,0x10); /* north+right, Xbox A */
    packet[4]=255; packet[5]=128;
    packet[6]=0; packet[7]=0x80;
    packet[8]=0; packet[9]=0x80;
    packet[10]=0xff; packet[11]=0x7f;
    packet[12]=0xff; packet[13]=0x7f;
    assert(input->poll(0,4));
    risc_usb_gamepad_event_v1 ev;
    assert(input->next(0,sub,&ev)==1 && ev.kind==1);
    assert(ev.state.connected && ev.state.device==device);
    assert(ev.state.buttons==(2u|(1u<<6)|(1u<<7)));
    assert(ev.state.hat==1 && ev.state.x==-32768 && ev.state.y==32767);
    assert(ev.state.rx==32767 && ev.state.ry==-32768);
    assert(ev.state.z==32767 && ev.state.rz==128);

    packet_length=20;
    assert(input->poll(0,4) && input->next(0,sub,&ev)==0);

    size_t count=1; risc_usb_gamepad_state_v1 state;
    assert(input->snapshot(0,&state,&count) && count==1 && state.buttons==ev.state.buttons);

    device=0;
    assert(input->poll(0,4) && releases==1);
    assert(input->next(0,sub,&ev)==1 && ev.kind==2 && !ev.state.connected && ev.state.hat==8);

    assert(!driver->quiesce());
    assert(input->unsubscribe(0,sub));
    assert(driver->quiesce());
    driver->stop();

    device=52; config[15]=0x81; /* wireless protocol */
    assert(driver->start(deps,2));
    sub=input->subscribe(0,0); assert(sub);
    assert(input->poll(0,4) && claims==2);
    packet[0]=8; packet[1]=0x80; packet_length=2;
    assert(input->poll(0,4) && input->next(0,sub,&ev)==1 && ev.kind==1);
    packet[0]=8; packet[1]=0; packet_length=2;
    assert(input->poll(0,4) && input->next(0,sub,&ev)==1 && ev.kind==2);
    assert(input->unsubscribe(0,sub));
    assert(driver->quiesce() && releases==2);
    driver->stop();

    puts("XInput descriptor binding, wired mapping, wireless presence, hotplug and shutdown: PASS");
}

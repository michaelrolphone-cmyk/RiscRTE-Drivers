#include "RiscUsbHidV1.h"
#include "RiscUsbGamepadDiagnosticsV1.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../../Drivers/usb_hid_gamepad/driver.c"
static const uint8_t descriptor[]={0x05,0x01,0x09,0x05,0xa1,0x01,0x05,0x09,0x19,0x01,0x29,0x04,0x15,0x00,0x25,0x01,0x75,0x01,0x95,0x04,0x81,0x02,0x75,0x01,0x95,0x04,0x81,0x03,0x05,0x01,0x09,0x30,0x09,0x31,0x15,0x81,0x25,0x7f,0x75,0x08,0x95,0x02,0x81,0x02,0xc0};
static risc_usb_hid_interface_v1 iface={.device=0x3344,.interface_number=3,.alternate=0,.subclass=0,.protocol=0,.interrupt_in=0x81,.max_packet=8};
static uint8_t reports[8][3]; static size_t report_count,report_index; static bool present_state=true,expose_iface=true; static unsigned opens,closes,scans,descriptor_reads;
static bool scan_mock(void*c,size_t m){(void)c;assert(m==16);++scans;return true;}
static bool interfaces_mock(void*c,risc_usb_hid_interface_v1*out,size_t*n){(void)c;assert(n);size_t need=expose_iface?1:0;if(*n<need){*n=need;return false;}if(need)out[0]=iface;*n=need;return true;}
static uint64_t open_mock(void*c,uint64_t d,uint8_t i,uint8_t a){(void)c;assert(d==iface.device&&i==3&&a==0);++opens;return 0x7788;}
static bool desc_mock(void*c,uint64_t s,uint8_t*out,size_t*n){(void)c;assert(s==0x7788&&out&&n);if(*n<sizeof(descriptor)){*n=sizeof(descriptor);return false;}memcpy(out,descriptor,sizeof(descriptor));*n=sizeof(descriptor);++descriptor_reads;return true;}
static bool boot_mock(void*c,uint64_t s,bool b){(void)c;(void)s;(void)b;return false;}
static int32_t read_mock(void*c,uint64_t s,uint8_t*out,size_t cap,uint32_t ms){(void)c;assert(s==0x7788&&cap>=3&&ms==10);if(report_index>=report_count)return 0;memcpy(out,reports[report_index++],3);return 3;}
static bool present_mock(void*c,uint64_t s){(void)c;assert(s==0x7788);return present_state;}
static bool close_mock(void*c,uint64_t s){(void)c;assert(s==0x7788);++closes;return true;}
static risc_usb_hid_api_v1 mock_hid={RISC_USB_HID_API_V1,sizeof(risc_usb_hid_api_v1),0,scan_mock,interfaces_mock,open_mock,desc_mock,boot_mock,read_mock,present_mock,close_mock};
static void queue(uint8_t b,int8_t x,int8_t y){assert(report_count<8);reports[report_count][0]=b;reports[report_count][1]=(uint8_t)x;reports[report_count][2]=(uint8_t)y;++report_count;}
int main(void){
 risc_provider_dependency_v1 dep={"usb.hid",RISC_USB_HID_API_V1,&mock_hid};assert(start(&dep,1));assert(!start(&dep,1));
 const risc_usb_gamepad_diagnostics_v1*diag=&api;const risc_usb_gamepad_api_v1*pad=&diag->base;
 assert(pad->api_version==1&&pad->struct_size==sizeof(risc_usb_gamepad_diagnostics_v1));
 uint64_t sub=pad->subscribe(0,0);assert(sub);queue(1,-127,127);assert(pad->poll(0,4));assert(scans&&opens==1&&descriptor_reads==1);
 risc_usb_gamepad_event_v1 ev={0};assert(pad->next(0,sub,&ev)==1);assert(ev.kind==1&&ev.state.connected&&ev.state.device==iface.device);assert(ev.state.buttons==1&&ev.state.x==-32767&&ev.state.y==32767&&ev.state.hat==8);
 risc_usb_gamepad_state_v1 snap[1];size_t n=0;assert(!pad->snapshot(0,0,&n)&&n==1);n=1;assert(pad->snapshot(0,snap,&n)&&n==1);assert(snap[0].buttons==1&&snap[0].x==-32767&&snap[0].y==32767);
 char msg[64]={0};assert(diag->diagnostic(0,msg,sizeof(msg)));assert(strcmp(msg,"GAMEPAD INPUT LAYOUT CONNECTED")==0);
 queue(2,0,0);assert(pad->poll(0,2));assert(pad->next(0,sub,&ev)==1&&ev.kind==3);assert(ev.state.buttons==2&&ev.state.x==0&&ev.state.y==0);
 assert(!quiesce());present_state=false;expose_iface=false;assert(pad->poll(0,1));assert(closes==1);assert(pad->next(0,sub,&ev)==1&&ev.kind==2&&!ev.state.connected);assert(pad->unsubscribe(0,sub));assert(quiesce());stop();assert(!hid);
 puts("USB HID gamepad descriptor parsing, normalization, mailbox, diagnostics and quiescence: PASS");return 0;
}

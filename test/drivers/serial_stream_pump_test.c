/* Shared implementation test, not firmware queue or USB hardware execution. */
#include "RiscUsbControllerV1.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
typedef struct {
 uint64_t token,device;uint32_t rx,tx,rx_size,rx_offset,tx_size,tx_offset;bool failed;
} serial_session;
static serial_session sessions[RISC_USB_CDC_MAX_SESSIONS];
static void *host;
#include "../../Drivers/common/SerialStreamPump.inc"
static int read_calls,write_calls,produces,consumes,finishes,closes;
static bool over_accept;
static char received[8],sent[8];static size_t received_n,sent_n;
static serial_session *lookup(uint64_t t){return t==sessions[0].token?&sessions[0]:NULL;}
static int32_t read_data(uint64_t t,uint8_t *p,size_t n,uint32_t ms){assert(t==1&&n==512&&ms==1);++read_calls;if(read_calls==1){memcpy(p,"OK",2);return 2;}return 0;}
static int32_t write_data(uint64_t t,const uint8_t *p,size_t n,uint32_t ms){assert(t==1&&n&&ms==1);++write_calls;sent[sent_n++]=(char)*p;return 1;}
static int32_t publish(uint64_t c,const risc_stream_endpoint_v1 *e,uint32_t *out){assert(c==9&&e->byte_capacity==2048);*out=e->rights;return 0;}
static int32_t produce(uint64_t c,uint32_t ep,const void *p,uint32_t n,uint32_t *accepted){assert(c==9&&ep==1);++produces;if(over_accept){*accepted=n+1;return 0;}if(produces==1){*accepted=0;return 1;}*accepted=1;received[received_n++]=*(const char*)p;return 0;}
static int32_t consume(uint64_t c,uint32_t ep,void *p,uint32_t n,uint32_t *out){assert(c==9&&ep==2&&n==512);++consumes;if(consumes==1){memcpy(p,"HI",2);*out=2;return 0;}*out=0;return 2;}
static int32_t finish(uint64_t c,uint32_t ep,int32_t e){assert(c==9&&(ep==1||ep==2)&&e<0);++finishes;return 0;}
static int32_t close_endpoint(uint64_t c,uint32_t ep){assert(c==9&&(ep==1||ep==2));++closes;return 0;}
int main(void){
 risc_stream_provider_v1 api={1,sizeof(api),9,publish,produce,consume,NULL,NULL,finish,close_endpoint};
 assert(bind_streams(&api));host=&api;sessions[0].token=1;sessions[0].device=5;
 uint32_t rx,tx;assert(endpoints(1,&rx,&tx)&&rx==1&&tx==2);
 uint8_t data[2];assert(legacy_read(1,data,2,1)<0&&legacy_write(1,data,2,1)<0);
 poll_streams(1);assert(!read_calls&&!write_calls);
 poll_streams(2);assert(read_calls==1&&write_calls==1&&received_n==0&&sent_n==1);
 assert(sessions[0].rx_size==2&&sessions[0].rx_offset==0);
 poll_streams(2);assert(read_calls==1&&write_calls==2&&received_n==1&&sent_n==2);
 poll_streams(2);assert(received_n==2&&!memcmp(received,"OK",2)&&!memcmp(sent,"HI",2));
 assert(finishes==0); /* queue EOF is not physical wire-flush success */
 sessions[0].rx_size=1;sessions[0].rx_offset=0;over_accept=true;
 poll_streams(2);assert(sessions[0].failed&&finishes==2);
 int before=read_calls;poll_streams(2);assert(read_calls==before);
 close_endpoints(&sessions[0]);assert(closes==2&&!sessions[0].rx&&!sessions[0].tx);
 assert(legacy_read(1,data,2,1)<0); /* failed endpoint state cannot bypass */
 uint64_t absent=99;reconcile_endpoints(&absent,1);
 puts("Shared pump: bounded turns/backpressure/order/invalid counts/EOF distinction PASS");
}

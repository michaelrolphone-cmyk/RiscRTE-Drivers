#include "RiscUsbControllerV1.h"
#include "RiscSerialPortV1.h"
#include "RiscSerialStreamSessionV1.h"
#include "RiscStreamSessionProviderV1.h"
#include "RiscPlatformClockV1.h"
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const uint8_t descriptor[] = {
  9,2,41,0,2,1,0,0x80,50, 9,4,0,0,0,2,2,1,0,
  9,4,1,0,2,10,0,0,0, 7,5,0x81,2,64,0,0, 7,5,0x02,2,64,0,0
};
static uint64_t now, device=17;
static uint32_t list_cost, config_cost, claim_cost, control_cost, queue_cost, release_cost, read_cost, write_cost;
static unsigned polls, claims, releases, controls, reads, writes, closes, finishes, publishes;
static bool claim_live[2], release_fail[2], claim_fail[2], claim_dirty, config_fail, config_malformed, inventory_fail, bad_list, absent, clock_fail;
static int control_result=7, publish_fail, finish_fail, close_fail;
static unsigned rx_chunk=2, tx_chunk=2, consume_chunk=512, produce_chunk=512;
static uint32_t control_budget, read_budget, write_budget;
static unsigned wire_size;
static uint8_t wire[1024];
static struct { uint8_t data[2048]; unsigned size, read, rights; bool live, finished; } q[32];
static uint32_t next_q;
static uint64_t tick(void *c) {(void)c;return clock_fail?UINT64_MAX:now;}
static bool f_poll(void*c,size_t n,size_t*p){(void)c;(void)n;(void)p;++polls;assert(!"daemon discovery must not run");return false;}
static bool f_devices(void*c,uint64_t*out,size_t*n){(void)c;now+=list_cost;if(inventory_fail)return false;if(bad_list){out[0]=out[1]=device;*n=2;return true;}if(absent){*n=0;return true;}assert(*n>=1);out[0]=device;*n=1;return true;}
static bool f_config(void*c,uint64_t d,uint8_t*out,size_t*n,uint16_t*v,uint16_t*p){(void)c;assert(d==device);now+=config_cost;if(config_fail)return false;assert(*n>=sizeof(descriptor));memcpy(out,descriptor,sizeof(descriptor));if(config_malformed)out[18]=1;*n=sizeof(descriptor);*v=1;*p=2;return true;}
static bool f_claim(void*c,uint64_t d,uint8_t i,uint8_t a,uint64_t*out){(void)c;assert(d==device&&i<2&&!a);now+=claim_cost;++claims;if(claim_fail[i]&&!claim_dirty)return false;assert(!claim_live[i]);claim_live[i]=true;*out=100+i;return !claim_fail[i];}
static bool f_release(void*c,uint64_t h){(void)c;assert(h>=100&&h<=101);unsigned i=(unsigned)(h-100);assert(claim_live[i]);now+=release_cost;++releases;if(release_fail[i])return false;claim_live[i]=false;return true;}
static void f_legacy_release(void*c,uint64_t h){(void)f_release(c,h);}
static int32_t f_control(void*c,uint64_t h,uint8_t t,uint8_t r,uint16_t v,uint16_t i,uint8_t*p,uint16_t n,uint32_t ms){(void)c;(void)v;assert(h==100&&claim_live[0]&&t==0x21&&i==0&&ms&&ms<=1000);assert(r==0x20||r==0x22);assert((n==7&&p)||(n==0&&!p));++controls;control_budget=ms;assert(control_cost<=ms);now+=control_cost;if(control_result<0)return control_result;return n;}
static int32_t f_read(void*c,uint64_t h,uint8_t e,uint8_t*d,size_t n,uint32_t ms){(void)c;assert(h==101&&claim_live[1]&&e==0x81&&ms);++reads;read_budget=ms;assert(read_cost<=ms);now+=read_cost;unsigned k=rx_chunk;if(k>n)k=(unsigned)n;for(unsigned i=0;i<k;i++)d[i]=(uint8_t)('A'+i);return (int32_t)k;}
static int32_t f_write(void*c,uint64_t h,uint8_t e,const uint8_t*d,size_t n,uint32_t ms){(void)c;assert(h==101&&claim_live[1]&&e==2&&ms);++writes;write_budget=ms;assert(write_cost<=ms);now+=write_cost;unsigned k=tx_chunk;if(k>n)k=(unsigned)n;assert(wire_size+k<=sizeof(wire));memcpy(wire+wire_size,d,k);wire_size+=k;return (int32_t)k;}
static int32_t f_publish(uint64_t c,const risc_stream_endpoint_v1*e,uint32_t*out){assert(c==9&&e&&e->byte_capacity==2048);now+=queue_cost;++publishes;if((int)publishes==publish_fail)return RISC_STREAM_LIMIT;*out=++next_q;assert(next_q<32);q[next_q].live=true;q[next_q].rights=e->rights;return 0;}
static int32_t f_produce(uint64_t c,uint32_t h,const void*d,uint32_t n,uint32_t*out){assert(c==9&&h&&q[h].live&&!q[h].finished&&q[h].rights==1);now+=queue_cost;unsigned k=n;if(k>produce_chunk)k=produce_chunk;if(k>sizeof(q[h].data)-q[h].size)k=sizeof(q[h].data)-q[h].size;memcpy(q[h].data+q[h].size,d,k);q[h].size+=k;*out=k;return k?0:1;}
static int32_t f_consume(uint64_t c,uint32_t h,void*d,uint32_t n,uint32_t*out){assert(c==9&&h&&q[h].live&&!q[h].finished&&q[h].rights==2);now+=queue_cost;unsigned k=q[h].size-q[h].read;if(k>n)k=n;if(k>consume_chunk)k=consume_chunk;memcpy(d,q[h].data+q[h].read,k);q[h].read+=k;*out=k;return k?0:1;}
static int32_t f_finish(uint64_t c,uint32_t h,int32_t reason){(void)reason;assert(c==9&&h&&q[h].live);now+=queue_cost;++finishes;if((int)finishes==finish_fail)return -5;q[h].finished=true;return 0;}
static int32_t f_close(uint64_t c,uint32_t h){assert(c==9&&h&&q[h].live);assert(!claim_live[0]&&!claim_live[1]);now+=queue_cost;++closes;if((int)closes==close_fail)return -5;q[h].live=false;return 0;}
static risc_usb_host_discovery_v1 fake_host={{1,sizeof(fake_host),0,f_config,f_claim,f_legacy_release,0,f_read,f_write},f_poll,f_devices,0,0,f_release,f_control};
static risc_platform_clock_api_v1 fake_clock={1,sizeof(fake_clock),0,tick,0};
static risc_stream_provider_v1 fake_streams={1,sizeof(fake_streams),9,f_publish,f_produce,f_consume,0,0,f_finish,f_close};
#ifdef USE_REAL_HOST
static const risc_driver_v2 *real_host_driver;
static const risc_usb_host_discovery_v1 *real_host;
static unsigned event_kind = 1;
static int32_t f_event(void*c,risc_usb_controller_event_v1*out){(void)c;if(!event_kind)return 0;*out=(risc_usb_controller_event_v1){event_kind,device};event_kind=0;return 1;}
static int32_t f_physical_control(void*c,uint64_t h,uint8_t t,uint8_t r,uint16_t v,uint16_t i,uint8_t*p,uint16_t n,uint32_t ms){assert(h==device);return f_control(c,100,t,r,v,i,p,n,ms);}
static bool f_quiesce(void*c){(void)c;return !claim_live[0]&&!claim_live[1];}
static risc_usb_controller_api_v1 fake_controller={1,sizeof(fake_controller),0,f_event,f_config,f_claim,f_release,f_physical_control,f_read,f_write,f_quiesce};
#endif
#ifdef LEGACY_CLIENT
extern void legacy_client(const risc_driver_v2*);
#endif
static const risc_driver_stream_sessions_v2 *d;
static const risc_stream_session_provider_v1 *a;
static risc_serial_stream_open_v1 request={1,sizeof(request),17,17,{115200,8,0,1,0}};
static risc_provider_stream_session_v1 opened;
static int32_t open_ms(uint32_t ms){opened=(risc_provider_stream_session_v1){sizeof(opened),99,99,99,99};return a->open(&request,sizeof(request),ms,&opened);}
static int32_t call(unsigned op,uint32_t ms){risc_serial_stream_call_v1 r={1,sizeof(r),op,0,{{0}}};if(op==1)r.value.config=request.config;uint32_t n=99;int32_t rc=a->call(opened.session,&r,sizeof(r),ms,0,0,&n);assert(!n);return rc;}
static void clean(void){assert(a->close(opened.session,1000)==0);assert(d->poll.streams.driver.quiesce());}
int main(int argc,char**argv){
 assert(argc==3 || argc==4);void*lib=dlopen(argv[1],RTLD_NOW);if(!lib){puts(dlerror());return 2;}
 risc_driver_get_v2_fn get=(risc_driver_get_v2_fn)dlsym(lib,"t5_driver_get");assert(get&&!get(1));
 d=(const risc_driver_stream_sessions_v2*)get(2);assert(d&&d->poll.streams.driver.struct_size==sizeof(*d));
 assert(d->extension_tag==RISC_DRIVER_STREAM_SESSIONS_TAG_V1&&d->extension_version==1);a=d->stream_sessions;
 assert(d->poll.streams.bind_streams(&fake_streams));
 risc_provider_dependency_v1 deps[]={{"platform.clock",1,&fake_clock},{"usb.host",1,&fake_host}};
 #ifdef USE_REAL_HOST
 assert(argc==4);void*hl=dlopen(argv[3],RTLD_NOW);assert(hl);
 risc_driver_get_v2_fn hg=(risc_driver_get_v2_fn)dlsym(hl,"t5_driver_get");assert(hg);real_host_driver=hg(2);
 risc_provider_dependency_v1 hd={"usb.controller",1,&fake_controller};assert(real_host_driver->start(&hd,1));
 real_host=real_host_driver->capability;size_t processed=0;assert(real_host->poll(0,16,&processed));
 uint64_t list[8];size_t n=8;assert(real_host->devices(0,list,&n)&&n==1);
 request.provider_device=request.device_generation=list[0];deps[1].api=real_host;
#endif
 assert(d->poll.streams.driver.start(deps,2));
 const char*mode=argv[2];
 #ifdef USE_REAL_HOST
 if(!strcmp(mode,"real-retained")){
  assert(open_ms(1000)==0);release_fail[0]=true;
  assert(a->close(opened.session,10)==RISC_STREAM_RETAINED&&!closes&&!claim_live[1]);
  assert(!real_host_driver->quiesce()&&!closes);
  release_fail[0]=false;
  /* Graph cleanup may finish physical cleanup but must retain its receipt. */
  assert(!real_host_driver->quiesce()&&!closes&&!claim_live[1]&&!claim_live[0]);
  clean();assert(real_host_driver->quiesce());
 }else if(!strcmp(mode,"real-dirty-claim")){
  claim_fail[1]=claim_dirty=release_fail[1]=true;
  assert(open_ms(1000)==RISC_STREAM_RETAINED&&opened.session&&claim_live[0]&&claim_live[1]);
  assert(!closes&&!d->poll.streams.driver.quiesce());
  release_fail[1]=false;clean();assert(real_host_driver->quiesce());
 }else if(!strcmp(mode,"real-dirty-claim-clean")){
  claim_fail[1]=claim_dirty=true;
  assert(open_ms(1000)==RISC_STREAM_IO&&!opened.session&&!claim_live[0]&&!claim_live[1]);
  assert(releases==2&&real_host_driver->quiesce());
 }else if(!strcmp(mode,"real-detach")){
  assert(open_ms(1000)==0);event_kind=2;size_t p=0;assert(real_host->poll(0,16,&p));
  assert(call(3,100)==RISC_STREAM_DISCONNECTED&&!closes);clean();assert(real_host_driver->quiesce());
 }else if(!strcmp(mode,"real-data")){
  assert(open_ms(1000)==0);memcpy(q[opened.tx_endpoint].data,"native",6);q[opened.tx_endpoint].size=6;
  for(unsigned i=0;i<3;i++)d->poll.poll(10);
  assert(wire_size==6&&!memcmp(wire,"native",6));clean();assert(real_host_driver->quiesce());
 }else{assert(!"unknown composed test");}
#else
#ifdef LEGACY_CLIENT
 if(!strcmp(mode,"old-header")){legacy_client(&d->poll.streams.driver);assert(!claim_live[0]&&!claim_live[1]);}else
#endif
 if(!strcmp(mode,"invalid")){
  assert(open_ms(0)==-1&&!opened.session&&!opened.rx_endpoint&&!opened.tx_endpoint&&!opened.reserved);
  assert(open_ms(1001)==-1);request.config.flow_control=1;assert(open_ms(1000)==-1);request.config.flow_control=0;
  request.device_generation++;assert(open_ms(1000)==-6&&!claims);request.device_generation--;
  assert(a->close(999,1000)==-1);assert(d->poll.streams.driver.quiesce());
 }else if(!strcmp(mode,"budget-open")){
  list_cost=1;config_cost=1;claim_cost=1;control_cost=1;
  assert(open_ms(10)==0);assert(control_budget==5&&now==6);clean();
 }else if(!strcmp(mode,"budget-exhaust")){
  list_cost=1;config_cost=1;claim_cost=1;
  assert(open_ms(4)==RISC_STREAM_RETAINED&&claims==1&&!controls&&!publishes&&!releases);
  assert(opened.session&&!opened.rx_endpoint);claim_cost=config_cost=list_cost=0;clean();
 }else if(!strcmp(mode,"budget-before-claims")){
  config_cost=2;assert(open_ms(2)==RISC_STREAM_TIMEOUT&&!claims&&!opened.session);
 }else if(!strcmp(mode,"clock-error")){
  clock_fail=true;assert(open_ms(10)==RISC_STREAM_TIMEOUT&&!claims);clock_fail=false;
  assert(open_ms(1000)==0);clock_fail=true;assert(a->close(opened.session,100)==RISC_STREAM_RETAINED&&!releases&&!closes);clock_fail=false;clean();
 }else if(!strcmp(mode,"missing-ack")||!strcmp(mode,"delayed-ack")){
  assert(open_ms(1000)==0);release_fail[1]=true;
  for(unsigned i=0;i<50;i++)assert(a->close(opened.session,1)==RISC_STREAM_RETAINED&&q[opened.rx_endpoint].live&&q[opened.tx_endpoint].live&&!closes);
  assert(finishes==2&&releases==50&&!d->poll.streams.driver.quiesce());
  d->poll.poll(10);assert(!reads&&!writes);d->poll.streams.driver.stop();assert(!closes&&claim_live[0]&&claim_live[1]);
  if(!strcmp(mode,"delayed-ack")){release_fail[1]=false;clean();}
 }else if(!strcmp(mode,"partial-close")){
  assert(open_ms(1000)==0);release_fail[0]=true;assert(a->close(opened.session,100)==RISC_STREAM_RETAINED&&!claim_live[1]&&claim_live[0]&&!closes);
  release_fail[0]=false;clean();assert(releases==3);
 }else if(!strcmp(mode,"close-budget")){
  assert(open_ms(1000)==0);queue_cost=1;assert(a->close(opened.session,2)==RISC_STREAM_RETAINED&&finishes==2&&!releases&&!closes);queue_cost=0;clean();
 }else if(!strcmp(mode,"close-after-release-budget")){
  assert(open_ms(1000)==0);release_cost=1;assert(a->close(opened.session,2)==RISC_STREAM_RETAINED&&!claim_live[0]&&!claim_live[1]&&!closes);release_cost=0;clean();assert(releases==2);
 }else if(!strcmp(mode,"partial-queue-close")){
  assert(open_ms(1000)==0);close_fail=2;assert(a->close(opened.session,100)==RISC_STREAM_RETAINED&&!q[opened.rx_endpoint].live&&q[opened.tx_endpoint].live);close_fail=0;clean();assert(closes==3&&releases==2);
 }else if(!strcmp(mode,"finish-failure")){
  assert(open_ms(1000)==0);finish_fail=2;assert(a->close(opened.session,100)==RISC_STREAM_RETAINED&&!releases&&!closes);finish_fail=0;clean();assert(finishes==3);
 }else if(!strcmp(mode,"partial-publish")){
  publish_fail=2;release_fail[1]=true;assert(open_ms(1000)==RISC_STREAM_RETAINED&&opened.session&&opened.rx_endpoint&&!opened.tx_endpoint&&!closes);release_fail[1]=false;clean();
 }else if(!strcmp(mode,"partial-publish-clean")){
  publish_fail=2;assert(open_ms(1000)==RISC_STREAM_IO&&!opened.session&&!opened.rx_endpoint&&closes==1&&releases==2);
 }else if(!strcmp(mode,"failed-config")){
  control_result=-1;assert(open_ms(1000)==RISC_STREAM_IO&&!opened.session&&releases==2&&!publishes);
 }else if(!strcmp(mode,"failed-claim")){
  claim_fail[1]=true;assert(open_ms(1000)==RISC_STREAM_IO&&!opened.session&&releases==1);
 }else if(!strcmp(mode,"dirty-failed-claim")){
  claim_fail[1]=claim_dirty=release_fail[1]=true;assert(open_ms(1000)==RISC_STREAM_RETAINED&&opened.session&&claim_live[0]&&claim_live[1]);release_fail[1]=false;clean();
 }else if(!strcmp(mode,"detach-reconnect")){
  assert(open_ms(1000)==0);uint64_t stale=opened.session;absent=true;assert(call(3,100)==RISC_STREAM_DISCONNECTED&&!closes&&claim_live[0]);d->poll.poll(10);assert(!reads);clean();absent=false;device=request.provider_device=request.device_generation=18;assert(open_ms(1000)==0&&opened.session!=stale);assert(a->close(stale,100)==RISC_STREAM_INVALID);clean();
 }else if(!strcmp(mode,"inventory-failure")){
  assert(open_ms(1000)==0);inventory_fail=true;assert(call(3,100)==RISC_STREAM_IO&&!finishes&&!closes);inventory_fail=false;bad_list=true;assert(call(3,100)==RISC_STREAM_IO&&!finishes);bad_list=false;config_fail=true;assert(call(3,100)==RISC_STREAM_IO&&!finishes);config_fail=false;config_malformed=true;assert(call(3,100)==RISC_STREAM_IO&&!finishes);config_malformed=false;d->poll.poll(10);assert(!reads&&!writes);assert(call(3,100)==0);d->poll.poll(10);assert(reads==1);clean();
 }else if(!strcmp(mode,"partial-data")){
  assert(open_ms(1000)==0);memcpy(q[opened.tx_endpoint].data,"hello",5);q[opened.tx_endpoint].size=5;produce_chunk=1;
  for(unsigned i=0;i<4;i++)d->poll.poll(10);
  assert(wire_size==5&&!memcmp(wire,"hello",5));assert(q[opened.rx_endpoint].size==7&&!memcmp(q[opened.rx_endpoint].data,"ABABABA",7));clean();
 }else if(!strcmp(mode,"poll-budget")){
  assert(open_ms(1000)==0);read_cost=1;memcpy(q[opened.tx_endpoint].data,"hello",5);q[opened.tx_endpoint].size=5;
  d->poll.poll(1);assert(reads==1&&read_budget==1&&!writes&&!q[opened.rx_endpoint].size);
  read_cost=0;d->poll.poll(1);assert(writes==1&&write_budget==1&&q[opened.rx_endpoint].size>=2);clean();
 }else if(!strcmp(mode,"queue-budget")){
  assert(open_ms(1000)==0);queue_cost=1;memcpy(q[opened.tx_endpoint].data,"hi",2);q[opened.tx_endpoint].size=2;
  d->poll.poll(2);assert(now==2&&reads==1&&!writes);queue_cost=0;d->poll.poll(2);assert(writes==1);clean();
 }else if(!strcmp(mode,"stale-native-prefix")){
  assert(open_ms(1000)==0);const risc_serial_port_streams_v1*p=d->poll.streams.driver.capability;uint8_t v=0;uint32_t r=99,t=99;
  assert(!p->inventory.discovery.serial.configure(opened.session,9600,8,0,1));assert(!p->inventory.discovery.serial.close(opened.session));assert(p->inventory.discovery.serial.read(opened.session,&v,1,1)<0);assert(!p->endpoints(opened.session,&r,&t)&&!r&&!t);clean();
 }else if(!strcmp(mode,"legacy")){
  const risc_serial_port_api_v1*p=d->poll.streams.driver.capability;uint64_t s=p->open(17);assert(s&&p->configure(s,9600,8,0,1)&&p->control_lines(s,true,false));uint8_t v[2];assert(p->read(s,v,2,15)==2&&p->write(s,v,2,20)==2);release_fail[1]=true;assert(!p->close(s));assert(p->read(s,v,2,1)<0);release_fail[1]=false;assert(p->close(s));assert(!p->close(s));
 }else if(!strcmp(mode,"wide-clock")){
  now=UINT64_C(0xffffffff)-2;list_cost=config_cost=claim_cost=control_cost=1;assert(open_ms(10)==0&&control_budget==5&&now>UINT32_MAX);clean();
 }else{assert(!"unknown test");}
 #endif
 assert(!polls);printf("PASS native CDC %s\n",mode);return 0;
}

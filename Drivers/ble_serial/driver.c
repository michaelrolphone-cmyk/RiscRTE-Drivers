/* Board-neutral BLE central over the established exclusive HCI lease.
 * Apache NimBLE owns GAP/GATT/ATT/L2CAP, controller credits and fragmentation.
 * This provider copies discovery records and notification bytes, never app
 * pointers. Protocol-specific ELM commands belong to the dashboard app. */
#include "RiscBluetoothSerialV1.h"
#include "RiscBluetoothHostV1.h"
#include "RiscPlatformClockV1.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_stop.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "ble_port.h"
#include <stdatomic.h>
#include <string.h>
#define SERVICE_MAX 24u
#define CHAR_MAX 64u
#define RX_SIZE 2048u
static const portable_bluetooth_host_v1 *host;
static const risc_platform_clock_api_v1 *clock_api;
static atomic_flag guard=ATOMIC_FLAG_INIT;
static uint64_t lease,token,serial;
static bool started,initialized,stopped,stopping,poisoned,scan_pending,tx_busy;
static uint16_t connection=BLE_HS_CONN_HANDLE_NONE,ccc,selected;
static uint32_t deadline,state,peer_count,service_count,char_count,channel_count,service_at;
static uint32_t rx_total,tx_total,dropped,generation;
static int32_t error_code;
static char message[72];
static risc_serial_peer_v1 peers[RISC_SERIAL_MAX_PEERS];
static struct ble_gatt_svc services[SERVICE_MAX];
static struct {struct ble_gatt_chr chr;uint16_t service;} chars[CHAR_MAX];
static struct {risc_serial_channel_v1 api;uint16_t end;uint8_t properties;} channels[RISC_SERIAL_MAX_CHANNELS];
static uint8_t rx[RX_SIZE];
static uint32_t rx_head,rx_count;
static struct ble_hs_stop_listener stop_listener;
static bool enter(void){return !atomic_flag_test_and_set_explicit(&guard,memory_order_acquire);}
static void leave(void){atomic_flag_clear_explicit(&guard,memory_order_release);}
static void text(char *out,size_t n,const char *in){size_t i=0;if(!in)in="";while(i+1<n&&in[i]){out[i]=in[i];i++;}out[i]=0;}
static bool valid(uint64_t t){return started&&token&&t==token;}
static void fail(int rc,const char *why){if(state!=RISC_SERIAL_FAULT){error_code=rc;text(message,sizeof(message),why);}state=RISC_SERIAL_FAULT;tx_busy=false;rx_count=0;}
static bool active(void){return !stopping&&state!=RISC_SERIAL_FAULT&&connection!=BLE_HS_CONN_HANDLE_NONE;}
static bool callback_ok(uint16_t c){return active()&&c==connection;}
static void phase(uint32_t s,const char *why,uint32_t timeout){state=s;text(message,sizeof(message),why);deadline=ble_npl_time_get()+timeout;}
static bool uuid_vendor(const ble_uuid_t *u){uint16_t n=ble_uuid_u16(u);return u->type!=BLE_UUID_TYPE_16||n>=0xfe00;}
/* UUID text is bounded locally; the native ABI deliberately has no sprintf. */
static void uuid_text(const ble_uuid_t *u,char out[37]){
 static const char digits[]="0123456789abcdef";
 if(u->type==BLE_UUID_TYPE_16||u->type==BLE_UUID_TYPE_32){
  uint32_t v=u->type==BLE_UUID_TYPE_16?BLE_UUID16(u)->value:BLE_UUID32(u)->value;
  unsigned n=u->type==BLE_UUID_TYPE_16?4:8;
  for(unsigned i=0;i<n;i++)out[i]=digits[(v>>((n-i-1)*4))&15];out[n]=0;return;
 }
 const uint8_t *v=BLE_UUID128(u)->value;unsigned n=0;
 for(unsigned i=0;i<16;i++){if(i==4||i==6||i==8||i==10)out[n++]='-';uint8_t b=v[15-i];out[n++]=digits[b>>4];out[n++]=digits[b&15];}out[n]=0;
}
static bool known_uart(const struct ble_gatt_svc *s,const struct ble_gatt_chr *w,const struct ble_gatt_chr *n){
 uint16_t su=ble_uuid_u16(&s->uuid.u),wu=ble_uuid_u16(&w->uuid.u),nu=ble_uuid_u16(&n->uuid.u);
 if(su==0xffe0&&wu==0xffe1&&nu==0xffe1)return true;
 if(su==0xfff0&&(wu==0xfff1||wu==0xfff2)&&(nu==0xfff1||nu==0xfff2))return true;
 char a[37],b[37],c[37];uuid_text(&s->uuid.u,a);uuid_text(&w->uuid.u,b);uuid_text(&n->uuid.u,c);
 return !strcmp(a,"6e400001-b5a3-f393-e0a9-e50e24dcca9e")&&!strcmp(b,"6e400002-b5a3-f393-e0a9-e50e24dcca9e")&&!strcmp(c,"6e400003-b5a3-f393-e0a9-e50e24dcca9e");
}
static void make_channels(void){
 channel_count=0;
 for(unsigned i=0;i<char_count;i++)for(unsigned j=0;j<char_count;j++){
  const struct ble_gatt_chr *w=&chars[i].chr,*n=&chars[j].chr;
  if(chars[i].service!=chars[j].service||!(w->properties&(BLE_GATT_CHR_PROP_WRITE|BLE_GATT_CHR_PROP_WRITE_NO_RSP))||!(n->properties&(BLE_GATT_CHR_PROP_NOTIFY|BLE_GATT_CHR_PROP_INDICATE)))continue;
  const struct ble_gatt_svc *s=&services[chars[i].service];if(!uuid_vendor(&s->uuid.u))continue;
  if(channel_count==RISC_SERIAL_MAX_CHANNELS){fail(BLE_HS_ENOMEM,"Too many serial channels; discovery limit");return;}
  unsigned k=channel_count++;memset(&channels[k],0,sizeof(channels[k]));
  risc_serial_channel_v1 *o=&channels[k].api;o->struct_size=sizeof(*o);o->tx_handle=w->val_handle;o->rx_handle=n->val_handle;
  o->flags=RISC_SERIAL_UNAUTHENTICATED|(known_uart(s,w,n)?RISC_SERIAL_KNOWN_UART:0);
  uuid_text(&s->uuid.u,o->service);uuid_text(&w->uuid.u,o->tx_uuid);uuid_text(&n->uuid.u,o->rx_uuid);
  channels[k].end=s->end_handle;channels[k].properties=w->properties;
  for(unsigned x=j+1;x<char_count;x++)if(chars[x].service==chars[j].service){channels[k].end=chars[x].chr.def_handle-1;break;}
  /* Store notification choice separately from transmitter properties. */
  if(n->properties&BLE_GATT_CHR_PROP_NOTIFY)channels[k].properties|=0x80;
 }
 if(!channel_count)fail(BLE_HS_ENOENT,"No BLE serial channel. Classic SPP is unsupported.");
 else phase(RISC_SERIAL_CHANNELS,"Choose serial channel",0);
}
static int chr_cb(uint16_t,const struct ble_gatt_error *,const struct ble_gatt_chr *,void *);
static void next_service(void){
 while(service_at<service_count&&!uuid_vendor(&services[service_at].uuid.u))service_at++;
 if(service_at==service_count){make_channels();return;}
 int rc=ble_gattc_disc_all_chrs(connection,services[service_at].start_handle,services[service_at].end_handle,chr_cb,NULL);
 if(rc)fail(rc,"Characteristic discovery failed");else deadline=ble_npl_time_get()+10000;
}
static int chr_cb(uint16_t c,const struct ble_gatt_error *e,const struct ble_gatt_chr *v,void *arg){
 (void)arg;if(!callback_ok(c))return BLE_HS_EAPP;
 if(e->status==BLE_HS_EDONE){service_at++;next_service();return 0;}
 if(e->status||!v){fail(e->status,"Characteristic discovery failed");return BLE_HS_EAPP;}
 if(char_count==CHAR_MAX){fail(BLE_HS_ENOMEM,"Too many characteristics");return BLE_HS_EAPP;}
 chars[char_count].chr=*v;chars[char_count++].service=(uint16_t)service_at;return 0;
}
static int svc_cb(uint16_t c,const struct ble_gatt_error *e,const struct ble_gatt_svc *v,void *arg){
 (void)arg;if(!callback_ok(c))return BLE_HS_EAPP;
 if(e->status==BLE_HS_EDONE){service_at=0;next_service();return 0;}
 if(e->status||!v){fail(e->status,"Service discovery failed");return BLE_HS_EAPP;}
 if(service_count==SERVICE_MAX){fail(BLE_HS_ENOMEM,"Too many services");return BLE_HS_EAPP;}
 services[service_count++]=*v;return 0;
}
static int subscribed_cb(uint16_t c,const struct ble_gatt_error *e,struct ble_gatt_attr *a,void *arg){
 (void)a;(void)arg;if(!callback_ok(c))return 0;
 if(e->status)fail(e->status,"Subscription refused; pairing may be required");
 else phase(RISC_SERIAL_READY,"BLE serial ready (unpaired)",0);return 0;
}
static int dsc_cb(uint16_t c,const struct ble_gatt_error *e,uint16_t value,const struct ble_gatt_dsc *d,void *arg){
 (void)arg;if(!callback_ok(c)||value!=channels[selected].api.rx_handle)return BLE_HS_EAPP;
 if(e->status==BLE_HS_EDONE){
  if(!ccc){fail(BLE_HS_ENOENT,"Notification subscription descriptor is missing");return 0;}
  uint8_t enable[2]={(channels[selected].properties&0x80)?1:2,0};
  int rc=ble_gattc_write_flat(c,ccc,enable,2,subscribed_cb,NULL);if(rc)fail(rc,"Subscription write failed");return 0;
 }
 if(e->status||!d){fail(e->status,"Descriptor discovery failed");return BLE_HS_EAPP;}
 if(ble_uuid_u16(&d->uuid.u)==0x2902)ccc=d->handle;return 0;
}
static int written_cb(uint16_t c,const struct ble_gatt_error *e,struct ble_gatt_attr *a,void *arg){
 (void)a;(void)arg;if(callback_ok(c)){tx_busy=false;if(e->status)fail(e->status,"Serial write refused; pairing may be required");}return 0;
}
static int gap_cb(struct ble_gap_event *e,void *arg){
 (void)arg;
 if(e->type==BLE_GAP_EVENT_DISCONNECT){
  if(e->disconnect.conn.conn_handle==connection){connection=BLE_HS_CONN_HANDLE_NONE;tx_busy=false;rx_count=0;error_code=e->disconnect.reason;
   if(!stopping&&state!=RISC_SERIAL_FAULT)phase(RISC_SERIAL_DISCONNECTED,"Adapter disconnected",0);}
  return 0;
 }
 if(stopping||state==RISC_SERIAL_FAULT)return 0;
 switch(e->type){
 case BLE_GAP_EVENT_DISC:{
  if(state!=RISC_SERIAL_SCANNING)return 0;
  const struct ble_gap_disc_desc *v=&e->disc;
  unsigned i;for(i=0;i<peer_count;i++)if(peers[i].address_type==v->addr.type&&!memcmp(peers[i].address,v->addr.val,6))break;
  if(i==peer_count){if(peer_count==RISC_SERIAL_MAX_PEERS){dropped++;return 0;}memset(&peers[i],0,sizeof(peers[i]));peers[i].struct_size=sizeof(peers[i]);memcpy(peers[i].address,v->addr.val,6);peers[i].address_type=v->addr.type;peer_count++;}
  peers[i].rssi=v->rssi;peers[i].connectable|=v->event_type==BLE_HCI_ADV_RPT_EVTYPE_ADV_IND||v->event_type==BLE_HCI_ADV_RPT_EVTYPE_DIR_IND;
  struct ble_hs_adv_fields f;if(!ble_hs_adv_parse_fields(&f,v->data,v->length_data)&&f.name&&f.name_len){unsigned n=f.name_len;if(n>31)n=31;
   for(unsigned k=0;k<n;k++)peers[i].name[k]=(f.name[k]>=32&&f.name[k]<=126)?(char)f.name[k]:'?';peers[i].name[n]=0;}
  return 0;}
 case BLE_GAP_EVENT_DISC_COMPLETE:if(state==RISC_SERIAL_SCANNING)phase(RISC_SERIAL_SCAN_DONE,"Select your OBD adapter",0);return 0;
 case BLE_GAP_EVENT_CONNECT:
  if(state!=RISC_SERIAL_CONNECTING)return 0;
  if(e->connect.status){error_code=e->connect.status;phase(RISC_SERIAL_DISCONNECTED,"Connection failed",0);return 0;}
  connection=e->connect.conn_handle;generation++;service_count=char_count=channel_count=0;rx_head=rx_count=0;tx_busy=false;
  phase(RISC_SERIAL_DISCOVERING,"Discovering serial services",30000);
  {int rc=ble_gattc_disc_all_svcs(connection,svc_cb,NULL);if(rc)fail(rc,"Service discovery failed");}return 0;
 case BLE_GAP_EVENT_NOTIFY_RX:{
  if(e->notify_rx.conn_handle!=connection||state!=RISC_SERIAL_READY||e->notify_rx.attr_handle!=channels[selected].api.rx_handle)return 0;
  uint16_t n=OS_MBUF_PKTLEN(e->notify_rx.om);if(n>RX_SIZE-rx_count){fail(BLE_HS_ENOMEM,"Serial receive overflow; reconnect");return 0;}
  uint8_t b[64];unsigned at=0;while(at<n){unsigned take=n-at;if(take>sizeof(b))take=sizeof(b);
   if(os_mbuf_copydata(e->notify_rx.om,(int)at,(int)take,b)){fail(BLE_HS_EBADDATA,"Malformed notification");return 0;}
   for(unsigned k=0;k<take;k++)rx[(rx_head+rx_count++)%RX_SIZE]=b[k];at+=take;}
  rx_total+=n;return 0;}
 default:return 0;
 }
}
static void reset_cb(int reason){if(!stopping)fail(reason,"Bluetooth host reset; reconnect");}
static void sync_cb(void){if(!stopping&&state==RISC_SERIAL_STARTING)scan_pending=true;}
static bool pump(uint32_t work){
 for(uint32_t i=0;i<work;i++){
  bool ok=hid_port_receive();hid_port_timers();struct ble_npl_event *e=ble_npl_eventq_get(nimble_port_get_dflt_eventq(),0);if(e)ble_npl_event_run(e);
  if(!ok&&!stopping)break;
 }
 if(hid_port_faulted()&&!stopping)fail(BLE_HS_ECONTROLLER,"Bluetooth transport failed");
 if(!stopping&&scan_pending&&state==RISC_SERIAL_STARTING){scan_pending=false;uint8_t own;
  struct ble_gap_disc_params p={0};p.itvl=80;p.window=48;p.filter_duplicates=0;p.passive=0;
  int rc=ble_hs_id_infer_auto(0,&own);if(!rc)rc=ble_gap_disc(own,8000,&p,gap_cb,NULL);
  if(rc)fail(rc,"BLE scan could not start");else phase(RISC_SERIAL_SCANNING,"Scanning for adapters",10000);
 }
 if(!stopping&&(state==RISC_SERIAL_STARTING||state==RISC_SERIAL_SCANNING||state==RISC_SERIAL_CONNECTING||state==RISC_SERIAL_DISCOVERING||state==RISC_SERIAL_SUBSCRIBING||tx_busy)&&
    (int32_t)(ble_npl_time_get()-deadline)>=0)fail(BLE_HS_ETIMEOUT,"Bluetooth operation timed out");
 return stopping||state!=RISC_SERIAL_FAULT;
}
static bool api_open(void *c,uint64_t *out){
 (void)c;if(!out)return false;*out=0;if(!enter())return false;
 if(!started||token||poisoned||serial==UINT64_MAX){leave();return false;}
 token=++serial;*out=token;peer_count=service_count=char_count=channel_count=0;rx_count=rx_head=rx_total=tx_total=dropped=0;error_code=0;tx_busy=stopping=scan_pending=false;
 connection=BLE_HS_CONN_HANDLE_NONE;state=RISC_SERIAL_STARTING;text(message,sizeof(message),"Starting Bluetooth");
 if(!host->claim(host->controls.context,&lease)||!lease){fail(BLE_HS_EBUSY,"Bluetooth is busy; stop SDR, HID or Scanner");leave();return false;}
 hid_port_bind(host,clock_api,lease);deadline=ble_npl_time_get()+4000;stopped=false;
 if(!initialized){nimble_port_init();ble_hs_cfg.reset_cb=reset_cb;ble_hs_cfg.sync_cb=sync_cb;initialized=true;}
 else {int rc=ble_hs_start();if(rc){poisoned=true;fail(rc,"Bluetooth host needs reload");}}
 /* Establish the first host start before close may be requested. */
 bool ok=pump(2);leave();return ok;
}
static bool api_poll(void*c,uint64_t t,uint32_t n){(void)c;if(!enter())return false;bool ok=valid(t)&&lease&&n&&n<=16;if(ok)ok=pump(n);leave();return ok;}
static bool api_status(void*c,uint64_t t,risc_serial_status_v1 *o){
 (void)c;if(!o||o->struct_size<sizeof(*o)||!enter())return false;
 bool ok=started&&(token?t==token:t==0);if(ok){*o=(risc_serial_status_v1){.struct_size=sizeof(*o),.state=state,.flags=RISC_SERIAL_UNAUTHENTICATED|(tx_busy?RISC_SERIAL_TX_BUSY:0)|(token?RISC_SERIAL_CLEANUP_REQUIRED:0)|(poisoned?RISC_SERIAL_RELOAD_REQUIRED:0),.peer_count=peer_count,.channel_count=channel_count,.rx_bytes=rx_total,.tx_bytes=tx_total,.dropped_peers=dropped,.generation=generation,.error_code=error_code};text(o->message,sizeof(o->message),message);}leave();return ok;
}
static bool api_peer(void*c,uint64_t t,uint32_t i,risc_serial_peer_v1 *o){(void)c;if(!o||o->struct_size<sizeof(*o)||!enter())return false;bool ok=valid(t)&&i<peer_count;if(ok)*o=peers[i];leave();return ok;}
static bool api_connect(void*c,uint64_t t,const risc_serial_peer_v1 *p){
 (void)c;if(!p||p->struct_size<sizeof(*p)||!enter())return false;
 bool ok=valid(t)&&(state==RISC_SERIAL_SCANNING||state==RISC_SERIAL_SCAN_DONE)&&p->address_type<=1;
 unsigned i;for(i=0;i<peer_count;i++)if(p->address_type==peers[i].address_type&&!memcmp(p->address,peers[i].address,6)&&peers[i].connectable)break;
 if(!ok||i==peer_count){leave();return false;}
 if(state==RISC_SERIAL_SCANNING){int rc=ble_gap_disc_cancel();if(rc&&rc!=BLE_HS_EALREADY){fail(rc,"Could not stop scan");leave();return false;}}
 ble_addr_t peer={.type=p->address_type};memcpy(peer.val,p->address,6);uint8_t own;
 phase(RISC_SERIAL_CONNECTING,"Connecting to selected adapter",12000);
 int rc=ble_hs_id_infer_auto(0,&own);if(!rc)rc=ble_gap_connect(own,&peer,10000,NULL,gap_cb,NULL);
 if(rc)fail(rc,"Connect request failed");leave();return rc==0;
}
static bool api_channel(void*c,uint64_t t,uint32_t i,risc_serial_channel_v1 *o){(void)c;if(!o||o->struct_size<sizeof(*o)||!enter())return false;bool ok=valid(t)&&i<channel_count;if(ok)*o=channels[i].api;leave();return ok;}
static bool api_select(void*c,uint64_t t,uint32_t i){
 (void)c;if(!enter())return false;if(!valid(t)||state!=RISC_SERIAL_CHANNELS||i>=channel_count){leave();return false;}
 selected=(uint16_t)i;ccc=0;rx_head=rx_count=0;phase(RISC_SERIAL_SUBSCRIBING,"Enabling serial notifications",10000);
 int rc=ble_gattc_disc_all_dscs(connection,channels[i].api.rx_handle,channels[i].end,dsc_cb,NULL);
 if(rc)fail(rc,"Subscription discovery failed");leave();return rc==0;
}
static int32_t api_write(void*c,uint64_t t,const uint8_t *b,uint32_t n){
 (void)c;if(!enter())return -1;
 if(!valid(t)||state!=RISC_SERIAL_READY||!b||!n||n>RISC_SERIAL_MAX_WRITE){leave();return -1;}if(tx_busy){leave();return 0;}
 int rc;if(channels[selected].properties&BLE_GATT_CHR_PROP_WRITE){tx_busy=true;deadline=ble_npl_time_get()+5000;rc=ble_gattc_write_flat(connection,channels[selected].api.tx_handle,b,(uint16_t)n,written_cb,NULL);}
 else rc=ble_gattc_write_no_rsp_flat(connection,channels[selected].api.tx_handle,b,(uint16_t)n);
 if(rc){tx_busy=false;if(rc!=BLE_HS_ENOMEM&&rc!=BLE_HS_EBUSY){fail(rc,"BLE serial transmit failed");leave();return -1;}leave();return 0;}
 tx_total+=n;leave();return (int32_t)n;
}
static int32_t api_read(void*c,uint64_t t,uint8_t *b,uint32_t n){
 (void)c;if(!enter())return -1;if(!valid(t)||state!=RISC_SERIAL_READY||!b||!n){leave();return -1;}
 if(n>rx_count)n=rx_count;for(uint32_t i=0;i<n;i++){b[i]=rx[rx_head];rx_head=(rx_head+1)%RX_SIZE;}rx_count-=n;leave();return (int32_t)n;
}
static void stopped_cb(int rc,void *arg){(void)arg;stopped=true;if(rc)poisoned=true;}
static bool close_impl(uint64_t t){
 if(!valid(t))return false;stopping=true;scan_pending=false;
 if(initialized&&lease&&!stopped){int rc=ble_hs_stop(&stop_listener,stopped_cb,NULL);if(rc==BLE_HS_EALREADY)stopped=true;
  for(unsigned i=0;i<300&&!stopped;i++){(void)pump(8);clock_api->sleep_ms(clock_api->context,1);}
  if(!stopped)poisoned=true;
 }
 if(lease){int rc=host->release(host->controls.context,lease);if(rc<0){fail(BLE_HS_ECONTROLLER,"Bluetooth cleanup pending; retry close");return false;}lease=0;if(!rc)text(message,sizeof(message),"Bluetooth closed; previous power restore failed");}
 token=0;connection=BLE_HS_CONN_HANDLE_NONE;rx_count=0;tx_busy=stopping=false;state=RISC_SERIAL_OFF;return true;
}
static bool api_close(void*c,uint64_t t){(void)c;if(!enter())return false;bool ok=close_impl(t);leave();return ok;}
static bool start(const risc_provider_dependency_v1 *d,size_t n){
 if(!enter())return false;if(started||!d||n!=2){leave();return false;}
 const portable_bluetooth_host_v1 *h=NULL;const risc_platform_clock_api_v1 *k=NULL;
 for(size_t i=0;i<n;i++){if(!d[i].capability_id||d[i].api_version!=1||!d[i].api){leave();return false;}
  if(!strcmp(d[i].capability_id,"bluetooth.hci")&&!h)h=d[i].api;else if(!strcmp(d[i].capability_id,"platform.clock")&&!k)k=d[i].api;else {leave();return false;}}
 bool ok=h&&k&&h->controls.api_version==1&&h->controls.struct_size>=sizeof(*h)&&h->claim&&h->release&&h->send_owned&&h->next_owned&&k->api_version==1&&k->struct_size>=sizeof(*k)&&k->monotonic_ms&&k->sleep_ms;
 if(ok){host=h;clock_api=k;started=true;}leave();return ok;
}
static bool quiesce(void){if(!enter())return false;bool ok=!token||close_impl(token);leave();return ok;}
static void stop(void){if(!enter())return;if(!token){started=false;host=NULL;clock_api=NULL;}leave();}
static const risc_bluetooth_serial_v1 api={1,sizeof(api),NULL,api_open,api_poll,api_status,api_peer,api_connect,api_channel,api_select,api_write,api_read,api_close};
static const risc_driver_v2 descriptor={2,sizeof(descriptor),"ble-serial",RISC_BLUETOOTH_SERIAL_CAPABILITY,1,&api,start,stop,quiesce};
__attribute__((visibility("default"))) const risc_driver_v2 *t5_driver_get(uint32_t abi){return abi==2?&descriptor:NULL;}

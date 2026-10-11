char* setup_log_uuid(const ble_uuid_t*uuid,char*out){(void)uuid;out[0]=0;return out;}
/* Same cooperative host and lease as peripheral setup. No native transport or
 * credential authority is added. Only selected advertised service peers qualify. */
static struct {ble_addr_t address;risc_bluetooth_session_peer_v1 value;} peers[RISC_SESSION_PEERS_MAX];
static unsigned peers_count;static uint32_t peer_serial;
static uint16_t service_start,service_end,descriptor_handle,validity_handle,characteristic_cursor,characteristic_last;
static uint16_t get16(const uint8_t*p){return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t get32(const uint8_t*p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint64_t get64(const uint8_t*p){return get32(p)|(uint64_t)get32(p+4)<<32;}
static void central_reset(void){central_phase=0;peers_count=0;service_start=service_end=descriptor_handle=validity_handle=0;peer_expiry=validity_started=0;erase(&peer_value,sizeof(peer_value));erase(&network_value,sizeof(network_value));erase(peers,sizeof(peers));}
static bool central_secure(uint16_t handle){struct ble_gap_conn_desc d;expire();return central_role&&!stopping&&state!=RISC_SETUP_FAILED&&state!=RISC_SETUP_ENDED&&handle==connection&&confirmed&&encrypted&&authenticated&&!ble_gap_conn_find(handle,&d)&&d.sec_state.encrypted&&d.sec_state.authenticated&&d.sec_state.key_size==16&&!d.sec_state.bonded;}
static int central_event(struct ble_gap_event*e){
 if(!central_role||stopping||state!=RISC_SETUP_DISCOVERING)return 0;
 if(e->type==BLE_GAP_EVENT_DISC_COMPLETE){end_session(RISC_SETUP_ENDED,RISC_SETUP_EXPIRED);return 0;}
 struct ble_hs_adv_fields f;if(ble_hs_adv_parse_fields(&f,e->disc.data,e->disc.length_data))return 0;
 unsigned index=0;for(;index<peers_count;index++)if(!memcmp(&peers[index].address,&e->disc.addr,sizeof(ble_addr_t)))break;
 bool matches=false;for(unsigned i=0;i<f.num_uuids128;i++)if(!ble_uuid_cmp(&f.uuids128[i].u,&service_uuid.u))matches=true;
 if(index==peers_count){if(!matches||peers_count==RISC_SESSION_PEERS_MAX||peer_serial==UINT32_MAX)return 0;peers_count++;peers[index].address=e->disc.addr;peers[index].value.struct_size=sizeof(peers[index].value);peers[index].value.peer_id=++peer_serial;memcpy(peers[index].value.name,"RiscRTE Files",14);}
 peers[index].value.rssi=e->disc.rssi;
 if(f.name&&f.name_len&&f.name_len<=20){bool printable=true;for(unsigned i=0;i<f.name_len;i++)if(f.name[i]<32||f.name[i]>126)printable=false;if(printable){memcpy(peers[index].value.name,f.name,f.name_len);peers[index].value.name[f.name_len]=0;}}
 return 0;
}
static int service_result(uint16_t h,const struct ble_gatt_error*e,const struct ble_gatt_svc*s,void*arg){(void)arg;if(!central_secure(h))return BLE_HS_EAUTHEN;if(e->status==BLE_HS_EDONE){if(!service_start||!service_end)failed(BLE_HS_ENOENT);else {characteristic_cursor=service_start;central_phase=3;}return 0;}if(e->status||!s||service_start||ble_uuid_cmp(&s->uuid.u,&service_uuid.u)){failed(e->status?e->status:BLE_HS_EINVAL);return BLE_HS_EINVAL;}service_start=s->start_handle;service_end=s->end_handle;return 0;}
static int characteristic_result(uint16_t h,const struct ble_gatt_error*e,const struct ble_gatt_chr*x,void*arg){(void)arg;if(!central_secure(h))return BLE_HS_EAUTHEN;if(e->status==BLE_HS_EDONE){if(descriptor_handle&&validity_handle)central_phase=5;else if(characteristic_last>=characteristic_cursor&&characteristic_last<service_end){characteristic_cursor=characteristic_last+1;central_phase=3;}else failed(BLE_HS_ENOENT);return 0;}if(e->status||!x){failed(e->status);return e->status;}
 characteristic_last=x->val_handle;uint16_t*target=NULL;if(!ble_uuid_cmp(&x->uuid.u,&descriptor_uuid.u))target=&descriptor_handle;else if(!ble_uuid_cmp(&x->uuid.u,&validity_uuid.u))target=&validity_handle;if(target){if(*target||x->val_handle<service_start||x->val_handle>service_end){failed(BLE_HS_EINVAL);return BLE_HS_EINVAL;}*target=x->val_handle;}return 0;}
static bool decode_peer(void){
 if(descriptor_size<16||(descriptor[0]!=1&&descriptor[0]!=2)||get16(descriptor+2)!=descriptor_size)return false;
 peer_value.struct_size=sizeof(peer_value);peer_value.transport=descriptor[1];peer_value.remaining_lifetime_ms=get32(descriptor+4);
 char*fields[]={peer_value.url,peer_value.username,peer_value.password,peer_value.session_id};const unsigned caps[]={sizeof(peer_value.url),sizeof(peer_value.username),sizeof(peer_value.password),sizeof(peer_value.session_id)};unsigned at=16;
 for(unsigned i=0;i<4;i++){unsigned n=get16(descriptor+8+2*i);if(!n||n>=caps[i]||n>descriptor_size-at)return false;memcpy(fields[i],descriptor+at,n);fields[i][n]=0;if(memchr(fields[i],0,n))return false;at+=n;}uint16_t sizes[4];if(!descriptor_valid(&peer_value,sizes))return false;
 if(descriptor[0]==1)return at==descriptor_size;
 if(descriptor_size-at<6)return false;unsigned sn=descriptor[at],pn=descriptor[at+1];
 if(!sn||sn>=sizeof(network_value.ssid)||pn<8||pn>=sizeof(network_value.password)||descriptor_size-at!=6+sn+pn)return false;
 network_value.struct_size=sizeof(network_value);network_value.endpoint=peer_value;memcpy(network_value.address,descriptor+at+2,4);
 memcpy(network_value.ssid,descriptor+at+6,sn);memcpy(network_value.password,descriptor+at+6+sn,pn);
 return network_valid(&network_value);
}
static int descriptor_result(uint16_t h,const struct ble_gatt_error*e,struct ble_gatt_attr*a,void*arg){(void)arg;if(!central_secure(h))return BLE_HS_EAUTHEN;if(e->status==BLE_HS_EDONE){if(!decode_peer()){failed(BLE_HS_EINVAL);return BLE_HS_EINVAL;}central_phase=7;return 0;}if(e->status||!a||a->handle!=descriptor_handle||a->offset!=descriptor_size){failed(e->status?e->status:BLE_HS_EINVAL);return BLE_HS_EINVAL;}unsigned n=OS_MBUF_PKTLEN(a->om);if(!n||n>sizeof(descriptor)-descriptor_size||os_mbuf_copydata(a->om,0,n,descriptor+descriptor_size)){failed(BLE_HS_EMSGSIZE);return BLE_HS_EMSGSIZE;}descriptor_size+=n;return 0;}
static int validity_result(uint16_t h,const struct ble_gatt_error*e,struct ble_gatt_attr*a,void*arg){(void)arg;uint8_t value[16];if(!central_secure(h))return BLE_HS_EAUTHEN;if(e->status||!a||a->handle!=validity_handle||a->offset||OS_MBUF_PKTLEN(a->om)!=sizeof(value)||os_mbuf_copydata(a->om,0,sizeof(value),value)){failed(e->status?e->status:BLE_HS_EINVAL);return BLE_HS_EINVAL;}uint32_t setup=get32(value),generation=get32(value+4);uint64_t remaining=get64(value+8);if(!setup||setup>RISC_SESSION_SETUP_MAX_LIFETIME_MS||!generation||remaining<setup||remaining>RISC_SESSION_SETUP_MAX_LIFETIME_MS||remaining>peer_value.remaining_lifetime_ms||validity_started>UINT64_MAX-remaining){failed(BLE_HS_EINVAL);return BLE_HS_EINVAL;}peer_expiry=validity_started+remaining;if(now()>=peer_expiry){failed(BLE_HS_ETIMEOUT);return BLE_HS_ETIMEOUT;}state=RISC_SETUP_READY;central_phase=9;return 0;}
static bool central_step(void){
 if(stopping||state==RISC_SETUP_FAILED||state==RISC_SETUP_ENDED)return true;int rc=0;
 if(central_phase==10){uint8_t address;struct ble_gap_disc_params p={0};p.passive=0;p.filter_duplicates=0;p.itvl=0x30;p.window=0x20;
  rc=ble_hs_util_ensure_addr(0);if(!rc)rc=ble_hs_id_infer_auto(0,&address);if(!rc)rc=ble_gap_disc(address,(int32_t)(setup_deadline-now()),&p,gap_event,NULL);if(!rc){state=RISC_SETUP_DISCOVERING;central_phase=0;}}
 else if(central_phase==1){central_phase=2;rc=ble_gattc_disc_svc_by_uuid(connection,&service_uuid.u,service_result,NULL);}
 else if(central_phase==3){central_phase=4;characteristic_last=0;rc=ble_gattc_disc_all_chrs(connection,characteristic_cursor,service_end,characteristic_result,NULL);}
 else if(central_phase==5){central_phase=6;erase(descriptor,sizeof(descriptor));descriptor_size=0;rc=ble_gattc_read_long(connection,descriptor_handle,0,descriptor_result,NULL);}
 else if(central_phase==7){central_phase=8;validity_started=now();rc=ble_gattc_read(connection,validity_handle,validity_result,NULL);}
 if(rc){failed(rc);return false;}return true;
}
static int32_t api_discover(void*c,uint32_t lifetime,uint64_t*out){(void)c;ENTER();if(token)LEAVE(RISC_SETUP_BUSY);if(!lifetime||lifetime>RISC_SESSION_SETUP_MAX_LIFETIME_MS)LEAVE(RISC_SETUP_INVALID);central_role=true;
 risc_bluetooth_session_descriptor_v1 d={.struct_size=sizeof(d),.transport=RISC_SESSION_HTTP,.remaining_lifetime_ms=lifetime,.url="http://127.0.0.1:1/",.username="discovery",.password="unused",.session_id="discovery"};LEAVE(open_impl("RiscRTE Files",&d,lifetime,out));}
static int32_t api_peer(void*c,uint64_t t,uint32_t index,risc_bluetooth_session_peer_v1*out){(void)c;ENTER();if(!valid(t)||!central_role)LEAVE(RISC_SETUP_CONTEXT);if(stopping)LEAVE(RISC_SETUP_CLEANUP_PENDING);expire();if(!out||out->struct_size<sizeof(*out))LEAVE(RISC_SETUP_INVALID);if(state!=RISC_SETUP_DISCOVERING||index>=peers_count)LEAVE(RISC_SETUP_INVALID);*out=peers[index].value;LEAVE(RISC_SETUP_OK);}
static int32_t api_select_peer(void*c,uint64_t t,uint32_t id){(void)c;ENTER();if(!valid(t)||!central_role)LEAVE(RISC_SETUP_CONTEXT);if(stopping)LEAVE(RISC_SETUP_CLEANUP_PENDING);expire();if(state!=RISC_SETUP_DISCOVERING)LEAVE(RISC_SETUP_INVALID);unsigned i=0;for(;i<peers_count;i++)if(peers[i].value.peer_id==id)break;if(i==peers_count)LEAVE(RISC_SETUP_INVALID);state=RISC_SETUP_CONNECTING;int rc=ble_gap_disc_cancel();if(rc&&rc!=BLE_HS_EALREADY){failed(rc);LEAVE(RISC_SETUP_FAULT);}uint8_t address;rc=ble_hs_id_infer_auto(0,&address);if(!rc){uint64_t left=setup_deadline-now();if(!left||left>RISC_SESSION_SETUP_MAX_LIFETIME_MS)LEAVE(RISC_SETUP_EXPIRED);state=RISC_SETUP_CONNECTING;rc=ble_gap_connect(address,&peers[i].address,(int32_t)(left>30000?30000:left),NULL,gap_event,NULL);}if(rc){failed(rc);LEAVE(RISC_SETUP_FAULT);}LEAVE(RISC_SETUP_PENDING);}
static int32_t api_peer_descriptor(void*c,uint64_t t,risc_bluetooth_session_descriptor_v1*out){(void)c;ENTER();if(!valid(t)||!central_role)LEAVE(RISC_SETUP_CONTEXT);if(stopping)LEAVE(RISC_SETUP_CLEANUP_PENDING);expire();if(!out||out->struct_size<sizeof(*out))LEAVE(RISC_SETUP_INVALID);uint64_t time=now();if(state!=RISC_SETUP_READY||central_phase!=9||!central_secure(connection)||time>=peer_expiry)LEAVE(RISC_SETUP_EXPIRED);*out=peer_value;out->remaining_lifetime_ms=(uint32_t)(peer_expiry-time);LEAVE(RISC_SETUP_OK);}

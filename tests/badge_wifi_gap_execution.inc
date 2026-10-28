/* Additive selected-native-WiFi Runtime cases. The only writes to production
 * state are real controller input and its existing namespace-6 storage path.
 * Scan rows and a refused connection are explicit hardware-model responses. */
extern "C" void badge_wifi_observe(const unsigned*s,const int*r,const char*editor,const char*ssid,const char*password,const char*entry,const char*message){
 if(!wm.enabled)return;memcpy(wm.state,s,sizeof(wm.state));memcpy(wm.rects,r,sizeof(wm.rects));
 wm.editor=editor;wm.ssid=ssid;wm.password=password;wm.entry=entry?entry:"";wm.message=message?message:"";
 if(wifiGapInjected&&!wifiGapFreshKey){assert(wm.ssid==wifiGapSsid&&wm.password==wifiGapPassword&&!wm.writes);if(wm.editor!=wifiGapEditor){assert(wifiGapFreshTap&&wm.editor==wifiGapEditor+"a"&&wm.state[2]==2);wifiGapFreshKey=true;puts("WIFI_GAP fresh_physical_key_exactly_once=passed retained_draft=passed");}}

 assert(wm.state[0]>=240&&wm.state[1]>=240&&wm.state[0]<=466&&wm.state[1]<=466);
 if(wm.state[2]==1||wm.state[2]==2)wm.pageMask|=1u<<wm.state[4];
}
void wifiKvWrite(uint32_t ns,const char*k){
 if(!wm.enabled||m.app!="wifi_settings")return;
 if(ns!=6){assert(m.caseIndex==45&&ns==4);return;} // Independent selected alarm service durability, not WiFi credential authority.
 assert(ns==6&&(!strcmp(k,"wifi.a0")||!strcmp(k,"wifi.a1")||!strcmp(k,"wifi.b0")||!strcmp(k,"wifi.b1")||!strcmp(k,"wifi.commit")));
 assert(m.caseIndex==44&&wm.writeAuthority);++wm.writes;
}
static std::string wifiDraft(){return std::to_string(wm.state[2])+"/"+std::to_string(wm.state[4])+"/"+wm.editor+"/"+wm.ssid+"/"+wm.password;}
static void wifiCapture(const char*name){
 const char*dir=getenv("BADGE_PROTOTYPE_RASTERS");if(!dir||!*dir)return;
 std::string path=std::string(dir)+"/"+std::to_string(m.caseIndex)+"-wifi-"+name+".ppm";
 FILE*f=fopen(path.c_str(),"wb");assert(f);fprintf(f,"P6\n466 466\n255\n");
 for(uint16_t p:m.pixels){unsigned char c[]={uint8_t(((p>>11)&31)*255/31),uint8_t(((p>>5)&63)*255/63),uint8_t((p&31)*255/31)};assert(fwrite(c,1,3,f)==3);}assert(!fclose(f));
}
void wifiPass(){
 assert(wm.enabled&&wm.finished);assert(m.caseIndex==43&&wifiGapInjected&&wifiGapNeutral&&wifiGapFreshTap&&wifiGapFreshKey&&m.frames>wifiGapFrame);puts("PASS actual Runtime installed native WiFi report gap active_draft_preserved fresh_key=passed");
 if(m.caseIndex==42){assert(wm.retainedFault&&m.terminal&&m.io==m.terminalIo&&wm.homeReturned&&wm.unsupportedFrame&&wm.hiddenFrame);}
 else assert(!m.terminal&&!am.retained);
 assert(am.resumeAttempts==m.lightEntries&&am.resumeSuccesses==m.lightEntries);
 assert(!m.nativeToken&&m.transactions==m.transactionResumes);
 if(m.caseIndex!=44)assert(!wm.writes);
 if(m.caseIndex==42||m.caseIndex==43)assert(!m.kvWrites);
 if(m.caseIndex==42)assert(wm.scanStarts==3&&wm.scanPolls&&wm.scanCancels>=3&&wm.scanCancelFailures==1);
 if(m.caseIndex==43)assert(m.lightEntries==3&&wm.pageMask==7&&wm.exited);
 if(m.caseIndex==44)assert(wm.writes==8&&wm.joins==1&&wm.password.empty()&&wm.ssid.empty()&&wm.exited);
 if(m.caseIndex==45){assert(am.acked&&am.visible&&am.saved&&am.timerWakes&&wm.pageMask==7&&wifiDraft()==wm.snapshot);assert(am.postAckFrame>am.ackedAtFrame&&!am.modal&&!am.restoringModal&&m.pixels==am.postAckPixels&&visibleRasterContent());wifiCapture("alarm-restored-password");}
 printf("PASS actual Runtime native WiFi case=%u actual_layout_input=passed scan_starts=%u scan_cancels=%u refused_connections=%u namespace6_writes=%u keyboard_pages=%u returning_Light=%u exact_resume_successes=%u completed_restore=%s retained_no_later_observed_callbacks=%s association_unverified hardware_unverified\n",m.caseIndex,wm.scanStarts,wm.scanCancels,wm.joins,wm.writes,wm.pageMask,m.lightEntries,am.resumeSuccesses,(m.caseIndex==43||m.caseIndex==45)?"passed":"not-in-case",m.caseIndex==42?"passed":"not-in-case");std::exit(0);
}
void wifiObserve(bool completed){
 if(!wm.enabled||m.terminal)return;if(am.uiProbe)am.uiProbe();
 if(m.app=="wifi_settings"&&m.probe)probeState();
 if(completed&&m.app=="wifi_settings"&&!am.modal&&!am.restoringModal&&m.nativeFrameComplete&&visibleRasterContent()){
  wm.frames=m.frames;wm.completedPixels=m.pixels;
  if(wm.state[2]==3){wifiCapture("scan-list");if(wm.message=="Unsupported network security"){wm.unsupportedFrame=true;wifiCapture("unsupported-auth");}if(wm.message=="Hidden network: enter name manually"){wm.hiddenFrame=true;wifiCapture("hidden-ssid");}}
  if(wm.state[2]==2&&!wm.editor.empty()){if(wifiGapFreshKey&&wm.editor==wifiGapEditor+"a")wifiCapture("gap-fresh-key-masked");assert(wm.entry==std::string(wm.editor.size(),'*'));wifiCapture("masked-password");wifiCapture(("keyboard-page-"+std::to_string(wm.state[4])).c_str());}
 }
 if(wm.exited&&m.app=="default"&&am.homeReady&&m.appVisible&&m.nativeFrameComplete&&visibleRasterContent()){if(m.caseIndex==42){assert(wm.finished&&!wm.homeReturned);wm.homeReturned=true;wm.exited=false;wm.finished=false;wm.step=8;}else wifiPass();}
}
/* Rect IDs: Back, Prev, Select, Next, 8 list rows, 35 keyboard keys.
 * Coordinates come from the production layout with actual content dimensions.
 * Provider safe insets are applied exactly once, never a logical240 scale. */
static void wifiTap(unsigned rect,bool&down,unsigned&x,unsigned&y){
 if(wifiGapInjected&&!wifiGapFreshTap){assert(wifiGapNeutral&&wm.editor==wifiGapEditor&&wm.ssid==wifiGapSsid&&wm.password==wifiGapPassword&&wm.state[2]==2&&rect==13&&!wm.writes);wifiGapFreshTap=true;}
 assert(rect<47);const unsigned*r=wm.rects[rect];assert(r[2]&&r[3]&&r[0]+r[2]<=wm.state[0]&&r[1]+r[3]<=wm.state[1]);
 wm.tx=(466-wm.state[0])/2+r[0]+r[2]/2;wm.ty=(466-wm.state[1])/2+r[1]+r[3]/2;
 printf("WIFI_INPUT step=%u page=%u choice=%u armed=%u rect=%u x=%u y=%u message=%s\n",wm.step,wm.state[2],wm.state[3],wm.state[11],rect,wm.tx,wm.ty,wm.message.c_str());
 wm.downUntil=m.ms+160;wm.nextAt=m.ms+650;down=true;x=wm.tx;y=wm.ty;
}
static bool wifiRow(unsigned row,bool&down,unsigned&x,unsigned&y){
 unsigned first=wm.state[3]/wm.state[5]*wm.state[5];
 if(row>=first&&row<first+wm.state[5]){wifiTap(4+row-first,down,x,y);return true;}
 wifiTap(row<first?1:3,down,x,y);return false;
}
bool wifiTouch(bool&down,unsigned&x,unsigned&y){
 down=false;if(am.modal||am.restoringModal)return false;if(m.probe)probeState();
 if(m.ms<wm.downUntil){down=true;x=wm.tx;y=wm.ty;return true;}if(m.ms<wm.nextAt||!wm.state[0]||!wm.state[11])return false;
 if(wm.finished){
  if(m.caseIndex==45)return false;
  if(m.caseIndex==43&&m.lightEntries<3)return false;
  if(m.caseIndex==43&&wm.substep)assert(wm.state[2]==0&&wm.editor.empty()&&wm.password.empty()&&wm.ssid=="a!A"&&!wm.writes);
  if(m.caseIndex==43&&!wm.substep++){assert(wifiDraft()==wm.snapshot&&!wm.writes&&m.restores==3);wifiCapture("three-light-draft");}
  wifiTap(0,down,x,y);return true;
 }
 if(m.caseIndex==42){
  switch(wm.step){
   case 0:assert(wm.state[2]==0);if(!wifiRow(3,down,x,y))return down;break;
   case 1:if(wm.state[6])return false;assert(wm.state[2]==3&&wm.state[10]==3);if(!wifiRow(0,down,x,y))return down;break;
   case 2:assert(wm.unsupportedFrame&&wm.ssid.empty());if(!wifiRow(1,down,x,y))return down;break;
   case 3:assert(wm.hiddenFrame&&wm.ssid.empty());if(!wifiRow(2,down,x,y))return down;break;
   case 4:assert(wm.state[2]==2&&wm.ssid=="Modeled WPA2 network"&&wm.password.empty()&&wm.state[9]);wifiTap(0,down,x,y);break;
   case 5:assert(wm.state[2]==0&&wm.editor.empty());if(!wifiRow(3,down,x,y))return down;break;
   case 6:assert(wm.state[2]==3);wifiTap(0,down,x,y);break;
   case 7:assert(wm.state[2]==0&&!wm.state[6]&&!wm.state[8]&&!wm.writes);wm.finished=true;return false;
   case 8:assert(wm.homeReturned&&wm.state[2]==0&&wm.ssid.empty());if(!wifiRow(3,down,x,y))return down;break;
   case 9:if(wm.state[6])return false;assert(wm.state[2]==3&&wm.scanStarts==3);wm.finished=true;wifiTap(0,down,x,y);break;
   default:assert(false);
  }++wm.step;return down;
 }
 /* Edit SSID on every keyboard page, then enter an eight-character password.
  * The final Page/Delete sequence also proves password masking on all pages. */
 static const int actions[]={100,1,32,1,32,1,34,101,1,1,1,1,1,1,1,1,32,1,32,1,33};
 if(wm.step<sizeof(actions)/sizeof(actions[0])){
  int a=actions[wm.step];if(a>=100){assert(wm.state[2]==0);if(!wifiRow(a-100,down,x,y))return down;}
  else{assert(wm.state[2]==(wm.step<7?1u:2u));wifiTap(12+a,down,x,y);}++wm.step;return down;
 }
 assert(wm.pageMask==7);if(wm.step<sizeof(actions)/sizeof(actions[0])+7)assert(wm.ssid=="a!A");
 if(m.caseIndex==43||m.caseIndex==45){assert(wm.editor=="aaaaaaaa!"&&wm.password.empty()&&wm.state[9]&&!wm.writes);wm.snapshot=wifiDraft();wm.finished=true;return false;}
 unsigned phase=wm.step-unsigned(sizeof(actions)/sizeof(actions[0]));
 switch(phase){
  case 0:assert(wm.editor=="aaaaaaaa!");wifiTap(12+34,down,x,y);break;
  case 1:assert(wm.password=="aaaaaaaa!"&&wm.editor.empty()&&wm.state[9]&&!wm.writes);wm.writeAuthority=1;if(!wifiRow(6,down,x,y))return down;break;
  case 2:assert(!wm.state[9]&&wm.writes==3);wm.writeAuthority=0;wm.savedWrites=wm.writes;
   assert(values.at("6:wifi.commit").at(5)==1&&values.at("6:wifi.a0").size()==64&&values.at("6:wifi.a1").size()==64);
   assert(!memcmp(values.at("6:wifi.a0").data()+12,"a!A",4)&&!memcmp(values.at("6:wifi.a0").data()+44,"aaaaaaaa!",10));if(!wifiRow(4,down,x,y))return down;break;
  case 3:if(wm.state[7]||wm.state[8])return false;assert(wm.joins==1);if(!wifiRow(7,down,x,y))return down;break;
  case 4:assert(wm.state[2]==4);if(!wifiRow(0,down,x,y))return down;break;
  case 5:assert(wm.state[2]==0&&wm.ssid=="a!A"&&wm.writes==wm.savedWrites);if(!wifiRow(7,down,x,y))return down;break;
  case 6:assert(wm.state[2]==4);wm.writeAuthority=2;if(!wifiRow(1,down,x,y))return down;break;
  case 7:assert(wm.state[2]==0&&wm.ssid.empty()&&wm.password.empty()&&!wm.state[9]);wm.writeAuthority=0;
   assert(values.at("6:wifi.commit").at(5)==0);for(const char*k:{"6:wifi.a0","6:wifi.a1","6:wifi.b0","6:wifi.b1"})assert(values.at(k)==std::vector<uint8_t>(64,0));wm.finished=true;return false;
  default:assert(false);
 }++wm.step;return down;
}
bool wifiLight(uint32_t*cause){
 assert(wm.enabled&&(m.caseIndex==43||m.caseIndex==45)&&wm.finished&&!wm.exited);
 assert(!m.display&&!m.hold&&m.nativeToken);
 assert(m.caseIndex==43?!m.pins[2]:(!m.pins[2]||m.touchSleeping));
 assert(am.resumeAttempts==m.lightEntries&&am.resumeSuccesses==m.lightEntries);
 memcpy(am.lightTicket,am.issuedTicket,sizeof(am.lightTicket));if(m.probe)probeState();assert(wifiDraft()==wm.snapshot&&!wm.writes);if(m.caseIndex==43)assert(!m.kvWrites);
 if(am.uiProbe)am.uiProbe();assert(!am.touchGrant&&!am.touchSubscription);
 if(m.caseIndex==43){assert(m.lightEntries<3&&!am.timer&&!am.saved);m.ms+=7000;++am.gpioWakes;*cause=RISC_LIGHT_SLEEP_WAKE_GPIO;}
 else {assert(am.saved&&am.timer);m.ms+=am.timer;++am.timerWakes;*cause=RISC_LIGHT_SLEEP_WAKE_TIMER;}
 ++m.lightEntries;m.wokeAt=m.ms;m.keySent=false;m.levels[3]=false;m.restoreExpected=true;wm.nextAt=m.ms+1800;return true;
}

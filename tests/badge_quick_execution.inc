/* Actual installed Home Quick controller. Geometry/state probes are read-only;
 * every happy-path edit is an injected physical touch, never an app assignment. */
#ifdef BADGE_QUICK_ACTIONS
void quickInitialize(){
 if(m.caseIndex!=34&&m.caseIndex!=36)return;
 // Explicit pre-existing user preference medium; never a fabricated Save.
 std::vector<uint8_t> zone(44);zone[0]='T';zone[1]='Z';zone[2]=1;memcpy(zone.data()+4,"America/Denver",14);zone[3]=0xa5;
 for(unsigned i=0;i<44;i++)if(i!=3)zone[3]^=zone[i];
 values["1:time_zone"]=zone;values["1:time_format"]={0x54,1,1,0xa4};
 for(const char*k:{"sleep_mode","time_format","time_zone"})qm.preservedPreferences[k]=values[std::string("1:")+k];
}
extern "C" void badge_quick_preference_call(){if(qm.enabled)assert(!m.terminal);}
extern "C" void badge_quick_preempt_observe(int visible,int capture,unsigned pending,unsigned generation){
 if(!qm.enabled||m.caseIndex!=40)return;
 assert(generation&&visible&&!pending&&qm.framesAtOpen&&!m.lightEntries);qm.preemptedOpen=true;qm.preemptedCapture=capture;
}
static unsigned quickStored(const char*key,unsigned missing=999){auto i=values.find(std::string("1:")+key);if(i==values.end())return missing;assert(i->second.size()==1);return i->second[0];}
extern "C" void badge_quick_home_observe(const int*state,const int*rects){
 if(!qm.enabled)return;for(unsigned i=0;i<24;i++)qm.ui[i]=unsigned(state[i]);
 for(unsigned i=0;i<10;i++)for(unsigned j=0;j<4;j++)qm.rects[i][j]=unsigned(rects[i*4+j]);
 if(qm.ui[0]&&qm.ui[2]==466u*256u){qm.opened=true;assert(qm.ui[10]&&!qm.ui[11]&&!qm.ui[12]&&!qm.ui[13]&&!qm.ui[14]);
  for(unsigned i=0;i<10;i++){const auto*r=qm.rects[i];if(!r[2]||!r[3])continue;assert(r[0]+r[2]<=466&&r[1]+r[3]<=466);
   for(unsigned a=0;a<2;a++)for(unsigned b=0;b<2;b++){int x=int(r[0]+(a?r[2]-1:0))*2+1-466,y=int(r[1]+(b?r[3]-1:0))*2+1-466;assert(x*x+y*y<=466*466);}
  }
 }
 if(qm.ui[23])qm.pendingCancelled=true;
 if(qm.ui[6])qm.torchSeen=true;if(qm.ui[4]!=qm.ui[5])qm.previewSeen=true;
}
void quickKvRead(uint32_t ns,const char*k,const void*b,uint32_t n){
 if(!qm.enabled)return;assert(!m.terminal);
 if((m.caseIndex==34||m.caseIndex==36)&&ns==1){
  auto expected=qm.preservedPreferences.find(k);if(expected!=qm.preservedPreferences.end()){
   assert(n==expected->second.size()&&!memcmp(b,expected->second.data(),n));
   unsigned bit=!strcmp(k,"sleep_mode")?1:!strcmp(k,"time_format")?2:4;
   if(m.app=="settings"){qm.settingsPreferenceReads|=bit;qm.settingsRead=qm.settingsPreferenceReads==7;assert(quickStored("brightness")==unsigned(m.caseIndex==36?60:80));}
   if(m.app=="default"&&m.visits==4)qm.homePreferenceReads|=bit;
  }
 }
}
extern "C" void badge_quick_pending_cancel_observe(int cancelled){
 if(!qm.enabled)return;assert(!m.terminal);
 if(m.caseIndex==38){assert(cancelled&&qm.pendingDelay&&qm.pendingGapSent&&!qm.dndSaved);qm.pendingCancelled=true;}
}
extern "C" void badge_quick_submit_observe(){
 if(!qm.enabled)return;assert(!m.terminal);if(am.uiProbe)am.uiProbe();
 assert(qm.ui[22]&&qm.pendingFrame&&m.frames==qm.pendingFrame&&m.pixels==qm.pendingPixels);++qm.pendingSubmits;
}
void quickKvWrite(uint32_t ns,const char*k,const void*b,uint32_t n){
 if(!qm.enabled)return;assert(!m.terminal);
 if(ns==1&&(!strcmp(k,"brightness")||!strcmp(k,"alert_dnd")||!strcmp(k,"quick_radio")))assert(qm.ui[22]&&qm.pendingFrame&&m.frames==qm.pendingFrame&&m.pixels==qm.pendingPixels);
 if(ns==1&&qm.preservedPreferences.count(k))assert(false); // No implicit preference repair/default overwrite.
 if(ns==1&&!strcmp(k,"brightness")){assert(n==1);++qm.writes;if(*(const uint8_t*)b==80)qm.saved=true;}
 if(ns==1&&!strcmp(k,"alert_dnd")){assert(n==1&&*(const uint8_t*)b==1);qm.dndSaved=true;}
 if(ns==1&&!strcmp(k,"quick_radio")){++qm.radioWrites;assert(n==4);const auto*v=static_cast<const uint8_t*>(b);if(!(v[2]&1))qm.radioOff=true;else if(qm.radioOff)qm.radioOn=true;}
}
extern "C" bool badge_quick_capture_observed(const risc_runtime_api_v1*rt,uint32_t actions,bool result){
 if(!qm.enabled||m.caseIndex!=36||qm.stale||!result||!(actions&(1u<<1)))return result;
 // Explicit competing-writer fault, through actual namespace1 raw capability.
 // This is not a direct backend-map edit and never fabricates a successful UI Save.
 risc_runtime_capability_v1 g{sizeof(g)};assert(rt->acquire("storage.key-value",1,1,&g));
 auto*kv=static_cast<const risc_key_value_v1*>(g.api);const uint8_t newer=60;assert(kv&&kv->put(kv->context,"brightness",&newer,1)==0);assert(rt->release(&g));
 qm.stale=true;qm.faultAt=m.ms;printf("MODELED_COMPETING_WRITER actual rawKV1 brightness60 after Quick captured revision\n");return result;
}
extern "C" int badge_quick_collect_result(int rc){
 if(!qm.enabled||m.caseIndex!=37||qm.unknown)return rc;
 assert(rc==0&&qm.saved);qm.unknown=true;m.terminal=true;m.terminalIo=m.io;
 printf("MODELED_PROVIDER_FAULT collect99 after actual terminal collection; client must retain\n");return 99;
}
static void quickRaster(const char*name){
 const char*dir=getenv("BADGE_PROTOTYPE_RASTERS");if(!dir||!*dir)return;
 std::string path=std::string(dir)+"/"+std::to_string(m.caseIndex)+"-quick-"+name+".ppm";FILE*f=fopen(path.c_str(),"wb");assert(f);fprintf(f,"P6\n466 466\n255\n");
 for(uint16_t p:m.pixels){unsigned char b[]={uint8_t(((p>>11)&31)*255/31),uint8_t(((p>>5)&63)*255/63),uint8_t((p&31)*255/31)};assert(fwrite(b,1,3,f)==3);}assert(!fclose(f));
}
void quickPass(const char*detail){
 if((m.caseIndex==35||m.caseIndex==40)&&(am.modal||m.frames!=am.postAckFrame||m.frames<=am.ackedAtFrame||m.pixels!=am.postAckPixels||m.pixels==am.submittedModalPixels))return; // Require the exact observed completed restoration, never a stale or partial modal.
 if(m.caseIndex==40&&qm.ui[1])return; // Require real post-modal neutral before final qualification.
 assert(qm.enabled&&(qm.opened||(m.caseIndex==39&&am.reset)));
 assert(am.resumeAttempts==m.lightEntries&&am.resumeSuccesses==m.lightEntries);
 if(m.caseIndex==34||m.caseIndex==36){assert(qm.settingsPreferenceReads==7&&qm.homePreferenceReads==7);for(auto&e:qm.preservedPreferences)assert(values["1:"+e.first]==e.second);}if(m.caseIndex!=37)assert(!m.terminal&&!qm.ui[15]);
 if(m.caseIndex==34)assert(qm.saved&&qm.settingsRead&&m.visits==4&&quickStored("brightness")==80&&qm.ui[5]==80);
 if(m.caseIndex==35){assert(qm.dndSaved&&quickStored("alert_dnd")==1&&am.saved&&am.acked&&am.visible&&am.timerWakes);assert(alarmOccurrence().silenced);}
 if(m.caseIndex==36)assert(qm.stale&&qm.settingsRead&&qm.saved&&m.visits==4&&quickStored("brightness")==80);
 if(m.caseIndex==37)assert(qm.ui[22]&&qm.pendingSubmits&&m.frames==qm.pendingFrame&&m.pixels==qm.pendingPixels);
 if(m.caseIndex==38)assert(qm.pendingDelay&&qm.pendingGapSent&&qm.pendingCancelled&&!qm.pendingSubmits&&!qm.dndSaved&&qm.previewSeen&&qm.torchSeen&&m.lightEntries==3&&quickStored("brightness")==999&&!qm.ui[6]&&qm.ui[5]==40&&m.level==102);
 if(m.caseIndex==39)assert(am.reset&&am.deepResets==1&&!m.seedCalls&&quickStored("brightness")==80&&qm.ui[5]==80);
 if(m.caseIndex==40)assert(am.saved&&am.acked&&am.visible&&!m.lightEntries&&qm.framesAtOpen&&qm.preemptedOpen&&!qm.ui[0]&&!qm.ui[1]&&!qm.ui[6]&&!qm.ui[20]&&!qm.ui[21]&&!qm.writes&&qm.ui[5]==40&&m.level==102);
 if(m.caseIndex==41){assert(values["1:quick_radio"]==std::vector<uint8_t>({0x51,1,1,0xa4}));assert(m.visits==1);} 
 if(m.caseIndex==41)fprintf(stderr,"QUICK_RADIO off=%u on=%u cancel=%u writes=%u leaves=%u joins=%u idle=%u\n",qm.radioOff,qm.radioOn,qm.cancelChecks,qm.radioWrites,qm.radioLeaves,qm.radioJoins,qm.radioIdleChecks);
 if(m.caseIndex==41)assert(qm.radioOff&&qm.radioOn&&qm.cancelChecks==3&&qm.radioWrites==2&&qm.radioIdleChecks&&!qm.radioJoins);
 quickRaster("passed");printf("PASS actual Runtime quick actions case=%u %s actual_UI_only=1 namespace1_shared=1 broker_copied_sync=1 physical_unverified\n",m.caseIndex,detail);std::exit(0);
}
void quickObserve(bool completed){
 if(!qm.enabled)return;if(am.uiProbe)am.uiProbe();
 if(m.app=="default"&&am.homeReady&&!qm.start)qm.start=m.ms;
 if(m.caseIndex==41&&m.app=="default"&&am.homeReady){uint64_t age=m.ms-am.appReadyAt;
  if(age>=1800&&age<1900){assert(!qm.ui[0]&&!qm.opened&&!qm.writes&&!qm.radioWrites&&m.visits==1);qm.cancelChecks|=1;}
  if(age>=7200&&age<7300){assert(!qm.ui[1]&&qm.radioWrites==2&&qm.ui[10]&&!qm.ui[11]);qm.cancelChecks|=2;}
 }
 if(qm.stale&&!qm.saved&&m.app=="default"&&m.visits==1&&m.ms>qm.faultAt+200){assert(quickStored("brightness")==60);assert(qm.ui[9]&4);}
 if(completed&&m.app=="default"&&m.level&&qm.ui[22]){qm.pendingFrame=m.frames;qm.pendingPixels=m.pixels;quickRaster("pending-before-save");}
 if(completed&&m.app=="default"&&m.level&&qm.ui[2]==466u*256u){qm.framesAtOpen=m.frames;quickRaster("open");}
 if(m.app=="default"&&m.visits==4&&am.homeReady&&!qm.ui[0]&&m.level){
  if(m.caseIndex==34&&qm.settingsRead&&qm.homePreferenceReads==7)quickPass("brightness Save -> actual Settings reads unchanged shared sleep preference -> fresh Home same brightness");
  if(m.caseIndex==36&&qm.saved&&qm.homePreferenceReads==7)quickPass("competing raw writer invalidates captured revision; fresh Home retry saves");
 }
 if(m.caseIndex==39&&am.reset&&am.homeReady&&m.level){quickPass("actual brightness Save -> fresh-process Deep -> same preference/no RTC reseed");}
 if(m.caseIndex==38&&m.lightEntries>=3&&m.level&&m.ms-m.wokeAt>1200)quickPass("cancelled preview/Torch restore and three returning Home Light cycles");
 if(m.caseIndex==41&&qm.radioOff&&qm.radioOn&&m.ms-am.appReadyAt>10000&&!qm.ui[0])quickPass("edge replay/gap neutral recovery; supported WiFi independently confirmed");
 if(m.caseIndex==35||m.caseIndex==40){qm.completedFrame=completed;qm.inAlarm=true;alarmObserve(completed);qm.inAlarm=false;qm.completedFrame=false;if(completed&&am.visible&&am.modal)quickRaster("alarm-preempted");}
}
unsigned quickCatalog(){return (m.caseIndex==35||m.caseIndex==40)?8:0;}
static void quickTap(unsigned rect,bool&down,unsigned&x,unsigned&y){const auto*r=qm.rects[rect];assert(r[2]&&r[3]);down=true;x=r[0]+r[2]/2;y=r[1]+r[3]/2;}
bool quickTouch(uint64_t age,bool&down,unsigned&x,unsigned&y){
 bool inheritedDown=down;down=false;if(am.modal)return false;
 if(m.app=="countdown"&&m.caseIndex==40){
  if(am.saved)return false;if(am.uiProbe)am.uiProbe();
#ifdef BADGE_ALARM_NATIVE_GEOMETRY
  const unsigned control[]={1,6,8,8,8,8,0,0,2};const unsigned page[]={0,1,2,2,2,2,2,1,0};
  for(unsigned i=0;i<9;i++){uint64_t at=1100+i*600;if(age>=at&&age<at+160){assert(am.uiPage==page[i]);const int*r=am.nativeRects[control[i]];assert(r[2]>0&&r[3]>0);down=true;x=69+r[0]+r[2]/2;y=69+r[1]+r[3]/2;}}
#else
  // Generic selected controller: four real minute-minus taps, then Start.
  assert(am.uiWidth&&am.uiHeight);
  for(unsigned i=0;i<5;i++){uint64_t at=1100+i*600;if(age>=at&&age<at+160){down=true;x=(466-am.uiWidth)/2+(i<4?am.uiWidth/2:am.uiWidth/4);y=(466-am.uiHeight)/2+(i<4?118:am.uiHeight-45);}}
#endif
  return down;
 }
 if(m.app=="countdown") {qm.inAlarm=true;alarmTouch(age,down,x,y);qm.inAlarm=false;return down;}
 if(m.app=="springboard"){
  if(quickCatalog()==8&&age>=1200&&age<1360){down=inheritedDown;return down;}
  if(quickCatalog()==8&&age>=2300&&age<2460){down=true;x=y=233;}
  else if(quickCatalog()==0&&age>=1200&&age<1360){down=true;x=y=233;}
  return down;
 }
 if(m.app!="default"||!am.homeReady)return false;
 age=m.ms-am.appReadyAt;
 if(m.caseIndex==40&&!am.saved){down=age>=1600&&age<1900;x=age<1750?310:100;y=179;return down;}
 if(m.caseIndex==35&&qm.dndSaved&&!am.saved&&age>=5600){down=age<5900;x=age<5750?310:100;y=179;return down;}
 if((m.caseIndex==34||m.caseIndex==36)&&m.visits==1&&age>=5600){down=age<5900;x=age<5750?310:100;y=179;return down;}
 if((m.caseIndex==34&&m.visits==4)||(m.caseIndex==39&&am.reset)||(m.caseIndex==35&&am.saved)||m.lightEntries)return false;
 if(am.uiProbe)am.uiProbe();
 unsigned opening=m.caseIndex==41?2000:(m.caseIndex==38&&qm.pendingCancelled?5000:1100);
 // Case41 first supplies an ordinary top-edge tap, not a downward drag.
 if(m.caseIndex==41&&age>=1100&&age<1260){down=true;x=233;y=4;return true;}
 if(age>=opening&&age<opening+500){down=true;x=233;y=age<opening+100?4:4+unsigned((age-opening-100)*280/400);return true;}
 if(m.caseIndex==40&&am.saved){if(age>2000&&age%4000<160){down=true;x=233;y=5;}return down;}
 if(!qm.opened)return false;
 if(m.caseIndex==35){if(age>=2600&&age<2760)quickTap(3,down,x,y);}
 else if(m.caseIndex==38){
  if(age>=2400&&age<2800){down=true;x=qm.ui[16]+(qm.ui[17]-qm.ui[16])*7/9;y=qm.ui[18];}
  if(age>=3400&&age<3560)quickTap(3,down,x,y); // Fast DND tap; delayed pending presentation is cancelled by real CST gap.
  if(age>=6400&&age<6560)quickTap(7,down,x,y);
  if(age>=7000&&age<7160){down=true;x=y=233;}
 }else if(m.caseIndex==41){
  if(age>=3600&&age<3760)quickTap(5,down,x,y);
  if(age>=4600&&age<4760)quickTap(5,down,x,y);
  if(age>=5600&&age<5760)quickTap(6,down,x,y); // Unsupported Bluetooth must not mutate policy.
  if(age>=6600&&age<6900){down=true;x=233;y=5;}
 }else if(age>=2600&&age<2860){down=true;x=qm.ui[16]+(qm.ui[17]-qm.ui[16])*7/9;y=qm.ui[18];}
 unsigned closing=m.caseIndex==41?8000:m.caseIndex==38?7600:4600;
 if(age>=closing&&age<closing+160)quickTap(9,down,x,y);
 return down;
}
void quickKeys(uint8_t reg){
 if(reg!=0x49)return;
 if(am.visible&&!am.dismissKey){if(m.ms-am.visibleAt>=2000&&m.ms-am.visibleAt<2200)m.levels[3]=true;if(m.ms-am.visibleAt>=2200){m.levels[3]=false;m.pmic[0x49]|=8;am.dismissKey=true;}return;}
 if(am.modal||am.acked)return; // No synthetic second user press after final dismissal.
 uint64_t age=m.ms-am.appReadyAt;
 if(((m.app=="countdown"&&am.saved)||m.app=="settings")&&!m.keySent&&m.ms-m.entered>=3000){m.pmic[0x49]|=4;m.keySent=true;return;}
 bool sleep=(m.caseIndex==35&&am.saved)||(m.caseIndex==39&&qm.saved&&!am.reset)||(m.caseIndex==38&&(m.lightEntries?m.ms-m.wokeAt>1500:age>9000));
 if(m.app=="default"&&sleep&&!m.keySent&&age>3000){if(!qm.actionAt){qm.actionAt=m.ms;m.levels[3]=true;}else if(m.ms-qm.actionAt>=200){m.levels[3]=false;m.pmic[0x49]|=8;m.keySent=true;qm.actionAt=0;}}
}
bool quickLight(uint32_t*cause){
 assert(m.caseIndex==38&&!m.terminal&&!m.display&&!m.hold&&!am.timer);
 assert(am.resumeAttempts==m.lightEntries&&am.resumeSuccesses==m.lightEntries);memcpy(am.lightTicket,am.issuedTicket,sizeof(am.lightTicket));
 m.ms+=7000;++m.lightEntries;++am.gpioWakes;m.wokeAt=m.ms;m.keySent=false;m.levels[3]=false;*cause=RISC_LIGHT_SLEEP_WAKE_GPIO;return true;
}
#else
void quickInitialize(){}void quickObserve(bool){}bool quickTouch(uint64_t,bool&,unsigned&,unsigned&){return false;}void quickKeys(uint8_t){}
unsigned quickCatalog(){return 0;}bool quickLight(uint32_t*){return false;}void quickPass(const char*){assert(false);}
void quickKvRead(uint32_t,const char*,const void*,uint32_t){}void quickKvWrite(uint32_t,const char*,const void*,uint32_t){}
#endif

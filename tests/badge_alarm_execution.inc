// Test-only explicit selected foreground app; legacy callers stay Calculator.
static const char* alarmForegroundApp(){
 const char*n=getenv("BADGE_ALARM_FOREGROUND_APP");
 if(!n)return "calculator";
 assert(!strcmp(n,"calculator")||!strcmp(n,"battery"));return n;
}
static unsigned alarmForegroundCatalog(){
 const char*n=getenv("BADGE_ALARM_FOREGROUND_INDEX");
 if(!n){assert(!strcmp(alarmForegroundApp(),"calculator"));return 2;}
 char*end=nullptr;unsigned long index=strtoul(n,&end,10);assert(end&&end!=n&&!*end&&index<32);return (unsigned)index;
}
bool alarmForegroundTicketCase(){
#ifdef BADGE_MIXED_POINTS_PRODUCT
 return !am.enabled&&selectedForeground();
#else
 return false;
#endif
}
void alarmForegroundTimerArm(uint32_t ms){
#ifdef BADGE_MIXED_POINTS_PRODUCT
 assert(alarmForegroundTicketCase()&&m.nativeToken&&!am.timer);
 // No invented schedule: use the same shared default records and calendar
 // projection with identity UTC, only while no explicit config/zone exists.
 assert(values.count("5:points_utc_cfg")==0&&values.count("1:time_zone")==0);
 points_config config=points_default_config();uint32_t day=0,expected=0;
 assert(points_local_day(am.ticketSeconds,&day));
 for(unsigned slot=0;slot<POINTS_MAX;slot++)for(unsigned edge=0;edge<POINTS_EDGE_COUNT;edge++){
  if(!points_notice_enabled(&config.points[slot],edge))continue;
  for(unsigned offset=0;offset<=8;offset++){
   points_event event={};unsigned parent=(day>1?day-1:day)+offset;
   if(points_event_for_day(&config,slot,parent,edge,&event,nullptr)&&event.deadline>am.ticketSeconds&&(!expected||event.deadline<expected))expected=event.deadline;
  }
 }
 assert(expected&&am.ticketDeadline==expected&&expected>am.ticketSeconds);
 uint32_t delta=expected-am.ticketSeconds;
 assert(ms==(delta>86400?86400000:delta>1?(delta-1)*1000:1));
 timeval wall{};wallRead(&wall,nullptr);
 int64_t remaining=((int64_t)expected+946684800-wall.tv_sec)*1000-wall.tv_usec/1000;
 assert((int64_t)ms<=remaining+250);
 printf("FOREGROUND_POINTS_TIMER app=%s deadline=%u duration=%u shared_default=1 early_GPIO_expected=1\n",m.app.c_str(),expected,ms);
#else
 (void)ms;assert(false);
#endif
}
void alarmKvWritten(uint32_t ns,const char*k,const void*b,uint32_t n){
#ifdef BADGE_NATIVE_ALARMS
 if(!am.enabled||ns!=4||strcmp(k,am.kind==1?"alarm_utc_occ":"timer_utc_occ"))return;
 alarm_occurrence occ{};assert(alarm_occurrence_decode(&occ,static_cast<const uint8_t*>(b),n,am.kind));
 if(occ.state==ALARM_OCC_ACKED&&!am.ackWriteObserved){am.ackWriteObserved=true;am.ackWriteFrame=m.frames;}
#else
 (void)ns;(void)k;(void)b;(void)n;
#endif
}
/* Additive actual-product alarm fixture. Only SDK/medium/input are modeled;
 * schedules are written exclusively by production app Save controllers. */
extern "C" void badge_alarm_touch_observe(int grant,int subscription){am.touchGrant=grant;am.touchSubscription=subscription;}
extern "C" void badge_alarm_restore_phase(int active){assert(!m.terminal);am.restoringModal=active!=0;}
extern "C" void badge_alarm_service_call(){assert(!m.terminal);}
extern "C" void badge_alarm_ui_register(void(*probe)()){am.uiProbe=probe;}
extern "C" void badge_alarm_home_observe(int modal,unsigned generation,unsigned state,int error){am.modal=modal;am.generation=generation;am.state=state;am.error=error;}
extern "C" void badge_alarm_app_observe(unsigned kind,unsigned a,unsigned b,unsigned c,unsigned revision,unsigned deadline,int editing,int retained){
 am.uiKind=kind;am.draft[0]=a;am.draft[1]=b;am.draft[2]=c;am.revision=revision;am.deadline=deadline;am.editing=editing;am.retained=retained;
}
#ifdef BADGE_NATIVE_ALARMS
extern "C" void badge_alarm_generic_geometry_observe(unsigned w,unsigned h){assert(w>32&&h>140&&w<=466&&h<=466);am.uiWidth=w;am.uiHeight=h;}
#ifdef BADGE_ALARM_NATIVE_GEOMETRY
extern "C" void badge_alarm_geometry_observe(unsigned w,unsigned h,unsigned page,unsigned field,const int*rects){
 assert(w==328&&h==328&&page<=2&&field<3);am.uiWidth=w;am.uiHeight=h;am.uiPage=page;am.uiField=field;memcpy(am.nativeRects,rects,sizeof(am.nativeRects));
}
#endif
extern "C" void badge_alarm_ticket_fault(void*raw,int rc){
 if((am.enabled||alarmForegroundTicketCase())&&rc==0){auto*t=static_cast<alarm_sleep_v1*>(raw);am.ticketSeconds=t->rtc_seconds;am.ticketDeadline=t->deadline;static_assert(sizeof(*t)==sizeof(am.issuedTicket),"ticket fixture size");memcpy(am.issuedTicket,t,sizeof(*t));}
 if(am.enabled&&m.caseIndex==29&&m.app==alarmForegroundApp()&&rc==0&&!am.ticketFaults){auto*t=static_cast<alarm_sleep_v1*>(raw);assert(t->snapshot>1);--t->snapshot;++am.ticketFaults;am.faultTransaction=m.transactions;assert(m.nativeToken&&am.faultTransaction);printf("MODELED_SERVICE_FAULT stale snapshot after actual prepare_sleep\n");}
}
extern "C" void badge_alarm_resume_observe(const void*raw,int phase){
 if(!am.enabled&&!alarmForegroundTicketCase())return;assert(!m.terminal&&raw&&m.lightEntries);
 assert(!memcmp(raw,am.lightTicket,sizeof(am.lightTicket)));
 if(!phase){assert(am.resumeAttempts+1==m.lightEntries&&am.resumeSuccesses==am.resumeAttempts);++am.resumeAttempts;}
 else {assert(am.resumeAttempts==m.lightEntries&&am.resumeSuccesses+1==am.resumeAttempts);++am.resumeSuccesses;}
}
static unsigned clockEditWord(unsigned offset){
 auto it=values.find("4:clock_edit_auth");assert(it!=values.end()&&it->second.size()==316&&!memcmp(it->second.data(),"ACE2",4));
 uint32_t sum=2166136261u;for(unsigned i=0;i<312;i++)sum=(sum^it->second[i])*16777619u;
 assert(sum==alarm_read32(it->second.data()+312));assert(offset<=312&&offset%4==0);return alarm_read32(it->second.data()+offset);
}
static void alarmPersistReset(bool deep);
extern "C" void badge_clock_edit_begin_result(int rc){
 if(!am.enabled||m.caseIndex!=33)return;assert(rc==ALARM_OK&&m.app=="settings");++am.editBegins;
 assert(clockEditWord(8)==1); // Actual durable INTENT, not a synthesized ledger.
 if(!am.reset){assert(m.seedCalls==1&&!editedSeed&&!am.saved&&clockEditWord(12)==1);alarmPersistReset(false);assert(false);}
 assert(am.pendingSeen&&am.pendingDismissals&&clockEditWord(12)>am.intentGeneration);
}
static alarm_config alarmSaved(){
 const char*key=am.kind==1?"3:alarm_utc_cfg":"3:timer_utc_cfg";
 auto it=values.find(key);alarm_config cfg{};
 if(it!=values.end())assert(alarm_config_decode(&cfg,it->second.data(),it->second.size(),am.kind));
 return cfg;
}
static alarm_occurrence alarmOccurrence(){
 const char*key=am.kind==1?"4:alarm_utc_occ":"4:timer_utc_occ";
 auto it=values.find(key);alarm_occurrence occ{};
 if(it!=values.end())assert(alarm_occurrence_decode(&occ,it->second.data(),it->second.size(),am.kind));
 return occ;
}
static void alarmPass(const char*detail){
#ifdef BADGE_WIFI_EXECUTION
 if(wm.enabled){wifiPass();return;}
#endif
 if(qm.enabled){quickPass(detail);return;}
 assert(am.resumeAttempts==m.lightEntries&&am.resumeSuccesses==m.lightEntries);
 if(!am.reset)assert(m.seedCalls==(m.caseIndex==28?2u:1u));
 if(am.acked){
  const char*dir=getenv("BADGE_PROTOTYPE_RASTERS");if(dir&&*dir){
   std::string path=std::string(dir)+"/"+std::to_string(m.caseIndex)+"-"+m.app+"-alarm-dismissed.ppm";
   FILE*f=fopen(path.c_str(),"wb");assert(f);fprintf(f,"P6\n466 466\n255\n");
   for(uint16_t pixel:m.pixels){unsigned char c[]={uint8_t(((pixel>>11)&31)*255/31),uint8_t(((pixel>>5)&63)*255/63),uint8_t((pixel&31)*255/31)};assert(fwrite(c,1,3,f)==3);}assert(!fclose(f));
  }
 }
 assert(!m.terminal&&!am.retained);printf("PASS actual Runtime alarm case=%u %s config_from_actual_Save=%u timer_wakes=%u gpio_wakes=%u exact_resume_successes=%u fresh_process_deep=%u visible=%u dismissed=%u Points_user_edit_and_occurrence_not_exercised hardware_unverified\n",m.caseIndex,detail,am.saved,am.timerWakes,am.gpioWakes,am.resumeSuccesses,am.deepResets,am.visible,am.acked);std::exit(0);
}
void alarmObserve(bool completed){
 if(!am.enabled)return;
#ifdef BADGE_WIFI_EXECUTION
 if(wm.enabled){wifiObserve(completed);if(m.caseIndex!=45)return;}
#endif
 if(qm.enabled&&!qm.inAlarm){quickObserve(completed);return;}
 if(am.uiProbe)am.uiProbe();
 if(completed&&m.caseIndex==28&&m.app=="settings"&&am.modal)assert(am.state!=ALARM_STATE_BLOCKED); // Verified editor Save must not present an RTC-error modal.
 if(m.caseIndex==33&&am.reset){
  assert(!am.generation&&!alarmOccurrence().generation&&!alarmSaved().enabled);
  if(completed&&am.modal&&am.state==ALARM_STATE_BLOCKED&&am.error==ALARM_RTC){
   assert(clockEditWord(8)==1);am.modalFrame=m.frames;am.submittedModalPixels=m.pixels;
  }
  if(!am.pendingActive&&am.modal&&am.state==ALARM_STATE_BLOCKED&&am.error==ALARM_RTC&&m.level&&am.modalFrame==m.frames&&m.pixels==am.submittedModalPixels){
   assert(m.seedCalls==1&&!editedSeed);am.pendingActive=am.pendingSeen=true;am.pendingDismissSent=false;am.pendingAt=m.ms;
   timeval wall{};wallRead(&wall,nullptr);assert(wall.tv_sec<946684800+(int64_t)clockEditWord(16));
   printf("PENDING_EDIT visible app=%s freshprocess=1 marker_INTENT=1 no_autoconfirm=1 UTC_fallback=2026\n",m.app.c_str());
  }
  if(am.pendingActive&&!am.modal&&am.pendingDismissSent){
   assert(clockEditWord(8)==1);++am.pendingDismissals;am.pendingActive=false;am.appReadyAt=m.ms;m.keySent=false;
  }
  if(m.seedCalls==2&&clockEditWord(8)==2){assert(am.editBegins==1&&clockEditWord(12)==am.intentGeneration+1&&editedSeed>=1798761600);am.editCompleted=true;}
  if(am.editCompleted&&m.app=="default"&&m.visits>=4&&!am.modal&&m.appVisible&&am.homeReady){
   assert(am.pendingSeen&&am.pendingDismissals&&!m.lightEntries&&m.seedCalls==2);
   alarmPass("real durable editor INTENT -> software crash cut -> pending error -> fresh explicit Save VERIFIED; no auto-confirm");
  }
 }
 auto cfg=alarmSaved();if(cfg.enabled){am.saved=true;assert(cfg.revision==1);if(am.kind==2&&qm.enabled&&m.caseIndex==40&&cfg.duration!=60)fprintf(stderr,"QUICK_COUNTDOWN saved_duration=%u draft=%u:%u:%u\n",cfg.duration,am.draft[0],am.draft[1],am.draft[2]);if(am.kind==2)assert(cfg.duration==((qm.enabled&&m.caseIndex==40)?60u:300u));}
 if(completed&&am.modal&&!am.restoringModal&&am.generation&&m.display&&m.nativeFrameComplete&&!m.restoring&&visibleRasterContent()){am.modalFrame=m.frames;am.submittedModalPixels=m.pixels;}
 if(am.modal&&am.generation&&m.display&&m.level&&am.modalFrame&&m.frames==am.modalFrame&&m.pixels==am.submittedModalPixels){if(!am.visible){timeval now{};wallRead(&now,nullptr);assert(now.tv_sec>=946684800+(int64_t)cfg.deadline);assert(now.tv_sec<=946684800+(int64_t)cfg.deadline+5);am.alertPixels=m.pixels;am.visibleAt=m.ms;am.visible=true;assert(am.timerWakes||am.deepResets||(qm.enabled&&m.caseIndex==40&&qm.framesAtOpen));}}
 auto occ=alarmOccurrence();
 if(occ.state==ALARM_OCC_ACKED){assert(am.visible&&am.dismissKey&&am.saved&&occ.revision==cfg.revision&&occ.deadline==cfg.deadline);if(!am.acked){assert(am.ackWriteObserved);am.ackedAtFrame=am.ackWriteFrame;}am.acked=true;}
 if(completed&&am.acked&&(!am.modal||am.restoringModal)&&m.display&&m.nativeFrameComplete&&m.frames>am.ackedAtFrame&&visibleRasterContent()){am.postAckFrame=m.frames;am.postAckPixels=m.pixels;}
 if(am.acked&&(m.app!="default"||am.homeReady)&&!am.modal&&!am.restoringModal&&m.display&&m.nativeFrameComplete&&m.level&&am.postAckFrame>am.ackedAtFrame&&m.frames==am.postAckFrame&&m.pixels==am.postAckPixels&&m.pixels!=am.submittedModalPixels){
  if((m.caseIndex==25||m.caseIndex==29)){assert(m.app==alarmForegroundApp()&&m.nativeToken==0&&m.transactions==m.transactionResumes&&am.parkedKv&&am.parkedRealtime);std::string draft;assert(probeState(&draft)==1&&draft==am.draftBefore);}
  if(m.caseIndex==26)assert(am.gpioWakes&&am.timerWakes);
  if(m.caseIndex==27)assert(am.reset&&am.deepResets==1&&m.visits==1&&!m.seedCalls);
  if(m.caseIndex==28)assert(editedSeed>=1798761600&&cfg.created>=1798761600-946684800);
  if(m.caseIndex==29)assert(am.ticketFaults==1);
  alarmPass("durable config -> timed sleep -> visual occurrence -> durable dismissal");
 }
 if((m.caseIndex==31||m.caseIndex==32)&&m.lightEntries>=3&&m.display&&m.level&&m.ms-m.wokeAt>1000){
  std::string text;probeState(&text);assert(!am.saved&&am.editing&&text==am.draftBefore&&!m.nativeToken&&m.transactions==m.transactionResumes);
  alarmPass("unsaved alarm draft preserved across three returning Light cycles");
 }
}
unsigned alarmCatalog(){
#ifdef BADGE_WIFI_EXECUTION
 if(wm.enabled)return m.caseIndex==45&&!am.saved?8:5;
#endif
 if(qm.enabled)return quickCatalog();if(m.caseIndex==33)return 0;if(m.caseIndex==28&&m.seedCalls<2)return 0;if((m.caseIndex==25||m.caseIndex==29)&&am.saved)return alarmForegroundCatalog();return am.kind==1?7:8;}
bool alarmTouch(uint64_t age,bool&down,unsigned&x,unsigned&y){
#ifdef BADGE_WIFI_EXECUTION
 if(wm.enabled&&m.app=="wifi_settings")return wifiTouch(down,x,y);
#endif
 if(qm.enabled&&!qm.inAlarm)return quickTouch(age,down,x,y);
 if(m.caseIndex==33){
  down=false;if(am.modal||am.pendingActive)return false;age=m.ms-am.appReadyAt;
  if(m.app=="default"&&am.homeReady&&!am.editCompleted&&(!am.reset||am.pendingDismissals)){down=age>=1600&&age<1900;x=age<1750?310:100;y=179;}
  if(m.app=="springboard"&&age>=1200&&age<1360){down=true;x=y=233;}
  if(m.app=="settings"){
   if(age>=1100&&age<1260){down=true;badgeSettingsTap(BS_ROOT_TIME,x,y);}
   if(age>=1700&&age<1860){down=true;badgeSettingsTap(BS_YEAR,x,y);}
   if(age>=2300&&age<2460){down=true;badgeSettingsTap(BS_INCREMENT,x,y);}
   if(age>=2900&&age<3060){down=true;badgeSettingsTap(BS_BACK,x,y);}
   if(age>=3500&&age<3660){down=true;badgeSettingsTap(BS_SAVE,x,y);}
  }
  return down;
 }
 if(m.app=="default"){
  down=false;
  if(!am.reset&&(!am.saved||((m.caseIndex==25||m.caseIndex==29||m.caseIndex==45)&&!m.foreground))){down=age>=1600&&age<1900;x=age<1750?310:100;y=179;}
 }else if(m.app=="springboard"&&alarmCatalog()>=7&&age>=2300&&age<2460){down=true;x=y=233;}
 else if(m.app=="alarms"||m.app=="countdown"){
  down=false;
#ifdef BADGE_ALARM_NATIVE_GEOMETRY
  if(am.uiProbe)am.uiProbe();
  const auto nativeTap=[&](unsigned control,unsigned expectedPage,unsigned bit){
   assert(am.uiWidth==328&&am.uiHeight==328);
   if(!am.nativeDraftInitial){memcpy(am.initialDraft,am.draft,sizeof(am.draft));am.nativeDraftInitial=true;}
   if(!(am.nativeInputMask&(1u<<bit))){
    assert(am.uiPage==expectedPage);const int*r=am.nativeRects[control];
    assert(r[0]>=0&&r[1]>=0&&r[2]>0&&r[3]>0&&r[0]+r[2]<=int(am.uiWidth)&&r[1]+r[3]<=int(am.uiHeight));
    am.nativeTouchX=69+r[0]+r[2]/2;am.nativeTouchY=69+r[1]+r[3]/2;am.nativeInputMask|=1u<<bit;
   }
   down=true;x=am.nativeTouchX;y=am.nativeTouchY;
  };
  if(m.caseIndex==31||m.caseIndex==32){
   // Actual nested navigation: edit, field, increment, Back, field, Done,
   // Back to root, reopen a different field. No model assignment or Save.
   const unsigned controls[]={1,5,9,0,5,11,0,1,6};
   const unsigned pages[]={0,1,2,2,1,2,1,0,1};
   if(!m.lightEntries)for(unsigned i=0;i<9;i++){
    const uint64_t start=1100+600*i;
    if(age>=start&&age<start+160)nativeTap(controls[i],pages[i],i);
   }
  }else if(!am.saved&&age>=1100&&age<1260)nativeTap(2,0,0);
#else
  if(am.uiProbe)am.uiProbe();assert(am.uiWidth&&am.uiHeight);
  const auto tap=[&](unsigned lx,unsigned ly){down=true;x=(466-am.uiWidth)/2+lx;y=(466-am.uiHeight)/2+ly;};
  if((m.caseIndex==31||m.caseIndex==32)){if(!m.lightEntries&&age>=1100&&age<1260)tap(am.uiWidth/2,46);}
  else if(!am.saved&&age>=1100&&age<1260)tap(am.uiWidth/4,am.uiHeight-45); // Actual generic START/ARM control.
#endif
 }
 return down;
}
void alarmKeys(uint8_t reg){
 if(qm.enabled&&!qm.inAlarm){quickKeys(reg);return;}
 if(reg!=0x49)return;
#ifdef BADGE_WIFI_EXECUTION
 if(wm.enabled&&m.caseIndex!=45)return;
#endif
 if(m.caseIndex==33){
  if(am.pendingActive&&!am.pendingDismissSent){
   if(m.ms-am.pendingAt>=2000&&m.ms-am.pendingAt<2200)m.levels[3]=true;
   if(m.ms-am.pendingAt>=2200){m.levels[3]=false;m.pmic[0x49]|=8;am.pendingDismissSent=true;}
  }
  if(m.app=="settings"&&am.editCompleted&&!am.modal&&!m.keySent&&m.ms-am.appReadyAt>=5000){m.pmic[0x49]|=4;m.keySent=true;}
  return;
 }
 if(am.visible&&!am.dismissKey){
  // A post-wake physical press must be observed by WAIT_FRESH_PRESS before
  // the PMIC short-release pulse can be a new user action, not a late latch.
  if(m.ms-am.visibleAt>=2000&&m.ms-am.visibleAt<2200)m.levels[3]=true;
  if(m.ms-am.visibleAt>=2200){m.levels[3]=false;m.pmic[0x49]|=8;am.dismissKey=true;return;}
 }
 if(am.modal||am.acked)return; // Dismissal is the final scripted user key; do not invent another sleep press.
 auto age=m.ms-m.entered;
 if((m.app=="alarms"||m.app=="countdown")&&am.saved&&!m.keySent&&age>=2500){m.pmic[0x49]|=4;m.keySent=true;}
 if(m.app=="settings"&&!m.keySent&&age>=5000){m.pmic[0x49]|=4;m.keySent=true;}
 if(m.app=="default"&&am.saved&&m.caseIndex!=25&&m.caseIndex!=29&&m.caseIndex!=45&&!m.keySent&&age>=3000){m.pmic[0x49]|=8;m.keySent=true;}
}
void alarmEnter(const char*n){
#ifdef BADGE_WIFI_EXECUTION
 if(wm.enabled&&!strcmp(n,"wifi_settings")){m.foreground=true;m.foregroundApp=n;m.safeViewport=true;m.nativeSafeContent=true;m.selectedSeedCalls=m.seedCalls;wm.nextAt=m.ms+1200;}
#endif
 assert(m.visits<=12);am.uiKind=0;am.modal=false;am.homeReady=false;am.appReadyAt=m.ms;am.pendingActive=false;am.pendingDismissSent=false;
 if(!strcmp(n,alarmForegroundApp())&&(m.caseIndex==25||m.caseIndex==29)){m.foreground=true;m.foregroundApp=n;m.safeViewport=true;m.nativeSafeContent=true;m.selectedSeedCalls=m.seedCalls;}
 if((!strcmp(n,"alarms")||!strcmp(n,"countdown"))&&(m.caseIndex==31||m.caseIndex==32)){m.foreground=true;m.foregroundApp=n;m.safeViewport=true;m.selectedSeedCalls=m.seedCalls;}
}
void alarmExit(const char*n){
#ifdef BADGE_WIFI_EXECUTION
 if(wm.enabled&&!strcmp(n,"wifi_settings")){wm.exited=true;m.foreground=false;}
#endif
 ++m.appExits;assert(m.appVisible);if(!strcmp(n,"alarms")||!strcmp(n,"countdown"))assert(alarmSaved().enabled);
 if(!qm.enabled&&!strcmp(n,"settings"))assert(m.seedCalls==2&&editedSeed>=1798761600);
 printf("ALARM_APP_EXIT %s\n",n);
}
static void alarmWrite(FILE*f,const void*p,size_t n){assert(fwrite(p,1,n,f)==n);}
static void alarmRead(FILE*f,void*p,size_t n){assert(fread(p,1,n,f)==n);}
void alarmInitialize(const char*exe){
 am.executable=exe;am.kind=(m.caseIndex==24||m.caseIndex==31)?1:2;m.target=0;
 // Explicit modeled pre-existing user sleep preference, never schedule data.
 unsigned mode=(m.caseIndex==27||m.caseIndex==39)?1:0;values["1:sleep_mode"]={0x53,1,(uint8_t)mode,(uint8_t)(mode^0xa5)};
 const char*reset=getenv("BADGE_ALARM_RESET");if(!reset)return;
 assert(m.caseIndex==27||m.caseIndex==33||m.caseIndex==39);am.reset=true;am.deepResets=m.caseIndex!=33;am.saved=m.caseIndex==27;
 FILE*f=fopen(reset,"rb");assert(f);uint32_t reason;alarmRead(f,&reason,sizeof(reason));assert(reason==(m.caseIndex!=33?1u:2u));alarmRead(f,&seeded,sizeof(seeded));alarmRead(f,&RiscCpu::NativeRealtime::stamp,sizeof(RiscCpu::NativeRealtime::stamp));
 uint32_t count;alarmRead(f,&count,sizeof(count));assert(count<100);values.clear();
 for(unsigned i=0;i<count;i++){uint32_t n,z;alarmRead(f,&n,sizeof(n));assert(n<100);std::string k(n,'\0');alarmRead(f,k.data(),n);alarmRead(f,&z,sizeof(z));assert(z<4096);std::vector<uint8_t> v(z);alarmRead(f,v.data(),z);values[k]=v;}
 assert(!fclose(f));assert(!unlink(reset));seedMs=0;resetReason=m.caseIndex!=33?ESP_RST_DEEPSLEEP:ESP_RST_SW;if(m.caseIndex==33){assert(clockEditWord(8)==1);am.intentGeneration=clockEditWord(12);}
}
void alarmTimerArm(uint32_t ms){
 auto cfg=alarmSaved();assert(cfg.enabled&&am.ticketDeadline==cfg.deadline&&am.ticketDeadline>am.ticketSeconds);
 uint32_t delta=am.ticketDeadline-am.ticketSeconds;
 uint32_t expected=delta>86400?86400000:delta>1?(delta-1)*1000:1;
 assert(ms==expected);timeval wall{};wallRead(&wall,nullptr);
 int64_t remaining=((int64_t)cfg.deadline+946684800-wall.tv_sec)*1000-wall.tv_usec/1000;
 assert((int64_t)ms<=remaining+250); // Explicit modeled post-ticket preparation allowance.
}
static void alarmPersistReset(bool deep){
 if(deep)m.ms+=am.timer+1000;timeval wall{};wallRead(&wall,nullptr);
 std::string file=m.storeRoot+"/alarm-reset-medium.bin";FILE*f=fopen(file.c_str(),"wb");assert(f);uint32_t reason=deep?1:2;
 alarmWrite(f,&reason,sizeof(reason));alarmWrite(f,&wall,sizeof(wall));alarmWrite(f,&RiscCpu::NativeRealtime::stamp,sizeof(RiscCpu::NativeRealtime::stamp));uint32_t count=values.size();alarmWrite(f,&count,sizeof(count));
 for(auto&it:values){uint32_t n=it.first.size(),z=it.second.size();alarmWrite(f,&n,sizeof(n));alarmWrite(f,it.first.data(),n);alarmWrite(f,&z,sizeof(z));alarmWrite(f,it.second.data(),z);}
 assert(!fclose(f));assert(!setenv("BADGE_ALARM_RESET",file.c_str(),1));std::string index=std::to_string(m.caseIndex);
 printf("MODELED_%s reset: discard entire Runtime/service/process; retain SDK wall and actual KV bytes; no live editor permit\n",deep?"DEEP":"EDITOR_CRASH_CUT");
 execl(am.executable.c_str(),am.executable.c_str(),m.storeRoot.c_str(),index.c_str(),(char*)nullptr);assert(false);
}
void alarmDeepReset(){
 if(qm.enabled&&m.caseIndex==39){assert(qm.saved&&!am.reset&&!m.display&&m.hold&&!am.timer&&m.deepArms);m.ms+=59000;RiscCpu::NativeRealtime::enter([](){alarmPersistReset(true);});return;}
 assert(m.caseIndex==27&&am.saved&&!am.reset&&!m.display&&m.hold&&am.timer&&m.deepArms);
 RiscCpu::NativeRealtime::enter([](){alarmPersistReset(true);});
}

bool alarmLight(uint32_t*cause){
#ifdef BADGE_WIFI_EXECUTION
 if(wm.enabled)return wifiLight(cause);
#endif
 if(qm.enabled&&m.caseIndex!=35)return quickLight(cause);
 assert(am.resumeAttempts==m.lightEntries&&am.resumeSuccesses==m.lightEntries);
 memcpy(am.lightTicket,am.issuedTicket,sizeof(am.lightTicket));
 assert(!m.terminal&&!m.display&&!m.hold);
 assert(!m.pins[2]||m.touchSleeping);
 if(m.foreground){if(am.uiProbe)am.uiProbe();assert(!am.touchGrant&&!am.touchSubscription);}
 if(m.foreground){assert(m.nativeToken);std::string text;probeState(&text);if(!m.lightEntries)am.draftBefore=text;else assert(text==am.draftBefore);}
#ifdef BADGE_ALARM_NATIVE_GEOMETRY
 if(m.caseIndex==31||m.caseIndex==32){
  assert(am.nativeInputMask==511&&am.uiPage==2&&am.uiField==1&&am.nativeDraftInitial);
  assert(am.draft[0]!=am.initialDraft[0]&&am.draft[1]==am.initialDraft[1]&&am.draft[2]==am.initialDraft[2]);
 }
#endif
 if((m.caseIndex==31||m.caseIndex==32)){assert(!am.saved&&!am.timer);m.ms+=7000;++am.gpioWakes;*cause=RISC_LIGHT_SLEEP_WAKE_GPIO;}
 else {auto before=alarmOccurrence();printf("ALARM_SLEEP case=%u cycle=%u timer_ms=%u saved=%u occurrence=%u modal=%u visible=%u dismiss_sent=%u acked=%u wall_ms=%llu\n",m.caseIndex,m.lightEntries,am.timer,am.saved,before.state,am.modal,am.visible,am.dismissKey,am.acked,(unsigned long long)m.ms);assert(am.saved&&am.timer);auto cfg=alarmSaved();timeval now{};wallRead(&now,nullptr);assert(now.tv_sec<946684800+(int64_t)cfg.deadline);if(m.caseIndex==26&&!am.gpioWakes){m.ms+=1000;++am.gpioWakes;*cause=RISC_LIGHT_SLEEP_WAKE_GPIO;}else{m.ms+=am.timer;++am.timerWakes;*cause=RISC_LIGHT_SLEEP_WAKE_TIMER;}}
 ++m.lightEntries;m.wokeAt=m.ms;m.keySent=false;m.entered=m.ms;m.levels[3]=false;
 if(m.foreground)m.restoreExpected=true;
 return true;
}
#else
extern "C" void badge_clock_edit_begin_result(int){}
extern "C" void badge_alarm_ticket_fault(void*,int){}
extern "C" void badge_alarm_resume_observe(const void*,int){}
void alarmObserve(bool){}
void alarmTimerArm(uint32_t){}
unsigned alarmCatalog(){if(qm.enabled)return quickCatalog();assert(false);return 0;}
bool alarmTouch(uint64_t,bool&,unsigned&,unsigned&){assert(false);return false;}
void alarmKeys(uint8_t){}void alarmEnter(const char*){}void alarmExit(const char*){}
void alarmInitialize(const char*){assert(false);}
// Alarm persistence reset exists only in the selected native-alarm fixture.
// Unselected builds retain a terminal guard rather than naming that helper.
void alarmDeepReset(){assert(false);}
bool alarmLight(uint32_t*){assert(false);return false;}
#endif

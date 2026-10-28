/* Actual Runtime Timecard composition: only external input, media, SDK wake and
 * an explicit post-success collect fault are modeled. All app actions use CST
 * reports, actual production control maps, and physical PMIC key semantics. */
extern "C" void badge_timecard_observe(const int*s,const int*c,const char*d,const char*status,const unsigned char*chars){
 memcpy(tc.ui,s,sizeof(tc.ui));memcpy(tc.controls,c,sizeof(tc.controls));memcpy(tc.chars,chars,32);tc.draft=d?d:"";tc.status=status?status:"";
}
extern "C" void badge_timecard_quick_observe(const int*s,const int*r){memcpy(tc.quick,s,sizeof(tc.quick));memcpy(tc.qrects,r,sizeof(tc.qrects));}
extern "C" void badge_timecard_launcher_observe(unsigned count,unsigned first,unsigned n,const int*c,int mx,int my){
 assert(count==tc.expectedCount&&n<=19);tc.count=count;tc.first=first;tc.visible=n;memcpy(tc.centers,c,sizeof(tc.centers));tc.motionX=mx;tc.motionY=my;++tc.launcherFrames;
}
static void tcRaster(const char*label){
 const char*dir=getenv("BADGE_PROTOTYPE_RASTERS");if(!dir||!*dir)return;
 std::string path=std::string(dir)+"/timecard-"+tc.scenario+"-"+label+".ppm";
 FILE*f=fopen(path.c_str(),"wb");assert(f);fprintf(f,"P6\n466 466\n255\n");
 for(auto p:m.pixels){unsigned char c[3]={uint8_t(((p>>11)&31)*255/31),uint8_t(((p>>5)&63)*255/63),uint8_t((p&31)*255/31)};assert(fwrite(c,1,3,f)==3);}assert(!fclose(f));
}
void timecardObserve(bool completed){
 if(m.terminal)return;
 if(am.uiProbe)am.uiProbe();
 if(completed&&selectedForeground()&&tc.quick[22]){tc.pendingFrame=m.frames;tc.pendingPixels=m.pixels;tcRaster("pending-before-save");}
 if(completed&&selectedForeground()&&tc.ui[2]&&!tc.capture){tc.capture=1;tcRaster("alphabetic-editor");}
 if(completed&&selectedForeground()&&tc.draft=="09:45"&&tc.capture<2){tc.capture=2;tcRaster("numeric-draft");}
 if(completed&&am.modal){tc.alarmSeen=true;am.visibleAt=am.visibleAt?am.visibleAt:m.ms;tcRaster("alarm");}
 if(tc.alarmSeen&&!am.modal&&tc.alarmDismissed&&selectedForeground()&&tc.ui[2])assert(tc.draft=="09:45");
 if(completed&&m.app=="settings")++tc.settingsFrames;
 if(completed&&tc.scenario=="quick-shared"&&m.app=="default"&&tc.homeEntries>1&&tc.quick[2]==466*256){
  assert(tc.quick[4]==80&&tc.quick[5]==80&&!tc.quick[22]&&tc.preferenceWrites==1&&tc.settingsFrames);tcRaster("fresh-home-saved80");tc.done=true;
 }
 if(m.app=="default"&&tc.homeEntries>1&&tc.scenario!="quick-shared"&&m.appVisible&&m.ms-m.entered>1200)tc.done=true;
}
static void tcTap(int x,int y){assert(x>=0&&x<466&&y>=0&&y<466);tc.x=x;tc.y=y;tc.x2=x;tc.y2=y;tc.drag=false;tc.tapActive=true;tc.actionAt=m.ms;tc.releaseAt=m.ms+160;tc.nextAt=m.ms+850;}
static void tcDrag(int x,int y,int x2,int y2){tcTap(x,y);tc.x2=x2;tc.y2=y2;tc.drag=true;tc.releaseAt=m.ms+500;tc.nextAt=m.ms+1400;}
static bool tcControl(unsigned action,unsigned arg){
 for(int i=0;i<tc.ui[8]&&i<40;i++){auto*c=tc.controls[i];if(c[0]==int(action)&&c[1]==int(arg)){assert(c[4]>0&&c[5]>0);tcTap((466-tc.ui[11])/2+c[2]+c[4]/2,(466-tc.ui[12])/2+c[3]+c[5]/2);return true;}}
 return false;
}
static bool tcKey(unsigned key){return tcControl(6,key);}
static void tcBack(){assert(tcControl(1,0));}
static bool tcEntry(){
 if(!tc.ui[2]){
  assert(tc.ui[1]>=0&&tc.ui[1]<=2&&tc.ui[3]>=0);
  // open_week(0) selects today and scrolls that actual row into view. Row0
  // need not be visible; drive the displayed Confirm for the selected row.
  printf("TIMECARD_SELECT screen=%d selected=%d first=%d controls=%d\n",tc.ui[1],tc.ui[3],tc.ui[15],tc.ui[8]);
  assert(tcControl(2,0));return true;
 }
 if(tc.initialPage==99){tc.initialPage=tc.ui[6];assert(tc.initialPage==2);}
 if(tc.scenario=="cold-load"){assert(tc.ui[9]==1&&tc.dataReads&&tc.ledger.find("585")!=std::string::npos);tc.backing=true;return false;}
 const std::string wanted="09:45";
 if(tc.draft==wanted)return false;
 // Clear any default or mismatched initial text using the visible Delete key.
 if(!tc.draft.empty()&&wanted.compare(0,tc.draft.size(),tc.draft)!=0){assert(tcKey(33));return true;}
 unsigned ch=(unsigned char)wanted[tc.draft.size()];
 for(unsigned i=0;i<32;i++)if(tc.chars[i]==ch){assert(tcKey(i));return true;}
 ++tc.keyboardPages;assert(tc.keyboardPages<12);assert(tcKey(32));return true;
}
static void tcNavigate(unsigned wanted){
 assert(tc.count==tc.expectedCount&&wanted<tc.count);int index=int(wanted)-int(tc.first);
 if(index<0||index>=int(tc.visible)){tcDrag(233,300,233,120);++tc.launcherDrags;return;}
 int x=tc.centers[index][0],y=tc.centers[index][1];
 // A real pan brings the manifest-selected item into reach. Use the actual
 // displayed centers, never overwrite launcher selection or invoke launch API.
 if(wanted==tc.timecardIndex&&!tc.launcherDrags){int sx=std::max(80,std::min(386,x)),sy=std::max(80,std::min(386,y));tcDrag(sx,sy,233,233);++tc.launcherDrags;return;}
 if(x<40||x>426||y<40||y>426){tcDrag(233,233,std::max(70,std::min(396,466-x)),std::max(70,std::min(396,466-y)));++tc.launcherDrags;return;}
 tcTap(x,y);
}
void timecardTouch(uint64_t age,bool&down,unsigned&x,unsigned&y){
 down=false;timecardObserve(false);if(tc.done||am.modal)return;
 if(tc.tapActive){if(m.ms<tc.releaseAt){down=true;uint64_t elapsed=m.ms-tc.actionAt,duration=tc.releaseAt-tc.actionAt;x=tc.x+(tc.drag?int(tc.x2-tc.x)*int(elapsed)/int(duration):0);y=tc.y+(tc.drag?int(tc.y2-tc.y)*int(elapsed)/int(duration):0);return;}tc.tapActive=false;}
 if(m.ms<tc.nextAt||age<1300||!m.appVisible)return;
 if(m.app=="default"){
  if(tc.homeEntries==1)tcDrag(310,179,100,179);
  else if(tc.scenario=="quick-shared"&&!tc.quick[0])tcDrag(233,4,233,284);
  return;
 }
 if(m.app=="springboard"){
  // Retained old app pixels are not this invocation's completed launcher map.
  if(!tc.count)return;
  if(!tc.entered)tcNavigate(tc.timecardIndex);
  else if(tc.scenario=="quick-shared"&&!tc.settingsEntries)tcNavigate(tc.settingsIndex);
  return;
 }
 if(!selectedForeground()||!tc.ui[0]||(!tc.ui[13]&&!tc.quick[0]))return;
 if(tc.backing){tcBack();return;}
 if(tcEntry())return;
 if(tc.backing){tcBack();return;}
 assert(tc.draft=="09:45"&&tc.ui[2]);
 if(tc.scenario=="save"){assert(tcKey(34));tc.backing=true;return;}
 if(tc.scenario=="light-draft"){
  if(!m.lightEntries||m.restoring||m.restoreExpected||m.ms-m.wokeAt<1400)return;
  assert(m.restores==1&&m.transactionResumes>=1&&!tc.dataWrites);tc.neutralAfterWake=true;tc.backing=true;tcBack();return;
 }
 if(tc.scenario=="alarm-draft"){
  if(!tc.alarmSeen||!tc.alarmDismissed||!tc.alarmAcked||am.modal)return;
  assert(tc.ui[2]&&tc.draft=="09:45"&&!tc.dataWrites);tc.backing=true;tcBack();return;
 }
 if(tc.scenario=="quick-shared"||tc.scenario=="quick-retained"){
  if(!tc.step){tcDrag(233,4,233,284);++tc.step;return;}
  if(tc.step==1&&tc.quick[2]==466*256){assert(tc.quick[16]<tc.quick[17]);tcTap(tc.quick[16]+(tc.quick[17]-tc.quick[16])*7/9,tc.quick[18]);++tc.step;return;}
  if(tc.step==2&&tc.preferenceWrites&&!tc.quick[22]&&tc.quick[5]==80){auto*r=tc.qrects[9];assert(r[2]>0&&r[3]>0);tcTap(r[0]+r[2]/2,r[1]+r[3]/2);++tc.step;return;}
  if(tc.step==3&&!tc.quick[0]){assert(tc.ui[2]&&tc.draft=="09:45"&&!tc.dataWrites);tc.backing=true;tcBack();return;}
 }
}
void timecardKeys(uint8_t reg){
 if(reg!=0x49||m.keySent)return;
 if(am.modal&&tc.alarmSeen){
  // The real post-Light key filter requires a fresh observed physical press,
  // not an isolated old PMIC latch. Match the established alarm fixture.
  uint64_t age=m.ms-am.visibleAt;
  if(age>=2000&&age<2200)tc.alarmKeyDown=true;
  if(age>=2200){tc.alarmKeyDown=false;m.pmic[0x49]|=8;m.keySent=true;tc.alarmDismissed=true;}
  return;
 }
 if(m.app=="springboard"&&tc.entered&&(tc.scenario!="quick-shared"||tc.settingsEntries)&&m.ms-m.entered>1500){m.pmic[0x49]|=4;m.keySent=true;}
 if(m.app=="settings"&&m.ms-m.entered>2200&&tc.settingsFrames){m.pmic[0x49]|=4;m.keySent=true;}
}
void timecardEnter(const char*n){
 tc.routes.push_back(n);tc.tapActive=false;tc.nextAt=m.ms+1300;memset(tc.quick,0,sizeof(tc.quick));
 if(!strcmp(n,"default"))++tc.homeEntries;
 else if(!strcmp(n,"timecard")){assert(!tc.entered&&tc.count==tc.expectedCount&&tc.launcherDrags);tc.entered=true;++tc.timecardEntries;m.selectedTouchOpens=m.touchOpens;m.selectedTouchCloses=m.touchCloses;m.selectedSeedCalls=m.seedCalls;}
 else if(!strcmp(n,"settings")){assert(tc.scenario=="quick-shared"&&tc.entered);++tc.settingsEntries;}
 else {assert(!strcmp(n,"springboard"));tc.count=tc.visible=0;}
}
void timecardExit(const char*n){if(!strcmp(n,"timecard")&&!m.terminal){assert(tc.backing&&tc.ui[1]==0&&!tc.ui[2]);if(tc.scenario=="save")assert(tc.dataWrites==1);}}
void timecardInitialize(){
 const char*s=getenv("TIMECARD_SCENARIO");assert(s);tc.scenario=s;
 const char*count=getenv("TIMECARD_CATALOG_COUNT"),*item=getenv("TIMECARD_CATALOG_INDEX"),*settings=getenv("TIMECARD_SETTINGS_INDEX");
 assert(count&&item&&settings);tc.expectedCount=std::strtoul(count,nullptr,10);tc.timecardIndex=std::strtoul(item,nullptr,10);tc.settingsIndex=std::strtoul(settings,nullptr,10);
 assert(tc.expectedCount&&tc.timecardIndex<tc.expectedCount&&tc.settingsIndex<tc.expectedCount&&tc.timecardIndex!=tc.settingsIndex);
 assert(tc.scenario=="save"||tc.scenario=="cold-load"||tc.scenario=="light-draft"||tc.scenario=="alarm-draft"||tc.scenario=="quick-shared"||tc.scenario=="quick-retained");
 if(tc.scenario=="alarm-draft"){
  alarm_config cfg{};cfg.kind=2;cfg.enabled=1;cfg.revision=1;cfg.created=1767225600u-946684800u;cfg.duration=90;cfg.deadline=cfg.created+cfg.duration;assert(alarm_config_valid(&cfg));std::vector<uint8_t>b(ALARM_RECORD_SIZE);alarm_config_encode(&cfg,b.data());values["3:timer_utc_cfg"]=b;
  puts("MODELED_PREEXISTING_COUNTDOWN duration90 UTC2026; configuration seeding is fixture setup, not app Save proof");
 }
}
bool timecardLight(uint32_t*cause){
 assert(!m.terminal&&selectedForeground()&&!m.display&&!m.pins[2]&&!m.hold&&m.nativeToken&&tc.ui[2]&&tc.draft=="09:45"&&!tc.dataWrites);
 printf("TIMECARD_LIGHT cycle=%u timer=%u modal=%u seen=%u dismiss=%u ack=%u\n",m.lightEntries+1,am.timer,unsigned(am.modal),unsigned(tc.alarmSeen),unsigned(tc.alarmDismissed),unsigned(tc.alarmAcked));
 assert(tc.scenario=="light-draft"||tc.scenario=="alarm-draft");++m.lightEntries;
 uint64_t elapsed=tc.scenario=="alarm-draft"?(assert(am.timer),am.timer+1000u):7000u;m.ms+=elapsed;m.totalSlept+=elapsed;m.wokeAt=m.ms;
 tc.gpioWake=tc.scenario=="light-draft";m.levels[3]=tc.gpioWake;m.restoreExpected=true;
 *cause=tc.gpioWake?RISC_LIGHT_SLEEP_WAKE_GPIO:RISC_LIGHT_SLEEP_WAKE_TIMER;return true;
}
void timecardKvRead(uint32_t ns,const char*k,const void*,uint32_t){assert(!m.terminal);if(ns==1&&m.app=="settings")++tc.settingsReads;(void)k;}
void timecardKvWrite(uint32_t ns,const char*k,const void*b,uint32_t n){
 assert(!m.terminal);
 if(ns==1&&!strcmp(k,"brightness")){assert(selectedForeground()&&n==1&&*(const uint8_t*)b==80&&tc.quick[22]&&tc.pendingFrame==m.frames&&tc.pendingPixels==m.pixels);++tc.preferenceWrites;}
 if(ns==4&&!strcmp(k,"timer_utc_occ")){alarm_occurrence o{};assert(alarm_occurrence_decode(&o,(const uint8_t*)b,n,2));if(o.state==ALARM_OCC_ACKED)tc.alarmAcked=true;}
}
void timecardDataRead(uint32_t ns,const char*p,const void*b,uint32_t n){assert(ns==1&&!strcmp(p,"timecard.json"));tc.ledger.assign((const char*)b,n);}
void timecardDataWrite(uint32_t ns,const char*p,const void*b,uint32_t n){assert(tc.scenario=="save"&&ns==1&&!strcmp(p,"timecard.json")&&tc.ui[2]);tc.ledger.assign((const char*)b,n);assert(tc.ledger.find("585")!=std::string::npos);++tc.dataWrites;}
extern "C" void badge_timecard_preference_call(){assert(!m.terminal);++tc.preferenceCalls;}
extern "C" void badge_timecard_submit_observe(){assert(!m.terminal&&selectedForeground()&&tc.quick[22]&&tc.pendingFrame==m.frames&&tc.pendingPixels==m.pixels);++tc.preferenceSubmits;}
extern "C" int badge_timecard_collect_result(int rc){assert(!m.terminal);if(tc.scenario=="quick-retained"&&rc==0){assert(tc.preferenceWrites==1&&tc.preferenceSubmits==1&&tc.pendingPixels==m.pixels);tc.unknown=true;m.terminal=true;m.terminalIo=m.io;return 99;}return rc;}
void timecardFinish(bool ran,bool retained){
 if(tc.scenario=="quick-retained"){assert(!ran&&retained&&m.terminal&&tc.unknown&&m.io==m.terminalIo);}
 else {assert(ran&&!retained&&tc.done&&tc.entered&&tc.timecardEntries==1&&tc.homeEntries==2);assert(tc.scenario=="save"?tc.dataWrites==1:tc.dataWrites==0);if(tc.scenario=="cold-load")assert(tc.dataReads);if(tc.scenario=="light-draft")assert(m.lightEntries==1&&m.restores==1&&tc.neutralAfterWake);if(tc.scenario=="alarm-draft")assert(tc.alarmSeen&&tc.alarmDismissed&&tc.alarmAcked);if(tc.scenario=="quick-shared")assert(tc.preferenceWrites==1&&tc.settingsEntries==1&&tc.settingsFrames);}
 printf("PASS actual Runtime Timecard scenario=%s launcher_item=%u catalog=%u actual_scroll=%u data_reads=%u data_writes=%u light=%u restored=%u pending_frames=%u copied_preference_writes=%u retained=%u no_after_retained=passed modeled_not_device\n",tc.scenario.c_str(),tc.timecardIndex+1,tc.expectedCount,tc.launcherDrags,tc.dataReads,tc.dataWrites,m.lightEntries,m.restores,tc.pendingFrame,tc.preferenceWrites,unsigned(retained));
}

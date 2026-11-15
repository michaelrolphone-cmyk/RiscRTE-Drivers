struct SettingsOp { bool deep; unsigned value; int ending; }; // 0 Save,1 Cancel,2 physical Back
static std::vector<SettingsOp> settingsOps;
static std::string settingsCase,settingsMessage;
static unsigned settingsIdle=10000,settingsDeep=120000,settingsOp=0,settingsWrites[2]={},settingsReads[2]={},settingsFailures=0,settingsPendingFrame=0,settingsErrorFrame=0;
static bool settingsAwaitRoot=false,settingsBack=false,settingsDone=false,settingsWriteArmed=false;
static int settingsState[32]={},settingsTick=-1,settingsAction=-1,settingsX=0,settingsY=0,settingsTimeStep=0;
static unsigned settingsPendingDrawAt=~0u,settingsErrorDrawAt=~0u,settingsAttemptFrame=0;
extern "C" void badge_settings_draw_text(const char*s){if(s&&!strcmp(s,"Saving - not confirmed"))settingsPendingDrawAt=m.frames;if(s&&!strcmp(s,"Save unconfirmed - retry"))settingsErrorDrawAt=m.frames;}
static std::map<std::string,std::vector<uint8_t>> settingsConfirmed;
static std::vector<uint8_t> settingsBytes(unsigned ms){unsigned v=ms/1000;return {0x54,1,uint8_t(v),uint8_t(v>>8),uint8_t(0x54^1^(v&255)^(v>>8)^0xf0)};}
static void settingsInitialize(){
 settingsCase=getenv("SETTINGS_SCENARIO")?getenv("SETTINGS_SCENARIO"):"automatic";
 settingsIdle=settingsCase=="manual"||settingsCase=="cancel-time"?15000:10000;settingsDeep=settingsCase=="manual"?60000:120000;
 settingsOps={{false,settingsIdle,0},{true,settingsDeep,0}};
 if(settingsCase=="cancel-time"){settingsOps.push_back({false,settingsIdle+5000,1});settingsOps.push_back({true,settingsDeep+60000,2});}
 settingsOps.push_back({false,settingsIdle,1});settingsOps.push_back({true,settingsDeep,1});
}
extern "C" void badge_settings_ui_observe(const int*s,const char*message){memcpy(settingsState,s,sizeof(settingsState));settingsMessage=message?message:"";}
static void settingsCompletedFrame(){
 if(m.app!="settings")return;if(am.uiProbe)am.uiProbe();
 if(settingsState[1]&&settingsMessage=="Saving - not confirmed"&&settingsPendingDrawAt<m.frames)settingsPendingFrame=m.frames;
 if(settingsState[1]&&settingsMessage=="Save unconfirmed - retry"&&settingsErrorDrawAt<m.frames)settingsErrorFrame=m.frames;
}
static bool settingsBeforeWrite(uint32_t ns,const char*k,const void*b,uint32_t size){
 if(ns!=1||(strcmp(k,"sleep_idle")&&strcmp(k,"sleep_deep")))return true;
 assert(m.app=="settings"&&settingsOp<settingsOps.size()&&settingsWriteArmed&&settingsOps[settingsOp].ending==0);
 const auto&o=settingsOps[settingsOp];assert(o.deep==!strcmp(k,"sleep_deep")&&size==5&&std::vector<uint8_t>((const uint8_t*)b,(const uint8_t*)b+size)==settingsBytes(o.value));
 assert(settingsPendingFrame>settingsAttemptFrame&&m.frames>=settingsPendingFrame);
 if(settingsCase=="io-retry"&&!settingsFailures){++settingsFailures;settingsWriteArmed=false;printf("SETTINGS_IO refused actual_Save key=%s durable_unchanged=1 pending_frame=%u\n",k,settingsPendingFrame);return false;}
 ++settingsWrites[o.deep];settingsConfirmed[k]=settingsBytes(o.value);printf("SETTINGS_SAVE key=%s ms=%u pending_frame=%u\n",k,o.value,settingsPendingFrame);return true;
}
static void settingsAfterRead(uint32_t ns,const char*k,const void*b,uint32_t size){
 if(ns!=1||(strcmp(k,"sleep_idle")&&strcmp(k,"sleep_deep")))return;
 if(m.app=="default"&&m.visits==4){assert(settingsDone&&size==5);bool deep=!strcmp(k,"sleep_deep");assert(std::vector<uint8_t>((const uint8_t*)b,(const uint8_t*)b+size)==settingsBytes(deep?settingsDeep:settingsIdle));++settingsReads[deep];}
}
static void settingsCheckSaved(){assert(settingsWrites[0]==1&&settingsWrites[1]==1);assert(values.at("1:sleep_idle")==settingsBytes(settingsIdle)&&values.at("1:sleep_deep")==settingsBytes(settingsDeep));assert(!values.count("1:sleep_mode"));if(settingsCase=="io-retry")assert(settingsFailures==1&&settingsErrorFrame);}
static bool hybridSettingsTouch(uint64_t age,bool&down,unsigned&x,unsigned&y){
 down=false;if(age<1100)return false;if(am.uiProbe)am.uiProbe();
 assert(settingsState[3]>0&&settingsState[4]>0&&!settingsState[2]);
 int tick=int((age-1100)/700),phase=int((age-1100)%700);
 if(tick!=settingsTick){settingsTick=tick;settingsAction=-1;
  auto tap=[](int a,int b){settingsAction=0;settingsX=a;settingsY=b;};
  auto row=[&](int yy){if(yy>170){settingsAction=1;settingsX=120;settingsY=170;}else if(yy<55){settingsAction=2;settingsX=120;settingsY=70;}else tap(120,yy);};
  if(settingsAwaitRoot&&settingsState[0]){const auto&o=settingsOps[settingsOp];if(o.ending==0)assert(settingsConfirmed.at(o.deep?"sleep_deep":"sleep_idle")==settingsBytes(o.value));else settingsCheckSaved();++settingsOp;settingsAwaitRoot=false;settingsWriteArmed=false;}
  if(settingsOp<settingsOps.size()){
   const auto&o=settingsOps[settingsOp];
   if(settingsState[0])row(settingsState[o.deep?8:7]);
   else if(settingsState[1]){
    assert(bool(settingsState[9])==o.deep);
    if(settingsAwaitRoot){assert(settingsCase=="io-retry"&&settingsFailures==1);if(settingsErrorFrame){settingsAwaitRoot=false;puts("SETTINGS_IO completed_unconfirmed_frame explicit_retry_next=1");}}
    if(!settingsAwaitRoot){
     if(unsigned(settingsState[10])!=o.value)tap(settingsState[unsigned(settingsState[10])>o.value?21:22]+settingsState[24]/2,settingsState[23]+settingsState[25]/2);
     else {settingsAwaitRoot=true;if(o.ending==2)settingsBack=true;else{settingsWriteArmed=o.ending==0;if(settingsWriteArmed){settingsPendingFrame=0;settingsPendingDrawAt=~0u;settingsAttemptFrame=m.frames;}tap(settingsState[o.ending==0?17:16]+settingsState[19]/2,settingsState[18]+settingsState[20]/2);}}
    }
   }else assert(false);
  }else if(settingsCase=="cancel-time"&&settingsTimeStep<12){
   if(settingsTimeStep==0||settingsTimeStep==6){assert(settingsState[0]);if(settingsState[11]){settingsAction=2;settingsX=120;settingsY=70;}else{tap(120,settingsState[27]);++settingsTimeStep;}}
   else if(settingsTimeStep==1||settingsTimeStep==7){assert(settingsState[12]);tap(120,60+settingsState[26]/2);++settingsTimeStep;}
   else if(settingsTimeStep==2||settingsTimeStep==8){assert(settingsState[13]&&settingsState[14]==0&&settingsState[15]==2026);tap(198,84);++settingsTimeStep;}
   else if(settingsTimeStep==3||settingsTimeStep==9){assert(settingsState[13]&&settingsState[15]==2027);tap(120,205);++settingsTimeStep;}
   else if(settingsTimeStep==4||settingsTimeStep==10){assert(settingsState[12]);tap(settingsState[settingsTimeStep==4?16:17]+settingsState[19]/2,settingsState[18]+settingsState[20]/2);++settingsTimeStep;}
   else{assert(settingsState[0]);++settingsTimeStep;}
  }else if(settingsState[0]){settingsCheckSaved();settingsDone=true;tap(120,205);}
 }
 if(settingsAction==0&&phase<160){down=true;badgeSettingsLogical(settingsX,settingsY,x,y);}
 if((settingsAction==1||settingsAction==2)&&phase<300){down=true;badgeSettingsLogical(settingsX,settingsY+(settingsAction==1?-1:1)*phase/3,x,y);}
 return down;
}

static std::string settingsCaptureName(){std::string name=settingsCase+"-"+m.app;if(m.app=="settings"){name+="-op"+std::to_string(settingsOp);if(settingsPendingFrame==m.frames)name+="-pending-complete";else if(settingsErrorFrame==m.frames)name+="-error-complete";else if(settingsState[1])name+="-timer"+std::to_string(settingsState[9])+"-"+std::to_string(settingsState[10]);else if(settingsState[12])name+="-time-fields";else if(settingsState[13])name+="-year"+std::to_string(settingsState[15]);else name+="-root";}return name;}

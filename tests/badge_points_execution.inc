/* Points scenarios share the existing Runtime/launcher/CST/PMIC fixture. Only
 * initial medium/time and external stimuli are modeled. Production app Save,
 * service recurrence, durable ACK, capability leases and navigation run intact. */
#ifdef BADGE_POINTS_EXECUTION
extern "C" void badge_points_observe(const int*s,const int*c,const unsigned char*saved,const char*notice){
 memcpy(pm.ui,s,sizeof(pm.ui));memcpy(pm.controls,c,sizeof(pm.controls));pm.observed.assign(saved,saved+64);pm.notice=notice;
}
static bool pointsControl(unsigned action,unsigned arg){
 for(int i=0;i<pm.ui[9]&&i<64;i++){const auto*c=pm.controls[i];if(c[0]==int(action)&&c[1]==int(arg)){
  assert(c[4]>0&&c[5]>0);tcTap((466-pm.ui[7])/2+c[2]+c[4]/2,(466-pm.ui[8])/2+c[3]+c[5]/2);return true;}}
 return false;
}
void pointsObserve(bool completed){
 if(m.terminal)return;if(am.uiProbe)am.uiProbe();
 if(selectedForeground()&&pm.ui[3]&&pm.ui[4]&&pm.ui[18]==ALARM_OK)++pm.contextReady;
 if(completed&&am.modal){pm.alarmSeen=true;am.visibleAt=am.visibleAt?am.visibleAt:m.ms;}
 if(completed&&selectedForeground()&&!tc.capture&&pm.ui[3]&&pm.ui[4]){tc.capture=1;tcRaster("loaded");}
 if(completed&&selectedForeground()&&pm.step==3&&pm.ui[13]==int(pm.draftMinute)&&tc.capture<2){tc.capture=2;tcRaster("draft");}
 if(m.app=="default"&&tc.homeEntries>1&&m.appVisible&&m.ms-m.entered>1200)tc.done=true;
}
void pointsTouch(uint64_t age,bool&down,unsigned&x,unsigned&y){
 down=false;pointsObserve(false);if(tc.done||am.modal)return;
 if(tc.tapActive){if(m.ms<tc.releaseAt){down=true;uint64_t e=m.ms-tc.actionAt,d=tc.releaseAt-tc.actionAt;x=tc.x+(tc.drag?int(tc.x2-tc.x)*int(e)/int(d):0);y=tc.y+(tc.drag?int(tc.y2-tc.y)*int(e)/int(d):0);return;}tc.tapActive=false;}
 if(m.ms<tc.nextAt||age<1300||!m.appVisible)return;
 if(m.app=="default"){if(tc.homeEntries==1)tcDrag(310,179,100,179);return;}
 if(m.app=="springboard"){
  if(tc.count&&(!tc.entered||(tc.scenario=="save-reopen"&&pm.entries==1)))tcNavigate(tc.timecardIndex);
  return;
 }
 if(!selectedForeground()||!pm.ui[0]||!pm.ui[10]||!pm.ui[3]||!pm.ui[4])return;
 assert(!pm.ui[5]&&!pm.ui[6]&&!pm.ui[17]);
 if(pm.entries==2){assert(tc.scenario=="save-reopen"&&pm.observed==pm.saved);pm.reopened=true;pm.backing=true;assert(pointsControl(1,0));return;}
 if(pm.backing){assert(pointsControl(1,0));return;}
 if(pm.step==0){assert(pm.ui[1]==0&&pm.observed==pm.initial);assert(pointsControl(4,0));++pm.step;return;}
 if(pm.step==1){if(pm.ui[1]!=1)return;pm.originalMinute=pm.ui[13];pm.draftMinute=(pm.originalMinute+1)%60;assert(pointsControl(6,0));++pm.step;return;}
 if(pm.step==2){if(pm.ui[1]!=3)return;assert(pointsControl(21,5));++pm.step;return;}
 if(pm.step==3){if(pm.ui[13]!=int(pm.draftMinute))return;
  assert(pm.ui[1]==3&&pm.observed==pm.initial);
  if(tc.scenario=="save-reopen"){assert(pointsControl(1,0));++pm.step;return;}
  if(tc.scenario=="light-draft"){
   if(!m.lightEntries||m.restoring||m.restoreExpected||m.ms-m.wokeAt<1400)return;
   assert(m.restores==1&&m.transactionResumes>=1&&!pm.writes);tc.neutralAfterWake=true;
  }else{assert(tc.scenario=="service-draft");if(!pm.alarmSeen||!pm.alarmDismissed||!pm.acked)return;assert(pm.pending&&pm.ui[13]==int(pm.draftMinute)&&!pm.writes);}
  pm.backing=true;assert(pointsControl(1,0));return;
 }
 if(pm.step==4){if(pm.ui[1]!=1)return;assert(pm.ui[13]==int(pm.draftMinute));assert(pointsControl(2,0));++pm.step;return;}
 if(pm.step==5&&pm.writes){assert(pm.ui[1]==0&&pm.observed==pm.saved);pm.backing=true;assert(pointsControl(1,0));}
}
void pointsKeys(uint8_t reg){
 if(reg!=0x49||m.keySent)return;
 if(am.modal&&pm.alarmSeen){uint64_t age=m.ms-am.visibleAt;if(age>=2000&&age<2200)tc.alarmKeyDown=true;
  if(age>=2200){tc.alarmKeyDown=false;m.pmic[0x49]|=8;m.keySent=true;pm.alarmDismissed=true;}return;}
 if(m.app=="springboard"&&tc.entered&&(tc.scenario!="save-reopen"||pm.reopened)&&m.ms-m.entered>1500){m.pmic[0x49]|=4;m.keySent=true;}
}
void pointsEnter(const char*n){
 tc.routes.push_back(n);tc.tapActive=false;tc.nextAt=m.ms+1300;
 if(!strcmp(n,"default"))++tc.homeEntries;
 else if(!strcmp(n,"points_in_time")){assert(tc.count==tc.expectedCount&&tc.launcherDrags);tc.entered=true;++pm.entries;pm.backing=false;memset(pm.ui,0,sizeof(pm.ui));m.selectedSeedCalls=m.seedCalls;}
 else{assert(!strcmp(n,"springboard"));tc.count=tc.visible=0;}
}
void pointsExit(const char*n){if(!strcmp(n,"points_in_time")){assert(pm.backing&&pm.ui[1]==0&&!m.terminal);assert(m.seedCalls==m.selectedSeedCalls);}}
void pointsInitialize(){
 const char*s=getenv("TIMECARD_SCENARIO"),*count=getenv("TIMECARD_CATALOG_COUNT"),*index=getenv("TIMECARD_CATALOG_INDEX");assert(s&&count&&index);tc.scenario=s;
 assert(tc.scenario=="save-reopen"||tc.scenario=="light-draft"||tc.scenario=="service-draft");
 tc.expectedCount=std::strtoul(count,nullptr,10);tc.timecardIndex=std::strtoul(index,nullptr,10);assert(tc.timecardIndex<tc.expectedCount);
 points_config c{};c.revision=1;c.created=0;c.points[0]={POINTS_WORK_START,1,ALARM_MODE_VIBRATE,127,(uint8_t)(tc.scenario=="service-draft"?0:12),(uint8_t)(tc.scenario=="service-draft"?2:0),0,0,0};
 assert(points_config_valid(&c));pm.initial.resize(64);points_config_encode(&c,pm.initial.data());values["5:points_utc_cfg"]=pm.initial;
 puts("MODELED_PREEXISTING_POINTS configuration/time are fixture setup, not app Save proof");
}
bool pointsLight(uint32_t*cause){
 assert(!m.terminal&&selectedForeground()&&!m.display&&!m.pins[2]&&!m.hold&&m.nativeToken&&pm.step==3&&pm.ui[13]==int(pm.draftMinute)&&!pm.writes);
 assert(tc.scenario=="light-draft"||tc.scenario=="service-draft");++m.lightEntries;
 uint64_t elapsed=tc.scenario=="service-draft"?(assert(am.timer),am.timer+1000u):7000u;
 m.ms+=elapsed;m.totalSlept+=elapsed;m.wokeAt=m.ms;tc.gpioWake=tc.scenario=="light-draft";m.levels[3]=tc.gpioWake;m.restoreExpected=true;
 *cause=tc.gpioWake?RISC_LIGHT_SLEEP_WAKE_GPIO:RISC_LIGHT_SLEEP_WAKE_TIMER;return true;
}
void pointsKvRead(uint32_t ns,const char*k,const void*,uint32_t){if(ns==5&&!strcmp(k,"points_utc_cfg"))++pm.reads;if(ns==4&&!strcmp(k,"points_utc_occ"))++pm.serviceReads;}
void pointsKvWrite(uint32_t ns,const char*k,const void*b,uint32_t n){
 assert(!m.terminal);
 if(ns==5){assert(!strcmp(k,"points_utc_cfg")&&selectedForeground()&&tc.scenario=="save-reopen"&&pm.step==5&&n==64);points_config c{};assert(points_config_decode(&c,(const uint8_t*)b,n)&&c.revision==2&&c.points[0].minute==pm.draftMinute);pm.saved.assign((const uint8_t*)b,(const uint8_t*)b+n);++pm.writes;assert(pm.writes==1);}
 if(ns==4&&!strcmp(k,"points_utc_occ")){points_ledger l{};assert(points_ledger_decode(&l,(const uint8_t*)b,n));if(l.state==ALARM_OCC_PENDING)++pm.pending;if(l.state==ALARM_OCC_ACKED)++pm.acked;}
}
void pointsFinish(bool ran,bool retained){
 assert(ran&&!retained&&tc.done&&tc.entered&&tc.homeEntries==2&&pm.contextReady&&pm.reads&&!pm.ui[17]);
 if(tc.scenario=="save-reopen")assert(pm.entries==2&&pm.reopened&&pm.writes==1&&pm.observed==pm.saved);
 else{assert(pm.entries==1&&!pm.writes&&values["5:points_utc_cfg"]==pm.initial);if(tc.scenario=="light-draft")assert(m.lightEntries==1&&m.restores==1&&tc.neutralAfterWake);else assert(pm.pending&&pm.acked&&pm.serviceReads&&pm.alarmSeen&&pm.alarmDismissed);}
 printf("PASS actual Runtime Points scenario=%s launcher_item=%u catalog=%u actual_scroll=%u entries=%u config_writes=%u context_ready=%u light=%u restored=%u service_pending=%u service_acked=%u modeled_not_device\n",tc.scenario.c_str(),tc.timecardIndex+1,tc.expectedCount,tc.launcherDrags,pm.entries,pm.writes,pm.contextReady,m.lightEntries,m.restores,pm.pending,pm.acked);
}
#endif

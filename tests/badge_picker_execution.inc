extern "C" int badge_picker_next_result(int result) {
 assert(!m.terminal);
 if(m.caseIndex==4&&pm.sawOpen&&pm.positionChanges>10){m.terminal=true;m.terminalIo=m.io;puts("PICKER_FAULT typed terminal stream result after actual next; preserve invocation custody");return -2;}
 return result;
}
/* Focused product diagnostic. Raw CST contacts only; probes never mutate UI.
 * SDK time/display are modeled; qualification is recorded per exact-source run. */
extern "C" void badge_picker_observe(const int* state,unsigned long long mask) {
 assert(state);for(unsigned i=0;i<19;i++)pm.ui[i]=state[i];
 pm.categories|=1u<<unsigned(state[2]);pm.faces|=mask;
 if(state[0]){pm.sawOpen=true;if(pm.previousPosition!=state[3])++pm.positionChanges;}
 pm.previousPosition=state[3];pm.maxJournal=std::max(pm.maxJournal,unsigned(state[9]));
 if(state[10])++pm.overflows;
}
extern "C" void badge_picker_preview(unsigned id,unsigned size,int cached,unsigned prior,int live) {
 assert(id<34&&size);++pm.previews;if(!cached)++pm.misses;else if(prior!=size)++pm.resizes;else {assert(live);++pm.liveRenders;}if(pm.previewSize[id]&&pm.previewSize[id]!=size)++pm.sizeChanges;pm.previewSize[id]=size;
}
extern "C" int badge_picker_allocation(size_t bytes) {
 ++pm.allocations;pm.cacheBytes=bytes;
 return m.caseIndex==2&&pm.allocations==1;
}
static void pickerObserve(bool completed) {
 if(m.app=="default"&&am.uiProbe)am.uiProbe();
 if(completed&&m.app=="default"&&pm.ui[0])++pm.pickerFrames;
 if(completed&&m.app=="default"&&m.visits==3&&pm.ui[12]){
  assert(pm.sawOpen&&pm.positionChanges>10&&pm.pickerFrames>10);
  if(!pm.timedStress){assert(pm.faces==((1ULL<<34)-1)&&pm.sizeChanges>10);assert(pm.coverageGesture==95);}
  else printf("TIMED_STRESS phase_dependent_coverage faces=%llu sizes=%u categories=%u all34=%u not_all_face_qualification\n",pm.faces,pm.sizeChanges,pm.categories,unsigned(pm.faces==((1ULL<<34)-1)));
  assert((pm.timedStress||pm.categories==31)&&pm.cancelled&&pm.returned&&!pm.ui[0]);
  assert(!m.terminal&&!m.deepArms&&!m.lightEntries&&!m.kvWrites);
  assert(m.appExits==2&&m.touchOpens>=1&&pm.ui[12]);
  if(m.caseIndex==2)assert(pm.allocations>=2&&pm.oom);
  if(m.caseIndex==3){assert(pm.gaps);if(!pm.timedStress)assert(pm.coverageRecoveries&&pm.coverageRecoveries==pm.coverageCancellations);}
  printf("PASS actual Runtime raw-CST picker case=%u categories=%u faces=%llu position_changes=%u picker_frames=%u cache_bytes=%zu allocations=%u preview_renders=%u cold_misses=%u resize_misses=%u live_same_size=%u size_changes=%u journal_highwater=%u overflow_samples=%u neutral_gap=%u cancellation_return=passed modeled_hardware_only\n",m.caseIndex,pm.categories,pm.faces,pm.positionChanges,pm.pickerFrames,pm.cacheBytes,pm.allocations,pm.previews,pm.misses,pm.resizes,pm.liveRenders,pm.sizeChanges,pm.maxJournal,pm.overflows,pm.gaps);
  std::exit(0);
 }
}
static void pickerKeys(uint8_t reg) {
 if(reg!=0x49||!pm.started)return;
 if(m.app=="springboard"&&!pm.returned&&m.ms-m.entered>800){m.pmic[0x49]|=8;pm.returned=true;}
 if(m.app=="default"&&m.visits==1&&pm.finishAt&&m.ms>=pm.finishAt&&!pm.cancelled){m.pmic[0x49]|=4;pm.cancelled=true;}
}
/* The coverage lane changes only raw CST contacts. Each point remains on the
 * modeled surface until the actual picker and a completed frame acknowledge it.
 * Fixed400ms stress remains a separate, explicitly non-coverage lane. */
static bool pickerCoverage(bool&down,unsigned&x,unsigned&y){
 if(pm.coverageGesture==95)return false;
 if(!pm.coverageAt)pm.coverageAt=m.ms;
 assert(m.ms-pm.coverageAt<15000); // per-gesture progress watchdog, not success
 /* A malformed/GAP observation cancels both recognizers until real neutral.
  * Never wait for an impossible held-point ACK or reset the gesture watchdog. */
 if(pm.coveragePhase&&pm.coveragePhase!=4&&(!pm.ui[6]||pm.ui[17])){
  assert(pm.ui[0]&&!pm.ui[18]);
  ++pm.coverageCancellations;pm.coveragePhase=4;pm.coverageFrame=pm.pickerFrames;
  printf("COVERAGE_CANCEL gesture=%u elapsed=%llu picker_neutral=%d quick_gate=%d visible=%d\n",pm.coverageGesture,(unsigned long long)(m.ms-pm.coverageAt),pm.ui[6],pm.ui[17],pm.ui[18]);
 }
 if(pm.coveragePhase==4){
  down=false;
  assert(pm.ui[0]&&!pm.ui[18]);
  if(pm.ui[5]||!pm.ui[6]||pm.ui[17]||pm.pickerFrames<=pm.coverageFrame)return true;
  ++pm.coverageRecoveries;pm.coveragePhase=0;pm.coveragePoint=0;pm.coverageAck=0;
  printf("COVERAGE_RECOVER gesture=%u elapsed=%llu neutral_confirmed=both completed_frames=%u watchdog_start_unchanged=%llu\n",pm.coverageGesture,(unsigned long long)(m.ms-pm.coverageAt),pm.pickerFrames,(unsigned long long)pm.coverageAt);
  return true; // completed neutral observation precedes the fresh DOWN
 }
 const unsigned kind=pm.coverageGesture%19;
 auto point=[&](unsigned n){if(kind<9){x=380-n*300/8;y=179;}else if(kind<18){x=80+n*300/8;y=179;}else{x=233;y=390-n*320/8;}};
 if(pm.coveragePhase==0){
  if(!pm.ui[0]||pm.ui[5]||!pm.ui[6]||!pm.ui[16])return true;
  pm.coverageCategory=unsigned(pm.ui[2]);pm.coveragePhase=1;pm.coveragePoint=0;pm.coverageFrame=pm.pickerFrames;pm.coverageAck=0;
 }
 if(pm.coveragePhase==1){
  down=true;point(pm.coveragePoint);
  if(pm.ui[5]&&pm.ui[13]==int(x)&&pm.ui[14]==int(y)&&pm.pickerFrames>pm.coverageFrame){
   if(!pm.coverageAck)pm.coverageAck=m.ms;
   if(m.ms-pm.coverageAck>=20){
    if(pm.coveragePoint<8){++pm.coveragePoint;pm.coverageFrame=pm.pickerFrames;pm.coverageAck=0;point(pm.coveragePoint);}
    else {pm.coveragePhase=2;pm.coverageAck=m.ms;}
   }
  }
  return true;
 }
 if(pm.coveragePhase==2){down=true;point(8);if(m.ms-pm.coverageAck<120)return true;pm.coveragePhase=3;pm.coverageFrame=pm.pickerFrames;}
 down=false;
 if(pm.ui[5]||!pm.ui[6]||!pm.ui[16]||pm.pickerFrames<=pm.coverageFrame)return true;
 const unsigned expected=(pm.coverageCategory+(kind==18))%5;
 assert(unsigned(pm.ui[2])==expected); // do not advance after a missed category
 printf("COVERAGE gesture=%u category=%u expected=%u faces=%llu sizes=%u completed_frames=%u\n",pm.coverageGesture,unsigned(pm.ui[2]),expected,pm.faces,pm.sizeChanges,pm.pickerFrames);
 ++pm.coverageGesture;pm.coveragePhase=0;pm.coverageAt=m.ms;return true;
}
static void pickerTouch(bool&down,unsigned&x,unsigned&y) {
 down=false;x=233;y=179;
 if(!pm.started||m.app!="default"||m.visits!=1)return;
 const uint64_t age=m.ms-pm.started;
 // OOM case explicitly retries a fresh long hold after neutral.
 const uint64_t opening=m.caseIndex==2?2600:1300;
 if(age>=100&&age<900){down=true;return;}
 if(m.caseIndex==2&&age>=1400&&age<2200){down=true;return;}
 if(age<opening)return;
 if(!pm.timedStress&&m.caseIndex!=4){if(pickerCoverage(down,x,y))return;}
 const uint64_t t=age-opening,step=t/1200,local=t%1200;
 static uint64_t observedStep=UINT64_MAX;if(step!=observedStep){observedStep=step;printf("PICKER_PROGRESS step=%llu t=%llu open=%d category=%d position=%d frames=%u health=%u faces=%llu sizes=%u\n",(unsigned long long)step,(unsigned long long)m.ms,pm.ui[0],pm.ui[2],pm.ui[3],pm.pickerFrames,m.health,pm.faces,pm.sizeChanges);}
 // Each category gets nine left and nine right drags then one upward drag.
 // Neutral 700ms after each contact preserves full release/settling evidence.
 if((pm.timedStress||m.caseIndex==4)&&step<95){
  if(local>=100&&local<500){
   down=true;const unsigned p=unsigned(local-100);unsigned gesture=unsigned(step%19);
   if(gesture<9)x=380-p*300/400;
   else if(gesture<18)x=80+p*300/400;
   else {x=233;y=390-p*320/400;}
  }
  return;
 }
 if(!pm.finishAt)pm.finishAt=m.ms;
 // Long physical key closes picker, then a fresh horizontal Home swipe opens
 // Springboard. A brief physical key there returns to a fresh Home invocation.
 if(pm.cancelled&&m.ms-pm.finishAt>=1200&&m.ms-pm.finishAt<1600){down=true;x=380-unsigned(m.ms-pm.finishAt-1200)*300/400;}
}

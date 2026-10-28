/* Execute actual Runtime, ProviderGraphV2, CpuPort, NativeRealtime and shared
 * Home/provider modules. Lowest GPIO, I2C registers, display transport and SDK
 * reset/timer/wall syscalls are modeled, alongside boot owner/health/confirmation/
 * diagnostic callbacks and the host file fixture. Host admission/dlopen is not
 * target Xtensa validation/relocation, native firmware boot, or physical proof. */
#include "ports/esp32s3/CpuPort.h"
#include "badge_time_sample_fixture.h"
#include "RuntimeFileFixture.h"
#include "diagnostics/StageLog.h"
#if defined(BADGE_SPRINGBOARD_GESTURE_DIAGNOSTICS) && !defined(BADGE_PERFORMANCE_CAPTURE)
#error "Selected Springboard diagnostic fixture requires actual native trace capture"
#endif
#ifdef BADGE_PERFORMANCE_CAPTURE
#include "diagnostics/Performance.h"
#endif
#include <RiscDisplaySpiV2.h>
#include <RiscProviderV2.h>
#include <algorithm>
#include "badge_fixture_idle_policy.h"
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <map>
#include <string>
#include <sys/time.h>
#include <vector>
#include "badge_settings_geometry_fixture.inc"
#include "runtime/storage/AppDataFiles.h"
#include <sys/stat.h>
#include <esp_system.h>
#include <unistd.h>
#ifdef BADGE_NATIVE_ALARMS
#include "AlarmRecords.h"
#include <RiscKeyValueV2.h>
#endif

void alarmObserve(bool completed=false);
void alarmKvWritten(uint32_t,const char*,const void*,uint32_t);
void alarmTimerArm(uint32_t);
bool alarmTouch(uint64_t age,bool&down,unsigned&x,unsigned&y);
void alarmKeys(uint8_t reg);
unsigned alarmCatalog();
void alarmEnter(const char*);
void alarmExit(const char*);
void alarmInitialize(const char*);
void alarmDeepReset();
bool alarmLight(uint32_t*);
void wifiObserve(bool);bool wifiTouch(bool&,unsigned&,unsigned&);bool wifiLight(uint32_t*);void wifiPass();void wifiKvWrite(uint32_t,const char*);
void quickInitialize();void quickObserve(bool);bool quickTouch(uint64_t,bool&,unsigned&,unsigned&);void quickKeys(uint8_t);
unsigned quickCatalog();bool quickLight(uint32_t*);void quickPass(const char*);
void quickKvRead(uint32_t,const char*,const void*,uint32_t);void quickKvWrite(uint32_t,const char*,const void*,uint32_t);


namespace {
struct Model {
    unsigned caseIndex=0;bool foreground=false,safeViewport=false,terminal=false,restoreExpected=false,restoring=false;
    std::string foregroundApp,savedProbe;unsigned lightEntries=0,wakeArms=0,wakeClears=0,restores=0,touchOpens=0,touchCloses=0;
    unsigned transactions=0,transactionResumes=0,appExits=0,kvWrites=0,terminalIo=0,lateLatches=0;
    bool touchSleeping=false,manualSleep=false,watchSpringboard=false,nativeSafeContent=false;
    std::string storeRoot,filesDraft;unsigned filesMode=0;bool filesPreviewObserved=false,filesReadOnlyObserved=false,filesShortBackObserved=false,filesHomeSent=false;
    unsigned selectedTouchOpens=0,selectedTouchCloses=0,maintenanceRefusals=0,heldIdleReads=0,selectedSeedCalls=0,firstCycleTouchOpens=0,firstCycleTouchCloses=0;
    uint64_t nativeToken=0,wokeAt=0,totalSlept=0,firstElapsed=0,restoreRowStart=0;
    bool lateOne=false,lateTwo=false;uint8_t savedBrightness=0;
    bool nativeFrameComplete=false,appVisible=false,freshForegroundVisible=false;
    uint8_t requestedBrightness=0;unsigned nativeNextRow=0,deferredBrightness=0;
    unsigned long long (*probe)(char*,size_t)=nullptr;
    std::vector<uint16_t> savedPixels;
    std::string mode,app;unsigned visits=0,target=0;size_t peakApp=0,peakProviders=0;uint64_t entered=0;bool keySent=false;std::map<std::string,bool> captured;uint8_t pmic[256]{},qmi[256]{};unsigned motionReads=0,qmiWrites=0;
    uint64_t ms=0,servicesAt=0,selectedAt=0,resetAt=0;
    unsigned health=0,io=0,rows=0,frames=0,unblanks=0,touchReads=0,contactDown=0,contactUp=0;
    unsigned i2cOpens=0,i2cCloses=0,displayOpens=0,displayCloses=0,realtimeReads=0,seedCalls=0,seeds=0,wallReads=0;
    unsigned loads=0,unloads=0,firstFrame=0,firstInput=0,promotions=0,confirmations=0,servicesCount=0;
    unsigned probeInit=0,probeFini=0,probeProgress=0,launchRefusals=0;
    bool pins[49]{},levels[49]{},i2c=false,display=false,ready=false,services=false;
    bool badSupplyOnce=false,rejectCloseOnce=false,hold=false,visibleClock=false;unsigned deepArms=0;
    uint16_t reg=0;uint8_t level=0,touchMode=1;
    std::vector<uint16_t> pixels=std::vector<uint16_t>(466*466);
    std::vector<std::string> stages,unloaded;
    std::map<void*,std::string> mapped;
} m;
struct AlarmModel {
 unsigned uiPage=0,uiField=0,uiWidth=0,uiHeight=0,nativeInputMask=0,nativeTouchX=0,nativeTouchY=0;
 int nativeRects[12][4]{};unsigned initialDraft[3]{};bool nativeDraftInitial=false;
 bool homeReady=false,pendingActive=false,pendingSeen=false,pendingDismissSent=false,editCompleted=false;
 unsigned pendingDismissals=0,intentGeneration=0,editBegins=0;int error=0;uint64_t pendingAt=0,appReadyAt=0;
 bool enabled=false,saved=false,visible=false,dismissKey=false,acked=false,reset=false,done=false;
 unsigned kind=2,stage=0,timer=0,timerArms=0,timerClears=0,gpioWakes=0,timerWakes=0,deepResets=0;
 unsigned generation=0,state=0,uiKind=0,revision=0,deadline=0,draft[3]{},ioAtFault=0,ticketFaults=0,parkedKv=0,parkedRealtime=0,ackedAtFrame=0,faultTransaction=0;
 uint32_t ticketSeconds=0,ticketDeadline=0,issuedTicket[4]{},lightTicket[4]{};
 unsigned resumeAttempts=0,resumeSuccesses=0;
 unsigned modalFrame=0,postAckFrame=0,ackWriteFrame=0;bool restoringModal=false,ackWriteObserved=false;std::vector<uint16_t> submittedModalPixels,alertPixels,postAckPixels;
 bool editing=false,retained=false,modal=false,touchGrant=false,touchSubscription=false;uint64_t visibleAt=0;void(*uiProbe)()=nullptr;
 std::string executable,draftBefore;unsigned sleepsBefore=0;
} am;
struct QuickModel {
 bool completedFrame=false;
 bool enabled=false,inAlarm=false,opened=false,saved=false,stale=false,unknown=false,settingsRead=false,homeReload=false;
 bool dndSaved=false,radioOff=false,radioOn=false,torchSeen=false,previewSeen=false,closed=false;
 unsigned radioLeaves=0,radioJoins=0,radioWrites=0,radioIdleChecks=0;
 unsigned settingsPreferenceReads=0,homePreferenceReads=0;bool preemptedOpen=false,preemptedCapture=false;
 std::map<std::string,std::vector<uint8_t>> preservedPreferences;
 unsigned pendingFrame=0,pendingSubmits=0;std::vector<uint16_t> pendingPixels;
 bool pendingDelay=false,pendingGapSent=false,pendingCancelled=false;
 unsigned ui[24]{},rects[10][4]{},writes=0,opens=0,keys=0,step=0,framesAtOpen=0,cancelChecks=0;
 uint64_t start=0,actionAt=0,faultAt=0;unsigned expectedBrightness=80;
} qm;
struct WifiModel {
 bool enabled=false,finished=false,exited=false,homeReturned=false,unsupportedFrame=false,hiddenFrame=false,retainedFault=false;unsigned state[14]{},rects[47][4]{};
 std::string editor,ssid,password,entry,message,snapshot;
 unsigned step=0,substep=0,writeAuthority=0,savedWrites=0,tx=0,ty=0,writes=0,scanStarts=0,scanCancels=0,scanCancelFailures=0,scanPolls=0,joins=0,leaves=0,frames=0;
 uint64_t nextAt=0,downUntil=0;bool tapped=false,scanBusy=false;unsigned pageMask=0;
 std::vector<uint16_t> completedPixels;
} wm;
RiscCpu::Port* cpu;
RiscBoot::Runtime* running;
bool owner(){return true;}
uint64_t now(){return m.ms;}
bool navigationTarget(){return m.target<6||m.target==21;}
unsigned catalogIndex(){return am.enabled?alarmCatalog():m.target==21?6:m.target;}
bool selectedForeground(){return m.foreground&&m.app==m.foregroundApp;}
// Match Co5300DisplayIdf6.inc: open starts black at zero brightness, and only
// a complete application frame can apply a queued nonzero request. Visibility
// here is modeled raster plus checked brightness, never physical panel proof.
bool visibleRasterContent(){
 unsigned count=0;
 for(unsigned y=0;y<466;y++)for(unsigned x=0;x<466;x++){
  int dx=int(2*x+1)-466,dy=int(2*y+1)-466;
  if(dx*dx+dy*dy<=466*466&&m.pixels[y*466+x]&&++count>100)return true;
 }
 return false;
}
void observeVisibility(){
 if(!m.display||!m.nativeFrameComplete||!m.level)return;
 if(!visibleRasterContent())return;
 m.appVisible=true;
 if(selectedForeground()&&!m.lightEntries)m.freshForegroundVisible=true;
 if(m.app=="default"&&!navigationTarget()&&m.ms-m.entered>=3000)m.visibleClock=true;
}
static unsigned long long probeState(std::string* text=nullptr){char b[256]={0};auto value=m.probe?m.probe(b,sizeof(b)):0;if(text)*text=b;return value;}
static void foregroundComplete(){
 if(am.enabled)return;
 if(!selectedForeground()||m.caseIndex==17||m.lightEntries<3||m.restoreExpected||m.restoring||m.ms-m.wokeAt<1800)return;
 assert(m.visits==(m.foregroundApp=="springboard"?2u:3u)&&m.appExits==m.visits-1);
 assert(m.display&&m.pins[2]&&m.restores==m.lightEntries&&m.touchOpens==m.firstCycleTouchOpens+m.lightEntries&&m.touchCloses==m.firstCycleTouchCloses+m.lightEntries-1);
 assert(m.transactions==m.transactionResumes&&m.transactionResumes>=m.lightEntries&&!m.nativeToken);
 assert(!m.deepArms&&!m.hold&&m.lateLatches>=m.lightEntries*2&&m.seedCalls==m.selectedSeedCalls&&m.freshForegroundVisible&&m.appVisible&&m.level);
 std::string current;auto progress=probeState(&current);
 if(m.foregroundApp=="settings"||m.foregroundApp=="calculator"||m.foregroundApp=="file_browser"){assert(progress==1&&current==m.savedProbe);assert(!m.kvWrites);}
 if(m.foregroundApp=="file_browser")assert(m.filesMode==2&&m.filesDraft=="a"&&m.filesReadOnlyObserved);
 if(m.foregroundApp=="stopwatch"){assert(progress>=m.firstElapsed+m.totalSlept);assert(m.kvWrites==1);}
 if(m.foregroundApp=="wifi_settings"){assert(!progress&&!m.kvWrites);}
 if(m.caseIndex==18)assert(m.maintenanceRefusals);
 printf("PASS actual Runtime foreground Light case=%u app=%s cycles=%u same_invocation=passed native_transactions=%u fresh_visibility=passed native_dark_until_complete=passed pixels_466x466=restored touch_reopens=%u late_latches=%u elapsed=%llu active_maintenance_refusal=%s no_deep_Save_Cancel_or_Home hardware_unverified\n",m.caseIndex,m.app.c_str(),m.lightEntries,m.transactionResumes,m.touchOpens,m.lateLatches,progress,m.caseIndex==18?"passed":"not-in-case");
 std::exit(0);
}
static void fixtureBoundaryObserve();
void delay(uint32_t ms){assert(!m.terminal);assert(ms<=5000);m.ms+=ms;assert(m.ms<(am.enabled?2000000:m.foreground?200000:badge_fixture_home_watchdog_ms()));fixtureBoundaryObserve();}



/* Native clock availability is not the fixture's normal stop signal.
 * Preserve every scenario observation/bound and stop predicate below, but
 * end Home through the explicit host-only observation loop gate. Runtime
 * controlExitRequested remains unbound: its native Home navigation action is
 * not a stop-run request. This is not a product/user return or target execution. */
static void fixtureBoundaryObserve(){assert(!m.terminal);if(running){const auto u=running->grantUsage();m.peakApp=std::max(m.peakApp,u.hostLive+u.foregroundLive);m.peakProviders=std::max(m.peakProviders,u.graphLive);}assert(++m.health<(qm.enabled&&m.caseIndex==40?20000000u:m.foreground?8000000u:!navigationTarget()?4000000u:1000000u));if(am.enabled)alarmObserve();assert(m.ms<(am.enabled?2000000:m.foreground?200000:badge_fixture_home_watchdog_ms()));foregroundComplete();}
static bool fixtureContinue(){fixtureBoundaryObserve();return !am.done&&!(m.target==10&&m.ms>5000)&&!(!am.enabled&&m.app=="default"&&m.visits>=(m.caseIndex==19?3u:4u)&&m.ms-m.entered>1500&&m.rows%466==0);}
static bool fixtureStopRequested;
bool fixtureMonotonic(uint32_t* out){assert(!m.terminal);*out=static_cast<uint32_t>(m.ms);return true;}
bool fixtureObservationStop(){
 if(!fixtureStopRequested)fixtureStopRequested=!fixtureContinue();
 return fixtureStopRequested;
}
extern "C" int badge_fixture_home_continue(){
 const bool keep=!fixtureObservationStop();
 if(!keep)puts("FIXTURE_CONTROLLED_HOME_EXIT completed observation; not a user return or target instruction execution");
 return keep?1:0;
}
bool health(risc_runtime_health_v1* out){
 assert(!m.terminal);out->uptime_ms=static_cast<uint32_t>(m.ms);return true;
}
bool log(const char*line){puts(line);if(am.enabled&&m.app=="default"&&strstr(line,"WATCH_KEY arming=start")){am.homeReady=true;am.appReadyAt=m.ms;}if(strstr(line,"WATCH_KEY sleep-enter=manual"))m.manualSleep=true;if(strstr(line,"launch=failed")){fprintf(stderr,"Unexpected missing app fallback\n");abort();}return true;}
bool confirm(){return true;}
bool gpioOpen(uint8_t pin,bool output,bool initial,bool pullup){
    assert(!m.terminal);++m.io;if(pin==2)++m.touchOpens;assert(((pin==1||pin==2)?output:(pin==3&&!output))&&!initial&&!pullup&&!m.pins[pin]);
    m.pins[pin]=true;m.levels[pin]=initial;if(pin==2){m.resetAt=m.ms;m.touchSleeping=false;}return true;
}
bool gpioWrite(uint8_t pin,bool high){assert(!m.terminal);++m.io;assert(pin==2&&m.pins[pin]);if(high){assert(!m.levels[pin]&&m.ms-m.resetAt>=10);}else {m.resetAt=m.ms;m.touchSleeping=false;}m.levels[pin]=high;return true;}
bool gpioRead(uint8_t pin,bool* out){assert(!m.terminal);++m.io;if(m.foreground&&!am.enabled&&pin==3)m.levels[3]=m.lightEntries&&m.ms-m.wokeAt<400;assert((pin==1||pin==2||pin==3)&&m.pins[pin]);const bool held=pin==3&&m.target==7&&m.ms-m.entered<badge_fixture_idle_fault_end_ms();if(held&&m.ms-m.entered>=BADGE_FIXTURE_HOME_IDLE_MS)++m.heldIdleReads;*out=held?true:m.levels[pin];return true;}
bool gpioPwm(uint8_t,uint32_t,uint16_t,uint16_t){assert(false);return false;}
bool gpioClose(uint8_t pin){assert(!m.terminal);++m.io;if(pin==2)++m.touchCloses;assert((pin==1||pin==2||pin==3)&&m.pins[pin]);m.pins[pin]=false;return true;}
bool i2cOpen(uint8_t physical,uint8_t sda,uint8_t scl,uint32_t hz){
    assert(!m.terminal);++m.io;++m.i2cOpens;assert(physical==0&&sda==15&&scl==14&&hz==400000&&!m.i2c);
    m.i2c=true;return m.mode!="probe-i2c-retained";
}
bool i2cClose(uint8_t physical){
    assert(!m.terminal);++m.io;++m.i2cCloses;assert(physical==0&&m.i2c);
    if(m.mode=="probe-i2c-retained")return false;
    m.i2c=false;return true;
}
bool i2cTransfer(uint8_t physical,uint8_t address,const uint8_t* tx,size_t tn,uint8_t* rx,size_t rn,uint32_t timeout){
    assert(!m.terminal);++m.io;assert(physical==0&&m.i2c&&timeout==20);
    if(address==0x6b){assert(tx&&((tn==1&&rx&&rn)||(tn==2&&!rx&&!rn)));if(tn==2){++m.qmiWrites;assert(tx[0]==2||tx[0]==8);assert(!((m.qmi[tx[0]]^tx[1])&~(tx[0]==2?1:3)));m.qmi[tx[0]]=tx[1];}else{assert((unsigned)tx[0]+rn<=256);memcpy(rx,m.qmi+tx[0],rn);assert(tx[0]!=0x35);}return true;}
    if(address==0x34){
        assert(tx&&((tn==1&&rx&&rn>=1&&rn<=2)||(tn==2&&!rx&&!rn)));
        if(tn==2){assert(tx[0]==0x41||tx[0]==0x49);if(tx[0]==0x49){assert(!(tx[1]&~0x0c));m.pmic[0x49]&=~tx[1];}else{assert(!((m.pmic[0x41]^tx[1])&~0x0c));m.pmic[0x41]=tx[1];}return true;}
        // Read-only state observation at an actual PMIC input read, never a clock query or new frame.
        if(tx[0]==0x49)fixtureBoundaryObserve();
        if(am.enabled)alarmKeys(tx[0]);
        if(!am.enabled&&tx[0]==0x49&&!m.keySent&&m.ms-m.entered>3500&&((m.caseIndex==19&&m.app=="springboard")||(m.caseIndex==20&&m.app=="default"))){m.pmic[0x49]|=8;m.keySent=true;}
        if(!am.enabled&&!m.foreground&&tx[0]==0x49&&m.app!="default"&&m.app!="springboard"&&!m.keySent&&m.ms-m.entered>((m.app=="settings"||m.app=="file_browser")?5000u:3000u)){m.pmic[0x49]|=8;m.keySent=true;}
        if(m.caseIndex==21&&m.app=="file_browser"&&tx[0]==0x49&&m.ms-m.entered>6500&&!m.filesHomeSent){assert(m.filesShortBackObserved);m.pmic[0x49]|=4;m.filesHomeSent=true;}
        if(!am.enabled&&selectedForeground()&&tx[0]==0x49&&m.lightEntries){
         if(!m.lateOne&&m.ms-m.wokeAt>=650){m.pmic[0x49]|=8;m.lateOne=true;++m.lateLatches;}
         if(!m.lateTwo&&m.ms-m.wokeAt>=1100){m.pmic[0x49]|=8;m.lateTwo=true;++m.lateLatches;}
        }
        memcpy(rx,m.pmic+tx[0],rn);return true;
    }
    assert(address==0x5a&&m.pins[2]&&m.levels[2]&&m.ms-m.resetAt>=60);
    assert(!(tn&&rn));
    if(tn){assert(tx&&tn==2&&!rx);m.reg=uint16_t((tx[0]<<8)|tx[1]);if(m.reg==0xd11e)m.touchMode=0x1e;if(m.reg==0xd101)m.touchMode=1;if(m.reg==0xd105)m.touchSleeping=true;m.selectedAt=m.ms;return true;}
    assert(!tx&&rx&&m.ms-m.selectedAt>=2);
    if(m.reg==0x0002){assert(rn==4||rn==2);memset(rx,0,rn);rx[1]=m.touchMode;return true;}
    if(m.reg==0xd1f8){assert(rn==4);unsigned width=m.mode=="home-touch-failure"?465:466;rx[0]=width;rx[1]=width>>8;rx[2]=466&255;rx[3]=466>>8;return true;}
    if(m.reg==0xd1fc||m.reg==0xd204){assert(rn==4);memset(rx,0,rn);return true;}
    assert(m.reg==0xd000&&rn==10);++m.touchReads;fixtureBoundaryObserve();memset(rx,0,rn);rx[6]=0xab;
    const auto age=m.ms-m.entered;bool down=false;unsigned x=233,y=233;
    if(!am.enabled&&navigationTarget()&&m.app=="default"&&m.visits==1){down=age>=1600&&age<1900;x=age<1750?310:100;y=179;}
    if((!am.enabled||m.caseIndex==28)&&m.app=="settings"&&!m.foreground){
        if(age>=1100&&age<1260){down=true;badgeSettingsTap(BS_ROOT_TIME,x,y);}
        if(age>=1700&&age<1860){down=true;badgeSettingsTap(BS_YEAR,x,y);} // Year editor.
        if(age>=2300&&age<2460){down=true;badgeSettingsTap(BS_INCREMENT,x,y);} // 2026 -> 2027, draft only.
        if(age>=2900&&age<3060){down=true;badgeSettingsTap(BS_BACK,x,y);} // Return without committing.
        if(age>=3500&&age<3660){down=true;badgeSettingsTap(BS_SAVE,x,y);} // Explicit Save.
    }
    if(m.app=="bringup"){if(age>=1000&&age<1160){down=true;x=155;y=330;}if(age>=1800&&age<1960){down=true;x=233;y=367;}}
    if(m.app=="springboard"&&m.caseIndex!=19&&!(m.foreground&&m.foregroundApp=="springboard")){
        unsigned tap=1200;
        if(m.watchSpringboard){
         // Independent axial lattice hit centers; stable radial ordering matches
         // catalog order while dimensions come from the selected native panel.
         struct Point{int x,y;};std::vector<Point> points;
         for(int q=-2;q<=2;++q)for(int r=-2;r<=2;++r)if(std::abs(q+r)<=2)points.push_back({864*q+432*r,748*r});
         // Production sorts by floored integer radius, not squared radius;
         // retain insertion order for equal radii across every lattice ring.
         auto radius=[](Point p){unsigned n=unsigned(p.x*p.x+p.y*p.y),lo=0,hi=4096;while(lo<hi){unsigned mid=(lo+hi+1)/2;if(mid*mid<=n)lo=mid;else hi=mid-1;}return lo;};
         std::stable_sort(points.begin(),points.end(),[&](Point a,Point b){return radius(a)<radius(b);});
         assert(catalogIndex()<points.size());
         if(age>=tap&&age<tap+160){down=true;x=233+(points[catalogIndex()].x*466/240)/16;y=233+(points[catalogIndex()].y*466/240)/16;}
        }else{
        if(m.target>=4){if(age>=900&&age<1060){down=true;x=233;y=369;}tap=2000;}
        if(age>=tap&&age<tap+160){down=true;x=69+16+(m.target%2)*148+74;y=69+56+((m.target%4)/2)*109+54;}
        }
    }
    if(m.app=="file_browser" && (!m.foreground || !m.lightEntries)){
     // Actual safe-content Watch transform, not legacy fixed framebuffer taps.
     int lx=0,ly=0;
     if(m.caseIndex==21){
      // First admitted root entry may be a provider folder or app file.
      // Follow the first row twice, then Read the resulting file details.
      if((age>=1100&&age<1260)||(age>=1900&&age<2060)){down=true;lx=120;ly=85;}
      if(age>=2700&&age<2860){down=true;lx=120;ly=210;}
     }else if(m.caseIndex==22){
      if(age>=1100&&age<1260){down=true;lx=210;ly=20;} // Options.
      if(age>=1900&&age<2060){down=true;lx=120;ly=85;} // Find by name.
      if(age>=2700&&age<2860){down=true;lx=52;ly=86;} // Native keyboard 'a', leave unsaved.
     }
     if(down){x=69+(unsigned)lx*328/240;y=69+(unsigned)ly*328/240;}
    }
    if(selectedForeground()&&!m.lightEntries){
     if(m.app=="settings"){
      if(age>=1100&&age<1260){down=true;badgeSettingsTap(BS_ROOT_TIME,x,y);} // Open Time and Date.
      if(age>=1900&&age<2060){down=true;badgeSettingsTap(BS_YEAR,x,y);} // Open Year numeric editor.
      if(age>=2700&&age<2860){down=true;badgeSettingsTap(BS_INCREMENT,x,y);} // Change draft only; never Save.
     }
     if(m.app=="calculator"){
      const unsigned extent=m.safeViewport?328:466,origin=m.safeViewport?69:0;
      const unsigned cell=(extent-8)/4,row=(extent-92)/5;
      if(m.nativeSafeContent){
       // Native calculator_key_rect: content third-height header, four columns,
       // five rows; center actual inset rectangles (not a 240px coordinate map).
       const auto key=[&](unsigned index){
        const unsigned c=index%4,r=index/4,top=extent/3,uw=extent-8,uh=extent-top-4;
        const unsigned left=4+c*uw/4,right=4+(index==18?4:c+1)*uw/4;
        const unsigned first=top+r*uh/5,last=top+(r+1)*uh/5;
        down=true;x=origin+left+2+(right-left-4)/2;y=origin+first+2+(last-first-4)/2;
       };
       if(age>=1100&&age<1260)key(4); // 7
       if(age>=1900&&age<2060)key(15); // +
       if(age>=2700&&age<2860)key(5); // 8; leave pending expression.
      }else{
       if(age>=1100&&age<1260){down=true;x=origin+4+cell/2;y=origin+88+row+row/2;} // 7
       if(age>=1900&&age<2060){down=true;x=origin+4+3*cell+cell/2;y=origin+88+3*row+row/2;} // +
       if(age>=2700&&age<2860){down=true;x=origin+4+cell+cell/2;y=origin+88+row+row/2;} // 8, leave pending expression.
      }
     }
     if(m.app=="stopwatch"&&age>=1100&&age<1260){
      const unsigned extent=m.safeViewport?328:466,origin=m.safeViewport?69:0;down=true;
      if(m.nativeSafeContent){
       // un_footer(extent,extent,0,3): exact Start button center.
       x=origin+10+((extent-16)/3-4)/2;y=origin+extent-32;
      }else{x=origin+extent/4;y=origin+extent-64;}
     } // Start.
    }
    if(am.enabled)alarmTouch(age,down,x,y);
    rx[5]=down;
    if(qm.enabled&&m.app=="default"){uint64_t qa=m.ms-am.appReadyAt;if(m.caseIndex==38&&qm.pendingDelay&&!qm.pendingGapSent&&qm.ui[22]){rx[5]=2;qm.pendingGapSent=true;}if((m.caseIndex==38&&qa>=2600&&qa<2700)||(m.caseIndex==41&&qa>=6700&&qa<6800))rx[5]=2;} // Explicit malformed CST multi-contact report; real provider invalidates input.
    rx[0]=down?6:0;rx[1]=x>>4;rx[2]=y>>4;rx[3]=uint8_t((x&15)<<4|(y&15));
    if(down)++m.contactDown;else if(m.contactDown)++m.contactUp;
    return true;
}
bool displayAccepts(const risc_hardware_device_v1& d,uint8_t physical){
    if(physical!=2||!d.config||d.config_version!=2||d.config_size!=sizeof(risc_hw_spi_display_v2)||
       strcmp(d.compatible,"chipone,co5300")||strcmp(d.config_type,"display.spi"))return false;
    const auto& c=*static_cast<const risc_hw_spi_display_v2*>(d.config);
    return c.base.width==466&&c.base.height==466&&c.base.offset_x==6&&!c.base.offset_y&&!c.base.rotation&&
        c.base.reset==1&&c.base.bus.sclk==38&&c.base.bus.mosi==4&&c.base.bus.miso==5&&c.data2==6&&c.data3==7&&c.base.cs==12;
}
bool displayOpen(const risc_hardware_device_v1& d,uint8_t physical){
    assert(!m.terminal);++m.io;++m.displayOpens;if(m.restoreExpected){m.restoring=true;m.restoreRowStart=m.rows;}assert(displayAccepts(d,physical)&&!m.display&&!m.pins[1]);m.display=true;
    m.level=m.requestedBrightness=0;m.nativeFrameComplete=false;m.nativeNextRow=0;
    std::fill(m.pixels.begin(),m.pixels.end(),0);
    return m.mode!="home-display-failure"&&m.mode!="probe-start-retained";
}
bool displayRow(uint32_t y,const uint16_t* row,size_t width){
    assert(!m.terminal);++m.io;assert(m.display&&row&&width==466&&y==m.nativeNextRow);
    if(!m.nativeFrameComplete)assert(!m.level);
    if(m.restoring)assert(std::equal(row,row+width,m.savedPixels.begin()+y*width));
    if(qm.enabled&&m.caseIndex==38&&m.app=="default"&&y==0&&!qm.pendingDelay){if(am.uiProbe)am.uiProbe();if(qm.ui[22]){m.ms+=500;qm.pendingDelay=true;}} // Explicit slow-display fault before the pending frame completes.
    std::copy(row,row+width,m.pixels.begin()+y*width);++m.rows;m.nativeNextRow=(y+1)%466;if(y==465){++m.frames;
 if(!m.nativeFrameComplete){m.nativeFrameComplete=true;m.level=m.requestedBrightness;if(m.level)++m.unblanks;}
 observeVisibility();
 if(am.enabled)alarmObserve(true);
 fixtureBoundaryObserve();
 if(m.app=="file_browser")probeState();
 const char*dir=getenv("BADGE_PROTOTYPE_RASTERS");const std::string picture=am.enabled&&am.modal?m.app+"-alarm-visible":am.enabled&&am.acked&&!am.modal?m.app+"-alarm-dismissed":selectedForeground()?m.app+(m.lightEntries?"-wake-"+std::to_string(m.lightEntries):(m.probe&&probeState()?"-active":"-initial")):m.app=="settings"&&m.seedCalls?"settings-saved":m.app;
 if(dir&&*dir&&(!m.captured[picture]||(m.app=="default"&&m.visits==4)||(!navigationTarget()&&m.visibleClock&&!m.captured["visible-clock"]))){m.captured[picture]=true;if(!navigationTarget()&&m.visibleClock)m.captured["visible-clock"]=true;std::string path=std::string(dir)+"/"+std::to_string(m.caseIndex)+"-"+picture+".ppm";std::string temporary=path+".tmp";FILE*f=fopen(temporary.c_str(),"wb");assert(f);fprintf(f,"P6\n466 466\n255\n");for(uint16_t p:m.pixels){unsigned char c[3]={uint8_t(((p>>11)&31)*255/31),uint8_t(((p>>5)&63)*255/63),uint8_t((p&31)*255/31)};assert(fwrite(c,1,3,f)==3);}assert(!fclose(f));assert(!std::rename(temporary.c_str(),path.c_str()));}
 }return true;
}
bool displayBrightness(uint8_t level){
 assert(!m.terminal);++m.io;assert(m.display);m.requestedBrightness=level;
 if(level&&!m.nativeFrameComplete){assert(!m.level);++m.deferredBrightness;return true;}
 if(m.restoring&&level){assert(m.rows-m.restoreRowStart==466&&level==m.savedBrightness);m.restoring=m.restoreExpected=false;++m.restores;}
 if(level)++m.unblanks;m.level=level;observeVisibility();
 // Brightness completion can make an already completed frame visible; it is not a new frame.
 fixtureBoundaryObserve();return true;
}
bool displayClose(){
    assert(!m.terminal);++m.io;++m.displayCloses;assert(m.display);
    if(selectedForeground()){m.savedPixels=m.pixels;m.savedBrightness=m.level;}
    if(m.mode=="probe-start-retained"||m.mode=="home-shutdown-retained")return false;
    if(m.rejectCloseOnce){m.rejectCloseOnce=false;return false;}
    m.display=false;m.level=m.requestedBrightness=0;m.nativeFrameComplete=false;m.nativeNextRow=0;return true;
}
bool bind(RiscBoot::Runtime& runtime){return cpu->bind(runtime);}
}


static std::map<std::string,std::vector<uint8_t>> values;
#ifdef BADGE_NATIVE_ALARMS
static constexpr uint32_t modeledKvMax=RISC_KEY_VALUE_V2_BLOB_MAX;
#else
static constexpr uint32_t modeledKvMax=RISC_KEY_VALUE_BLOB_MAX;
#endif
static const RiscBoot::KeyValueBackend keyValue{nullptr,
 [](void*,uint32_t ns,const char*k,void*b,uint32_t capacity,uint32_t*size)->int32_t{
  assert(!m.terminal);if(am.enabled&&m.nativeToken)++am.parkedKv;auto i=values.find(std::to_string(ns)+":"+k);if(i==values.end()){*size=0;return RISC_KEY_VALUE_NOT_FOUND;}*size=i->second.size();if(capacity<*size)return RISC_KEY_VALUE_BUFFER_SMALL;memcpy(b,i->second.data(),*size);quickKvRead(ns,k,b,*size);return 0;},
 [](void*,uint32_t ns,const char*k,const void*b,uint32_t size)->int32_t{assert(!m.terminal);if(!b||!size||size>modeledKvMax)return RISC_KEY_VALUE_INVALID;if(am.enabled&&m.nativeToken)++am.parkedKv;if(selectedForeground())++m.kvWrites;values[std::to_string(ns)+":"+k]=std::vector<uint8_t>((const uint8_t*)b,(const uint8_t*)b+size);alarmKvWritten(ns,k,b,size);quickKvWrite(ns,k,b,size);wifiKvWrite(ns,k);return 0;},modeledKvMax};
static int64_t editedSeed=0,firstHomeEpoch=0,lastHomeEpoch=0;static unsigned homeValidReads=0;
static timeval seeded{};static uint64_t seedMs;
static esp_reset_reason_t resetReason=ESP_RST_POWERON;
esp_reset_reason_t esp_reset_reason(){return resetReason;}
int64_t esp_timer_get_time(){return m.ms*1000;}
static int wallRead(timeval*out,void*){*out=seeded;if(am.enabled){uint64_t us=out->tv_usec+(m.ms-seedMs)*1000;out->tv_sec+=us/1000000;out->tv_usec=us%1000000;}else out->tv_sec+=(m.ms-seedMs)/1000;return 0;}
static int wallSet(const timeval*in,const void*){seeded=*in;seedMs=m.ms;return 0;}
#define gettimeofday wallRead
#define settimeofday wallSet
#include "ports/esp32s3/NativeRealtime.cpp"
#undef gettimeofday
#undef settimeofday
#define BADGE_WIFI_EXECUTION 1
#include "badge_alarm_execution.inc"
#include "badge_quick_execution.inc"
#include "badge_wifi_execution.inc"
namespace RiscDiagnostics {uint64_t monotonicUs(){return m.ms*1000;}void timestamped(const char*f,...){char b[1024];va_list a;va_start(a,f);vsnprintf(b,sizeof(b),f,a);va_end(a);puts(b);}}
// Read-only fixture observation: compare the app's actual Runtime read buffer
// with the host medium. Never inject contents or change the app/provider state.
extern "C" void badge_files_observe(unsigned mode,const char*draft,const char*path,const unsigned char*bytes,unsigned count,int readonly){
 m.filesMode=mode;m.filesDraft=draft;if(readonly)m.filesReadOnlyObserved=true;
 if(m.caseIndex==21&&mode==3&&m.filesPreviewObserved&&m.keySent){m.filesShortBackObserved=true;}
 if(mode!=4||!count||m.filesPreviewObserved)return;
 assert(path&&*path&&!strstr(path,"..")&&count<=4096&&m.filesReadOnlyObserved);
 std::string relative=path[0]=='/'?path+1:path;
 assert(relative.size()>4&&(relative.substr(relative.size()-4)==".elf"||relative.substr(relative.size()-5)==".json"));
 FILE*f=fopen((m.storeRoot+"/"+relative).c_str(),"rb");assert(f);
 std::vector<unsigned char> expected(count);assert(fread(expected.data(),1,count,f)==count);assert(!fclose(f));
 assert(!memcmp(expected.data(),bytes,count));m.filesPreviewObserved=true;
 printf("INSTALLED_FILES actual_allowlisted_preview=%s exact_bytes=%u readonly_callbacks_absent=passed\n",relative.c_str(),count);
}
extern "C" void badge_probe_register(unsigned long long (*probe)(char*,size_t)){m.probe=probe;}
extern "C" void badge_sleep_transaction(int begin,unsigned long long token,int result){
 if(!selectedForeground())return;
 if(begin&&result==0){assert(token&&!m.nativeToken);m.nativeToken=token;++m.transactions;}
 else if(!begin){assert(token==m.nativeToken);if(result==0||result==2){m.nativeToken=0;++m.transactionResumes;}}
}
extern "C" void badge_app_enter(const char*n){
 m.app=n;m.entered=m.ms;m.keySent=false;m.appVisible=false;++m.visits;printf("APP_ENTER %s %u\n",n,m.visits);
 if(am.enabled){alarmEnter(n);return;}
 if(selectedForeground()){m.selectedTouchOpens=m.touchOpens;m.selectedTouchCloses=m.touchCloses;m.selectedSeedCalls=m.seedCalls;assert(m.seedCalls==(m.foregroundApp=="stopwatch"?0u:1u));}
 if(m.caseIndex==19){assert(m.visits<=3&&!strcmp(n,m.visits==2?"springboard":"default"));return;}
 const char*targets[]={"settings","bringup","calculator","stopwatch","battery","wifi_settings","file_browser"};
 if(!navigationTarget()){assert(m.visits==1&&!strcmp(n,"default"));return;}
 assert(m.visits<=4);assert(!strcmp(n,m.visits==1||m.visits==4?"default":m.visits==2?"springboard":targets[catalogIndex()]));
}
extern "C" void badge_app_exit(const char*n){
 if(am.enabled){alarmExit(n);return;}
 if(navigationTarget())assert(m.appVisible);
 ++m.appExits;if(selectedForeground()){assert(m.caseIndex==17&&m.terminal);return;}
 if(!strcmp(n,"settings")){risc_realtime_snapshot_v1 snapshot{};snapshot.struct_size=sizeof(snapshot);assert(m.seedCalls==2&&RiscCpu::NativeRealtime::read(&snapshot)==0&&snapshot.validity==RISC_REALTIME_VALID&&editedSeed>=1798761600&&editedSeed<1798761660&&snapshot.epoch_seconds==editedSeed+(int64_t)((m.ms-seedMs)/1000));printf("NATIVE_SETTINGS_UI explicit_Save native_seed_calls=%u readback_utc=%lld no_RTC_grant\n",m.seedCalls,(long long)snapshot.epoch_seconds);}
 if(!strcmp(n,"file_browser")){std::string state;assert(probeState(&state)==1&&!m.kvWrites&&m.filesMode==3&&m.filesPreviewObserved&&m.filesReadOnlyObserved&&m.filesShortBackObserved&&m.filesHomeSent);printf("INSTALLED_FILES read_only_allowlist visible=%s no_mutation_or_execution\n",state.c_str());}
 if(!strcmp(n,"bringup")){assert(!m.motionReads);puts("QMI_BRINGUP optional motion unavailable; only checked sensor-idle owner, zero sampling");}
 printf("APP_EXIT %s\n",n);
}
int main(int argc,char**argv){
 setvbuf(stdout,nullptr,_IONBF,0);assert(argc==3);m.storeRoot=argv[1];m.caseIndex=std::strtoul(argv[2],nullptr,10);
#ifdef BADGE_TIME_SERVER_SYNC
 if(m.caseIndex==46){badgeTimeSample.success=true;m.caseIndex=44;}
 badgeTimeSample.allowed=[](){return !m.terminal;};
#endif
#ifdef BADGE_QUICK_ACTIONS
 assert(m.caseIndex<46);qm.enabled=m.caseIndex>=34&&m.caseIndex<42;
#ifdef BADGE_WIFI_NATIVE_GEOMETRY
 wm.enabled=m.caseIndex>=42;
#else
 assert(m.caseIndex<42);
#endif
#else
 assert(m.caseIndex<34);
#endif
 am.enabled=m.caseIndex>=23;m.foreground=(m.caseIndex>=11&&m.caseIndex<19)||m.caseIndex==22;m.target=m.caseIndex;m.mode="prototype";m.watchSpringboard=getenv("BADGE_WATCH_SPRINGBOARD")!=nullptr;if(m.caseIndex==19)m.target=0;if(m.caseIndex==20)m.target=6;
 if(m.foreground&&m.caseIndex!=22){const unsigned selected[]={0,2,3,0,4,5,2,4};const char*names[]={"settings","calculator","stopwatch","springboard","battery","wifi_settings","calculator","battery"};m.target=selected[m.caseIndex-11];m.foregroundApp=names[m.caseIndex-11];const char*safe=getenv("BADGE_PROTOTYPE_SAFE_VIEWPORT");m.safeViewport=safe&&(std::string(",")+safe+",").find(","+m.foregroundApp+",")!=std::string::npos;}
 if(m.caseIndex==22){m.target=21;m.foregroundApp="file_browser";}
 const char*native=getenv("BADGE_PROTOTYPE_NATIVE_SAFE_CONTENT");m.nativeSafeContent=!m.foregroundApp.empty()&&native&&(std::string(",")+native+",").find(","+m.foregroundApp+",")!=std::string::npos;
 if(m.nativeSafeContent)assert(m.safeViewport);
 m.qmi[0]=5;m.qmi[0x2e]=1;m.qmi[0x36]=0x20; m.pmic[3]=0x4a;m.pmic[0x80]=1;m.pmic[0x82]=18;m.pmic[0]=0x28;m.pmic[1]=0x20;m.pmic[0x18]=0x0a;m.pmic[0x68]=1;m.pmic[0xa4]=42;m.pmic[0x34]=0x10;m.pmic[0x35]=4;
 if(m.target==9){m.qmi[8]=3;m.qmi[3]=0x16;m.qmi[4]=0x26;}
 if(m.target==10){m.qmi[2]=2;m.qmi[8]=3;}
 if(am.enabled)alarmInitialize(argv[0]);if(qm.enabled)quickInitialize();
 // These historical cases qualify an explicit saved Direct Deep preference,
 // not the product default. Keep their Deep/held-key assertions unchanged.
 if(m.caseIndex==6||m.caseIndex==7||m.caseIndex==8||m.caseIndex==9||m.caseIndex==20)
  values["1:sleep_mode"]={0x53,1,1,0xa4};
#ifdef BADGE_PERFORMANCE_CAPTURE
 RiscPerf::configure([](){return m.ms*1000u;},owner,true);
#endif
 RiscCpu::NativeRealtime::configure(owner);RiscCpu::NativeRealtime::start();
 if(am.enabled&&m.caseIndex==33&&am.reset){risc_realtime_snapshot_v1 snap{};snap.struct_size=sizeof(snap);assert(RiscCpu::NativeRealtime::read(&snap)==0&&snap.validity==RISC_REALTIME_UNSET&&!snap.epoch_seconds);}
 if(!am.enabled&&(!navigationTarget()||(m.foreground&&m.foregroundApp=="stopwatch")))assert(RiscCpu::NativeRealtime::seed(1792900000,0)==0);
 RiscCpu::Hardware h{};h.owner=owner;h.now=now;h.sleep=delay;h.gpioOpen=gpioOpen;h.gpioWrite=gpioWrite;h.gpioRead=gpioRead;h.gpioPwm=gpioPwm;h.gpioClose=gpioClose;h.i2cOpen=i2cOpen;h.i2cTransfer=i2cTransfer;h.i2cClose=i2cClose;
 h.wakeValid=[](uint8_t pin){if(qm.enabled||wm.enabled)assert(!m.terminal);return (am.enabled||m.foreground)&&pin==3;};
 h.wakeArm=[](uint8_t pin,bool high){if(qm.enabled||wm.enabled)assert(!m.terminal);assert((am.enabled||selectedForeground())&&pin==3&&high&&!m.terminal);++m.wakeArms;return true;};
 h.lightSleep=[](uint32_t*cause){if(qm.enabled||wm.enabled)assert(!m.terminal);
  if(am.enabled)return alarmLight(cause);
  assert(selectedForeground()&&!m.terminal&&!m.display&&!m.pins[2]&&!m.hold&&m.nativeToken);
  assert(!(m.qmi[8]&3)&&(m.qmi[2]&1)&&!m.motionReads&&m.savedBrightness&&m.freshForegroundVisible);
  if(!m.lightEntries){assert(m.touchCloses>m.selectedTouchCloses);m.firstCycleTouchOpens=m.touchOpens;m.firstCycleTouchCloses=m.touchCloses;}
  else assert(m.touchOpens==m.firstCycleTouchOpens+m.lightEntries&&m.touchCloses==m.firstCycleTouchCloses+m.lightEntries);
  if(m.caseIndex==18)assert(m.ms-m.entered>=badge_fixture_idle_fault_end_ms()&&m.maintenanceRefusals);
  std::string text;auto progress=probeState(&text);
  printf("FOREGROUND_ENTRY case=%u app=%s cycle=%u age=%llu probe=%llu state=%s\n",m.caseIndex,m.app.c_str(),m.lightEntries+1,(unsigned long long)(m.ms-m.entered),progress,text.c_str());
  if(m.app=="settings"||m.app=="calculator"||m.app=="file_browser"){assert(progress==1);if(!m.lightEntries)m.savedProbe=text;else assert(text==m.savedProbe);}
  if(m.app=="file_browser")assert(m.filesMode==2&&m.filesDraft=="a"&&m.filesReadOnlyObserved);
   if(m.app=="stopwatch"){assert(progress);if(!m.lightEntries)m.firstElapsed=progress;}
  if(m.app=="wifi_settings")assert(!progress);
  ++m.lightEntries;m.ms+=7000;m.totalSlept+=7000;m.wokeAt=m.ms;m.lateOne=m.lateTwo=false;
  m.levels[3]=true;m.pmic[0x49]|=8;m.restoreExpected=true;*cause=RISC_LIGHT_SLEEP_WAKE_GPIO;return true;
 };
 h.wakeClear=[](uint8_t pin){if(qm.enabled||wm.enabled)assert(!m.terminal);assert((am.enabled||selectedForeground())&&pin==3);++m.wakeClears;if(m.caseIndex==17){m.terminal=true;m.terminalIo=m.io;return false;}return true;};
 h.timerArm=[](uint32_t ms){if(qm.enabled||wm.enabled)assert(!m.terminal);assert(am.enabled&&!m.terminal&&ms&&ms<=86400000);if(m.caseIndex==29&&am.ticketFaults){assert(m.transactions>am.faultTransaction&&m.transactionResumes>=am.faultTransaction);}alarmTimerArm(ms);am.timer=ms;++am.timerArms;return true;};h.timerClear=[](){if(qm.enabled||wm.enabled)assert(!m.terminal);assert(am.enabled&&!m.terminal);am.timer=0;++am.timerClears;return true;};
 h.maintenanceIdle=[](){if(qm.enabled||wm.enabled)assert(!m.terminal);if(selectedForeground()&&m.caseIndex==18&&m.ms-m.entered<badge_fixture_idle_fault_end_ms()){++m.maintenanceRefusals;return false;}return true;};
 h.deepWakeValid=[](uint8_t pin){if(qm.enabled||wm.enabled)assert(!m.terminal);return pin==3;};h.deepReady=[](){if(qm.enabled||wm.enabled)assert(!m.terminal);return true;};
 h.deepWakeArm=[](uint8_t pin,bool high,bool pull){if(qm.enabled||wm.enabled)assert(!m.terminal);assert(pin==3&&high&&!pull);++m.deepArms;if(m.target==8&&m.deepArms==1)m.levels[3]=true;return true;};
 h.deepWakeClear=[](uint8_t pin,bool pull){if(qm.enabled||wm.enabled)assert(!m.terminal);assert(pin==3&&!pull);m.levels[3]=false;return true;};
 h.deepHold=[](uint8_t pin,bool enabled){if(qm.enabled||wm.enabled)assert(!m.terminal);assert(pin==1&&m.pins[1]&&!m.levels[1]);m.hold=enabled;return true;};
 h.deepSleep=[](){if(qm.enabled||wm.enabled)assert(!m.terminal);if(am.enabled){alarmDeepReset();return;}if(m.caseIndex==20)assert(m.manualSleep&&m.ms-m.entered<15000);assert(!m.foreground);assert(m.target!=10);assert(!navigationTarget()&&!m.display&&m.hold&&m.frames&&m.visits==1&&m.visibleClock);assert(!(m.qmi[8]&3)&&(m.qmi[2]&1)&&!m.motionReads);assert(m.target!=7||(m.ms-m.entered>=badge_fixture_idle_fault_end_ms()&&m.heldIdleReads));assert(m.target!=8||m.deepArms==2);
   RiscCpu::NativeRealtime::enter([](){resetReason=ESP_RST_DEEPSLEEP;m.ms+=60000;RiscCpu::NativeRealtime::start();risc_realtime_snapshot_v1 out{};out.struct_size=sizeof(out);assert(RiscCpu::NativeRealtime::read(&out)==0&&out.validity==RISC_REALTIME_VALID&&out.epoch_seconds>=1792900060);printf("PASS actual Runtime Home deep entry target=%u PWR_GPIO3_only held_guard=passed panel_held=passed time_retained=passed hardware_unverified\n",m.target);std::exit(0);});};
 h.spiOpen=[](uint8_t,int16_t,int16_t,int16_t){if(qm.enabled||wm.enabled)assert(!m.terminal);assert(false);return false;};
 h.spiBegin=[](uint8_t,uint8_t,uint32_t,uint8_t,uint32_t){if(qm.enabled||wm.enabled)assert(!m.terminal);assert(false);return false;};h.spiTransfer=[](uint8_t,const uint8_t*,uint8_t*,size_t,uint32_t){if(qm.enabled||wm.enabled)assert(!m.terminal);assert(false);return false;};h.spiEnd=[](uint8_t,uint8_t,uint32_t){if(qm.enabled||wm.enabled)assert(!m.terminal);assert(false);return false;};h.spiClose=[](uint8_t){if(qm.enabled||wm.enabled)assert(!m.terminal);assert(false);return false;};
 h.realtimeRead=[](risc_realtime_snapshot_v1*out){if(qm.enabled||wm.enabled)assert(!m.terminal);
  if(am.enabled&&m.caseIndex==30&&am.saved&&m.manualSleep&&!m.display){assert(!m.terminal);m.terminal=true;m.terminalIo=m.io;return 99;}
  assert(!m.terminal);if(am.enabled&&m.nativeToken)++am.parkedRealtime;int rc=RiscCpu::NativeRealtime::read(out);
  if(m.caseIndex==0&&m.app=="default"&&m.visits==4&&rc==0&&out->validity==RISC_REALTIME_VALID){if(!homeValidReads)firstHomeEpoch=out->epoch_seconds;++homeValidReads;lastHomeEpoch=out->epoch_seconds;}return rc;};
 h.realtimeSeed=[](int64_t e,uint32_t n){if(qm.enabled||wm.enabled)assert(!m.terminal);++m.seedCalls;if(m.app=="settings")editedSeed=e;return RiscCpu::NativeRealtime::seed(e,n);};
 h.radioJoin=[](const char*ssid,const char*password){if(wm.enabled){assert(m.caseIndex==44&&wm.ssid==ssid&&wm.password==password);++wm.joins;}if(qm.enabled||wm.enabled)assert(!m.terminal);if(qm.enabled)++qm.radioJoins;
#ifdef BADGE_TIME_SERVER_SYNC
 if(badgeTimeSample.success){badgeTimeSample.network=true;return true;}
#endif
 return false;};h.radioState=[](uint8_t*s,int8_t*r){if(qm.enabled||wm.enabled)assert(!m.terminal);*s=0;*r=-127;
#ifdef BADGE_TIME_SERVER_SYNC
 if(badgeTimeSample.success&&badgeTimeSample.network){*s=2;*r=-40;}
#endif
 return true;};h.radioLeave=[](){
#ifdef BADGE_TIME_SERVER_SYNC
 if(badgeTimeSample.success){assert(!badgeTimeSample.active);badgeTimeSample.network=false;}
#endif
 if(wm.enabled)++wm.leaves;if(qm.enabled||wm.enabled)assert(!m.terminal);if(qm.enabled)++qm.radioLeaves;return true;};h.radioIdle=[](){if(wm.enabled)assert(!m.terminal);if(qm.enabled){assert(!m.terminal);++qm.radioIdleChecks;}return true;};h.radioAddresses=[](uint8_t*station,uint8_t*ap){(void)station;(void)ap;if(qm.enabled||wm.enabled)assert(!m.terminal);
#ifdef BADGE_TIME_SERVER_SYNC
 if(badgeTimeSample.success&&badgeTimeSample.network){memset(station,0,12);memset(ap,0,12);station[0]=192;station[2]=2;station[3]=1;return true;}
#endif
 return false;};h.radioScanStart=[](){if(wm.enabled){++wm.scanStarts;wm.scanBusy=true;}if(qm.enabled||wm.enabled)assert(!m.terminal);return true;};h.radioScanCancel=[](){if(wm.enabled)assert(!m.terminal);if(wm.enabled){++wm.scanCancels;if(wm.scanStarts==3&&!wm.scanCancelFailures){assert(wm.homeReturned&&wm.step==10);++wm.scanCancelFailures;wm.retainedFault=true;m.terminal=true;m.terminalIo=m.io;return false;}wm.scanBusy=false;}if(qm.enabled||wm.enabled)assert(!m.terminal);return true;};h.radioScanPoll=[](garden_radio_scan_result_v1*out){if(qm.enabled||wm.enabled)assert(!m.terminal);out->state=GARDEN_RADIO_SCAN_DONE;out->count=0;if(wm.enabled){++wm.scanPolls;out->count=3;memset(out->entries,0,sizeof(out->entries));strcpy(out->entries[0].ssid,"Unsupported enterprise");out->entries[0].auth=GARDEN_RADIO_AUTH_UNSUPPORTED;out->entries[1].auth=0;strcpy(out->entries[2].ssid,"Modeled WPA2 network");out->entries[2].auth=3;for(unsigned i=0;i<3;i++){out->entries[i].rssi=-40-int(i)*10;out->entries[i].channel=1+i;}}return true;};
#ifdef BADGE_TIME_SERVER_SYNC
 badgeTimeSampleHardware(h);
#endif
 static const RiscCpu::DisplayBackend display={displayAccepts,displayOpen,displayRow,displayBrightness,displayClose};h.displayBackend=&display;RiscCpu::Port port(h);cpu=&port;
 RiscStorage::AppDataFiles data({nullptr,[](void*){return 0u;},[](void*){return true;},malloc,free});std::string path=std::string(argv[1])+"/host-appdata";assert((!mkdir(path.c_str(),0700)||am.reset)&&data.configure(path.c_str()));
 RiscBoot::AppDataBackend appData{&data,[](void*c,uint32_t n,const char*p,uint32_t*s,uint64_t*r){return static_cast<RiscStorage::AppDataFiles*>(c)->stat(n,p,s,r);},[](void*c,uint32_t n,const char*p,uint64_t v,void*b,uint32_t z,uint32_t*s,uint64_t*r){return static_cast<RiscStorage::AppDataFiles*>(c)->read(n,p,v,b,z,s,r);},[](void*c,uint32_t n,const char*p,uint64_t v,const void*b,uint32_t z){return static_cast<RiscStorage::AppDataFiles*>(c)->replace(n,p,v,b,z);},[](void*c){return static_cast<RiscStorage::AppDataFiles*>(c)->exitSafe();}};
 RiscBoot::Port boot{owner,health,delay,log,bind,&keyValue};boot.monotonicMs=fixtureMonotonic;boot.confirmBoot=confirm;boot.appData=&appData;
 RiscTest::FileSources files({std::string(argv[1])});RiscBoot::Runtime runtime(boot);running=&runtime;assert(files.bind(runtime));if(!runtime.prepare(argv[1])){fprintf(stderr,"PREPARE %s\n",runtime.error());return 2;}assert(!m.io);bool ran=runtime.run();if(am.enabled){if(wm.enabled&&wm.retainedFault){assert(!ran&&runtime.retained()&&m.terminal&&m.io==m.terminalIo);wifiPass();}if(qm.enabled&&m.caseIndex==37){assert(!ran&&runtime.retained()&&qm.unknown&&m.terminal&&m.io==m.terminalIo);quickPass("unknown preference collect result retains; no later observed preference/service/KV/realtime/radio/display/GPIO/timer callback");}if(m.caseIndex==30){assert(!ran&&runtime.retained()&&m.terminal&&m.io==m.terminalIo&&!m.lightEntries&&!am.timerArms);puts("PASS actual Runtime alarm case=30 unknown realtime result retains invocation; no later I/O, timer, restore or cleanup");std::exit(0);}alarmObserve();fprintf(stderr,"Alarm qualification incomplete case=%u app=%s saved=%u modal=%u timer=%u GPIO=%u error=%s retained=%u\n",m.caseIndex,m.app.c_str(),am.saved,am.visible,am.timerWakes,am.gpioWakes,runtime.error(),unsigned(runtime.retained()));return 5;}if(m.foreground){
  if(m.caseIndex==17){assert(!ran&&runtime.retained()&&m.terminal&&m.io==m.terminalIo&&m.visits==3&&m.lightEntries==1&&!m.restores&&!m.display&&!m.pins[2]&&m.nativeToken&&m.transactions==1&&!m.transactionResumes&&!m.kvWrites&&!m.deepArms);puts("PASS actual Runtime foreground Light retained wake cleanup preserves invocation and dependencies with no later hardware I/O or restore");std::exit(0); /* Retained Runtime deliberately cannot undergo ordinary destructor cleanup. */}
  fprintf(stderr,"Foreground exited before qualification: case=%u app=%s visits=%u light=%u tx=%u restored=%u error=%s retained=%u\n",m.caseIndex,m.app.c_str(),m.visits,m.lightEntries,m.transactions,m.restores,runtime.error(),unsigned(runtime.retained()));return 4;
 }if(m.target==10){assert(!m.qmiWrites&&m.qmi[2]==2&&m.qmi[8]==3&&!runtime.retained());puts("PASS actual Runtime unsupported inherited sensor mode refuses idle qualification with zero sensor writes");return 0;}if(!ran){fprintf(stderr,"RUN error=%s retained=%u\n",runtime.error(),unsigned(runtime.retained()));return 3;}assert(m.visits==(m.caseIndex==19?3u:4u)&&!runtime.retained()&&port.quiescent()&&!m.display&&!m.i2c);if(m.caseIndex==19)puts("BUTTON_RETURN Springboard SHORT -> fresh Home passed");printf("PASS actual Runtime navigation target=%u Home -> Springboard -> selected app -> Home; fresh_visibility=passed native_dark_until_complete=passed physical device untested; peak_app_grants=%zu peak_graph_grants=%zu\n",m.target,m.peakApp,m.peakProviders);
 if(m.caseIndex==0){
  assert(m.seedCalls==2&&homeValidReads>=3&&lastHomeEpoch>firstHomeEpoch&&firstHomeEpoch>=editedSeed);
  risc_realtime_snapshot_v1 snapshot{};snapshot.struct_size=sizeof(snapshot);
  assert(RiscCpu::NativeRealtime::read(&snapshot)==0&&snapshot.validity==RISC_REALTIME_VALID);
  int64_t before=snapshot.epoch_seconds;
  for(unsigned cycle=0;cycle<1024;++cycle){
   try{RiscCpu::NativeRealtime::enter([](){throw 1;});assert(false);}catch(int){}
   resetReason=ESP_RST_DEEPSLEEP;m.ms+=60000;RiscCpu::NativeRealtime::start();
   assert(RiscCpu::NativeRealtime::read(&snapshot)==0&&snapshot.validity==RISC_REALTIME_VALID&&snapshot.epoch_seconds==before+60&&m.seedCalls==2);
   before=snapshot.epoch_seconds;
  }
  for(auto reset:{ESP_RST_SW,ESP_RST_POWERON}){
   resetReason=reset;RiscCpu::NativeRealtime::start();
   assert(RiscCpu::NativeRealtime::read(&snapshot)==0&&snapshot.validity==RISC_REALTIME_UNSET&&snapshot.epoch_seconds==0);
  }
  printf("PASS production Settings Year2026->2027 explicit_Save -> Home %u native reads advancing %lld seconds -> 1024 seed-once modeled deep resets; software/power reset honest UNSET; physical hardware unverified\n",homeValidReads,(long long)(lastHomeEpoch-firstHomeEpoch));
 }

#ifdef BADGE_PERFORMANCE_CAPTURE
 assert(RiscPerf::data.count>0);
 printf("PASS selected bounded RAM recorder active records=%u overwritten=%u; no automatic replay\n",RiscPerf::data.count,RiscPerf::data.lost);
#endif

}

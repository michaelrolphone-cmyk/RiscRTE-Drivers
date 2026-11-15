#pragma once
// Fixture-only observations and modeled external stimuli. No app state writes.
struct TimecardModel {
 std::string scenario,draft,status,ledger;int ui[20]{},controls[40][6]{},quick[24]{},qrects[10][4]{};unsigned char chars[32]{};
 int centers[19][2]{};unsigned count=0,first=0,visible=0;int motionX=0,motionY=0;
 unsigned expectedCount=0,timecardIndex=0,settingsIndex=0;
 unsigned step=0,editorStep=0,launcherDrags=0,launcherFrames=0,dataStats=0,dataReads=0,dataWrites=0,keyboardPages=0;
 unsigned pendingFrame=0,preferenceSubmits=0,preferenceCalls=0,preferenceWrites=0,settingsReads=0,settingsFrames=0;
 unsigned timecardEntries=0,homeEntries=0,settingsEntries=0,initialPage=99;int settingsBrightness=-1;
 unsigned capture=0;bool done=false,entered=false,backing=false,gpioWake=false,alarmKeyDown=false,alarmSeen=false,alarmDismissed=false,alarmAcked=false,unknown=false,neutralAfterWake=false;
 bool tapActive=false,drag=false;int x=0,y=0,x2=0,y2=0;uint64_t actionAt=0,releaseAt=0,nextAt=0;
 std::vector<uint16_t> pendingPixels;std::vector<std::string> routes;
} tc;
void timecardObserve(bool);void timecardTouch(uint64_t,bool&,unsigned&,unsigned&);void timecardKeys(uint8_t);
void timecardEnter(const char*);void timecardExit(const char*);void timecardInitialize();bool timecardLight(uint32_t*);
void timecardKvRead(uint32_t,const char*,const void*,uint32_t);void timecardKvWrite(uint32_t,const char*,const void*,uint32_t);
void timecardDataRead(uint32_t,const char*,const void*,uint32_t);void timecardDataWrite(uint32_t,const char*,const void*,uint32_t);
void timecardFinish(bool,bool);

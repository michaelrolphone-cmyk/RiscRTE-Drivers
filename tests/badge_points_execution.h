#pragma once
// Small app-specific observer; launcher, Runtime, providers and input are shared.
#ifdef BADGE_POINTS_EXECUTION
struct PointsModel {
 int ui[24]{},controls[64][6]{};std::string notice;
 std::vector<uint8_t> initial,saved,observed;
 unsigned entries=0,writes=0,reads=0,step=0,originalMinute=0,draftMinute=0;
 unsigned pending=0,acked=0,serviceReads=0,contextReady=0;
 bool backing=false,reopened=false,alarmSeen=false,alarmDismissed=false;
} pm;
void pointsObserve(bool);void pointsTouch(uint64_t,bool&,unsigned&,unsigned&);void pointsKeys(uint8_t);
void pointsEnter(const char*);void pointsExit(const char*);void pointsInitialize();bool pointsLight(uint32_t*);
void pointsKvRead(uint32_t,const char*,const void*,uint32_t);void pointsKvWrite(uint32_t,const char*,const void*,uint32_t);
void pointsFinish(bool,bool);
#define fixtureAppObserve pointsObserve
#define fixtureAppTouch pointsTouch
#define fixtureAppKeys pointsKeys
#define fixtureAppEnter pointsEnter
#define fixtureAppExit pointsExit
#define fixtureAppInitialize pointsInitialize
#define fixtureAppLight pointsLight
#define fixtureAppKvRead pointsKvRead
#define fixtureAppKvWrite pointsKvWrite
#define fixtureAppFinish pointsFinish
#else
#define fixtureAppObserve timecardObserve
#define fixtureAppTouch timecardTouch
#define fixtureAppKeys timecardKeys
#define fixtureAppEnter timecardEnter
#define fixtureAppExit timecardExit
#define fixtureAppInitialize timecardInitialize
#define fixtureAppLight timecardLight
#define fixtureAppKvRead timecardKvRead
#define fixtureAppKvWrite timecardKvWrite
#define fixtureAppFinish timecardFinish
#endif

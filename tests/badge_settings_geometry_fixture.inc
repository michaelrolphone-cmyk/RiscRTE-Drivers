/* Test-only read-only bridge to the selected app's actual layout. */
static void (*badgeSettingsGeometryProbe)(int*)=nullptr;
enum BadgeSettingsControl { BS_ROOT_TIME,BS_YEAR,BS_INCREMENT,BS_BACK,BS_SAVE,BS_CANCEL,BS_TIMER_DOWN,BS_TIMER_UP };
extern "C" void badge_settings_geometry_register(void(*probe)(int*)){
 badgeSettingsGeometryProbe=probe;
}
static inline void badgeSettingsGeometryQuery(int*v){
 assert(badgeSettingsGeometryProbe);badgeSettingsGeometryProbe(v);const char*expected=getenv("BADGE_SETTINGS_NATIVE_GEOMETRY_EXPECTED");
  assert(expected&&(!strcmp(expected,"0")||!strcmp(expected,"1")));assert(v[0]==atoi(expected));
  assert(v[1]>=240&&v[4]>0&&v[5]>0);
  if(v[0]&&v[1]>240)assert(v[1]>=300);
}
static inline int badgeSettingsFloor(int n,int d){return n>=0?n/d:-((-n+d-1)/d);}
static inline void badgeSettingsLogical(int lx,int ly,unsigned&x,unsigned&y){
 int v[44]={};badgeSettingsGeometryQuery(v);
 int px=v[2]+badgeSettingsFloor(lx*v[1],240),py=v[3]+badgeSettingsFloor(ly*v[1],240);
 assert(px>=0&&py>=0&&px<v[4]&&py<v[5]);x=(unsigned)px;y=(unsigned)py;
}
static inline void badgeSettingsTap(unsigned control,unsigned&x,unsigned&y){
 assert(control<8);int v[44]={};badgeSettingsGeometryQuery(v);
 if(control==BS_ROOT_TIME)assert(v[40]);if(control==BS_YEAR)assert(v[41]);if(control==BS_INCREMENT)assert(v[42]);
 const int*q=v+8+control*4;assert(q[0]>=0&&q[1]>=0&&q[2]>0&&q[3]>0&&q[0]+q[2]<=v[4]&&q[1]+q[3]<=v[5]);
 if(v[0]&&v[1]>=300)assert(q[3]>=44);
 x=(unsigned)(q[0]+q[2]/2);y=(unsigned)(q[1]+q[3]/2);
}

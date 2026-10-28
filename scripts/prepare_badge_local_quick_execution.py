#!/usr/bin/env python3
"""Prepare isolated test-only overlay; never edit selected Drivers/source trees."""
from pathlib import Path
import argparse,json,ast

def once(s,old,new):
 assert s.count(old)==1,(old,s.count(old));return s.replace(old,new,1)
def main():
 p=argparse.ArgumentParser();p.add_argument('--drivers',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();r=a.drivers.resolve();o=a.output.resolve();o.mkdir(exist_ok=False)
 for x in r.iterdir():
  if x.name in ('.git','scripts','tests'):continue
  (o/x.name).symlink_to(x,target_is_directory=x.is_dir())
 for name in ('scripts','tests'):
  (o/name).mkdir()
  for x in (r/name).iterdir():(o/name/x.name).symlink_to(x,target_is_directory=x.is_dir())
 def write(name,s):
  q=o/name;q.unlink();q.write_text(s)
 s=(r/'tests/badge_quick_execution.inc').read_text()
 # Existing exact Shared Home fixture is specialized to local Springboard:
 # retain its copied submit/unknown fences and actual alarm service workflow.
 s=once(s,'if(!qm.enabled)return;for(unsigned i=0;i<24;i++)','if(!qm.enabled||m.app!="springboard")return;for(unsigned i=0;i<24;i++)')
 s=s.replace('m.app=="default"&&m.visits==1','m.app=="springboard"&&m.visits==2')
 s=s.replace('if(completed&&m.app=="default"&&m.level','if(completed&&m.app=="springboard"&&m.level')
 s=once(s,'if(m.app=="default"&&am.homeReady&&!qm.start)qm.start=m.ms;','if(m.app=="springboard"&&!qm.start)qm.start=m.ms;')
 # Shared preferences are checked by actual Settings reads; final Home reads
 # remain checked, but Home's independent UI does not overwrite local state.
 s=once(s,'assert(qm.ui[10]&&!qm.ui[11]&&!qm.ui[12]&&!qm.ui[13]&&!qm.ui[14]);','assert(!qm.ui[10]&&!qm.ui[11]&&!qm.ui[12]&&!qm.ui[13]&&!qm.ui[14]);')
 s=once(s,'assert(qm.enabled&&(qm.opened||(m.caseIndex==39&&am.reset)));','assert(qm.enabled&&(qm.opened||(m.caseIndex==39&&am.reset)));assert(!qm.radioWrites&&!qm.radioJoins&&!qm.radioLeaves);')
 s=once(s,'if(m.caseIndex==34)assert(qm.saved&&qm.settingsRead&&m.visits==4&&quickStored("brightness")==80&&qm.ui[5]==80);','if(m.caseIndex==34)assert(qm.saved&&qm.settingsRead&&m.visits==4&&quickStored("brightness")==80&&qm.ui[5]==80&&m.level==204);')
 s=s.replace('if(m.caseIndex==38)assert(qm.pendingDelay', 'if(m.caseIndex==38)assert(m.restores==3&&m.transactions==m.transactionResumes&&qm.pendingDelay')
 s=s.replace('m.ms+=7000;++m.lightEntries;', 'm.restoreExpected=true;m.ms+=7000;++m.lightEntries;')
 s=once(s,'cancelled preview/Torch restore and three returning Home Light cycles','cancelled preview/Torch restore and three returning Springboard Light cycles')
 s=s.replace('if(m.caseIndex==36)assert(qm.stale&&qm.settingsRead&&qm.saved&&m.visits==4&&quickStored("brightness")==80);','if(m.caseIndex==36)assert(qm.settingsRead&&qm.saved&&m.visits==4&&quickStored("brightness")==80);')
 # This focused suite selects34/37/38/40 only; existing case36 remains
 # qualified separately by the unchanged Home suite.
 start=s.index(' if(m.app=="springboard"){',s.index('bool quickTouch'))
 end=s.index(' if(am.uiProbe)am.uiProbe();',start)
 s=s[:start]+''' if(m.app=="default"){
  if(!am.homeReady)return false;
  age=m.ms-am.appReadyAt;
  if((m.visits==1)||(m.caseIndex==40&&am.saved&&!am.acked)){down=age>=1600&&age<1950;x=age<1775?310:100;y=179;}
  return down;
 }
 if(m.app!="springboard")return false;
 if(m.caseIndex==40&&!am.saved){if(age>=1200&&age<1360){down=inheritedDown;return down;}if(age>=2300&&age<2460){down=true;x=y=233;}return down;}
 age=m.ms-m.entered;
 if((m.caseIndex==34||m.caseIndex==36)&&qm.saved&&age>=6500){if(age<6660){down=true;x=y=233;}return down;}
 if(m.lightEntries)return false;
'''+s[end:]
 # Springboard SHORT is Back, not Home's sleep action. Exercise the selected
 # foreground automatic-idle path; no fabricated press may navigate away.
 s=once(s,' bool sleep=(m.caseIndex==35', ' if(m.caseIndex==38)return; // Wait for actual configured foreground idle.\n bool sleep=(m.caseIndex==35')
 s=once(s,'assert(m.caseIndex==38&&!m.terminal&&!m.display&&!m.hold&&!am.timer);','assert(m.caseIndex==38&&m.app=="springboard"&&m.visits==2&&selectedForeground()&&!m.terminal&&!m.display&&!m.hold&&!am.timer);')

 # Actual frame identity observations; no write to production app state.
 s+='''
extern "C" void badge_local_quick_radio_contract(int controls){if(m.app=="springboard")assert(!controls);}
static uint64_t localAppRequested,localAppCompleted,localPending,localCompleted;
static unsigned localModalFrames;
extern "C" void badge_local_quick_identity(unsigned long long requested,unsigned long long complete,unsigned long long pending,unsigned long long displayed,int modal){
 if(m.app!="springboard")return;
 if(!modal){localAppRequested=requested;localAppCompleted=complete;}
 else {assert(requested==localAppRequested&&complete==localAppCompleted);++localModalFrames;}
 localPending=pending;localCompleted=displayed;
}
'''
 # Submit must match both physical completed pixels and private modal receipt.
 s=s.replace('printf("PASS actual Runtime quick actions case=%u ', 'printf("PASS actual Runtime quick actions ownership=temporary-local case=%u ')
 s=s.replace('extern "C" void badge_quick_submit_observe(){','extern "C" void badge_local_quick_submit_identity();\nextern "C" void badge_quick_submit_observe(){badge_local_quick_submit_identity();')
 s+='''extern "C" void badge_local_quick_submit_identity(){if(m.app=="springboard"){assert(localModalFrames&&localPending&&localPending==localCompleted);}}
'''
 write('tests/badge_quick_execution.inc',s)
 cpp=(r/'tests/badge_prototype_execution.cpp').read_text()
 # Native foreground transaction/restore identity uses the actual existing path.
 cpp=once(cpp,'if(am.enabled){alarmEnter(n);return;}','if(am.enabled){alarmEnter(n);if(!strcmp(n,"springboard")){m.foreground=true;m.foregroundApp=n;m.selectedSeedCalls=m.seedCalls;}else m.foreground=false;return;}')
 # Before configuring Countdown, preserve the ordinary catalog-derived
 # first tap and the existing second center activation. Foreground ownership
 # must not suppress the first tap and accidentally launch Settings at center.
 cpp=once(cpp,'m.app=="springboard"&&m.caseIndex!=19&&!(m.foreground&&m.foregroundApp=="springboard")','m.app=="springboard"&&m.caseIndex!=19&&(!(m.foreground&&m.foregroundApp=="springboard")||(qm.enabled&&m.caseIndex==40&&!am.saved))')
 cpp=once(cpp,'if(am.enabled){alarmEnter(n);','if(am.enabled){if(qm.enabled&&m.caseIndex==40){if(m.visits==3)assert(!strcmp(n,"countdown"));if(m.visits==4)assert(!strcmp(n,"default"));if(m.visits==5)assert(!strcmp(n,"springboard"));}alarmEnter(n);')
 # Home-specific fault timing now applies to the actual local sheet.
 cpp=cpp.replace('if(qm.enabled&&m.app=="default"){uint64_t qa=', 'if(qm.enabled&&m.app=="springboard"){uint64_t qa=')
 cpp=cpp.replace('m.caseIndex==38&&m.app=="default"&&y==0','m.caseIndex==38&&m.app=="springboard"&&y==0')
 # Guard the fixture snapshot before comparison; a wrong owner must fail an
 # assertion, never perform pointer arithmetic on an empty vector.
 cpp=once(cpp,'if(m.restoring)assert(std::equal(row,row+width,m.savedPixels.begin()+y*width));','if(m.restoring){assert(m.savedPixels.size()==m.pixels.size()&&m.savedPixels.size()==466u*466u&&y<466&&width==466);assert(std::equal(row,row+width,m.savedPixels.begin()+y*width));}')
 write('tests/badge_prototype_execution.cpp',cpp)
 runner=(r/'scripts/test_badge_prototype_execution.py').read_text()
 # Copy the already-qualified Home read-only layout probe, substituting the
 # selected local controller. Additional identity probe uses private counters.
 tree=ast.parse(runner);probe=next(n.value.value for n in ast.walk(tree) if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='quick_probe' for t in n.targets))
 probe=probe.replace('clock_quick','quick').replace('static void badge_quick_home_probe','static void badge_local_quick_probe')
 probe=probe.replace('const pqa_state*u=&quick.ui;', 'const pqa_state*u=&quick.ui;badge_local_quick_radio_contract(u->radio_controls);')
 probe='extern void badge_local_quick_radio_contract(int);\n'+probe
 probe+='''\nextern void badge_local_quick_identity(unsigned long long,unsigned long long,unsigned long long,unsigned long long,int);
static void badge_local_frame_probe(void){badge_local_quick_identity(frame_requested_id,frame_completed_id,quick_pending_receipt,presentation_completed_id,quick_modal||quick_restore_pending||quick_restoring);}
'''
 needle='     if extra:\n      original=f;f=out/(name+\'-alarm-observed-\'+f.name)'
 addition='''     if name=='springboard' and (f.name=='adapter.c' or f.name.endswith('-observed-adapter.c')):
      extra='''+repr(probe)+'''+extra.replace('badge_alarm_adapter_probe(void){','badge_alarm_adapter_probe(void){badge_local_quick_probe();badge_local_frame_probe();')
'''
 runner=once(runner,needle,addition+needle)
 # Observe copied API calls in local adapter using same exact parser seam as
 # Home. Only the instrumentation prefix differs; no capture-policy mutation.
 needle='      else:f.write_text(\'#include \'+json.dumps(str(original))+\'\\n\'+extra)'
 addition='''      elif name=='springboard' and (original.name=='adapter.c' or original.name.endswith('-observed-adapter.c')):
       atomic_header=next(directory/'PortablePreferenceAtomic.h' for directory in inc if (directory/'PortablePreferenceAtomic.h').is_file())
       text=atomic_header.read_text();collect='int rc=s->api->collect(s->api->context,s->token,&receipt);';assert text.count(collect)==1
       text=text.replace(collect,collect+'if(rc==RISC_ATOMIC_OK)rc=badge_quick_collect_result(rc);')
       text=re.sub(r'((?:int )?rc=(?:s->)?api->(state|get|submit|collect|cancel)\\([^;\\n]*\\);)',lambda m:'badge_quick_preference_call();'+('badge_quick_submit_observe();' if m.group(2)=='submit' else '')+m.group(1),text)
       observed=out/'local-quick-observed-atomic.h';observed.write_text('extern int badge_quick_collect_result(int);extern void badge_quick_preference_call(void);extern void badge_quick_submit_observe(void);\\n'+text)
       text=original.read_text()
       # The earlier alarm instrumentation may wrap the production adapter.
       adapter=S/'lib/PortableApps/src/adapter.c'
       if '#include "quick_adapter.inc"' not in text:
        text=text.replace('#include '+json.dumps(str(adapter)),adapter.read_text())
       assert text.count('#include "quick_adapter.inc"')==1
       qa=S/'lib/PortableApps/src/quick_adapter.inc';qat=qa.read_text()
       pending_source=qa.parent/'quick_pending_frame.inc';pending_text=pending_source.read_text()
       pending_clear=' quick.ui.save_pending=false;quick.ui.save_cancelled=false;'
       assert pending_text.count(pending_clear)==1,'Changed shared local pending cancellation boundary'
       pending_text=pending_text.replace(pending_clear,' badge_quick_pending_cancel_observe(quick.ui.save_cancelled);'+pending_clear,1)
       pending_overlay=out/'local-quick-observed-pending.inc';pending_overlay.write_text('extern void badge_quick_pending_cancel_observe(int);\\n'+pending_text)
       assert qat.count('#include "quick_pending_frame.inc"')==1
       qat=qat.replace('#include "quick_pending_frame.inc"','#include '+json.dumps(str(pending_overlay)))
       qao=out/'local-quick-observed-adapter.inc';qao.write_text(qat)
       text=text.replace('#include "quick_adapter.inc"','#include '+json.dumps(str(qao)))
       sources[str(qa)]=sha(qa);sources[str(pending_source)]=sha(pending_source)
       f.write_text('#include '+json.dumps(str(observed))+'\\n'+text+'\\n'+extra);inc=[*inc,original.parent,adapter.parent]
       sources[str(adapter)]=sha(adapter);sources[str(atomic_header)]=sha(atomic_header)
'''
 runner=once(runner,needle,addition+needle)
 # Preserve the existing alarm-restore overlay and production call ordering.
 # The alarm state is declared in alarm.inc, after quick_adapter.inc.
 alarm_write="     alarm_overlay=overlay_dir/'alarm.inc';alarm_overlay.write_text(alarm_text)"
 alarm_observer="""     if name=='springboard':
      boundary='    if(!quick_interrupt())return alarm_failure();'
      assert alarm_text.count(boundary)==1,'Changed actual local Quick alarm preemption boundary'
      alarm_text=alarm_text.replace(boundary,'    badge_quick_preempt_observe(pqa_visible(&quick.ui),pqa_capture(&quick.ui),quick.ui.pending,alarms.status.occurrence.generation);\\n'+boundary)
      alarm_text='extern void badge_quick_preempt_observe(int,int,unsigned,unsigned);\\n'+alarm_text
"""
 runner=once(runner,alarm_write,alarm_observer+alarm_write)
 runner=once(runner,"{'passed':True,","{'qualification':'temporary-springboard-local-quick','temporary_local_ownership':True,'resident_architecture_complete':False,'passed':True,")
 trace_start=runner.index('   if interaction_trace and index in (6,34,38):')
 trace_end=runner.index("   shutil.rmtree(host/'host-appdata')",trace_start)
 runner=runner[:trace_start]+'''   assert 'APP_ENTER default 1' in r.stdout and 'APP_ENTER springboard 2' in r.stdout,'Missing installed Home-to-Springboard route'
   assert 'ownership=temporary-local' in r.stdout,'Wrong fixture qualification marker'
'''+runner[trace_end:] # Home-only trace phases remain in the separate unchanged Home suite.

 runner=once(runner,'selected_cases=a.cases if a.cases else range(total_cases)','selected_cases=a.cases if a.cases else [34,37,38,40]')
 runner=once(runner,"assert all(0<=i<total_cases for i in selected_cases)","assert all(i in (34,37,38,40) for i in selected_cases)\n  assert records['springboard']['native_copied_quick']['selected']\n  assert records['springboard']['grant_bindings']['storage.preferences']==[1]")
 profile_guard="  assert records['springboard']['grant_bindings']['storage.preferences']==[1]"
 profile_checks='''
  local=records['springboard'];policy=local['native_copied_quick']
  assert policy['ownership']=='temporary-app-local' and policy['resident_architecture_complete'] is False
  assert policy['wifi_action']=='setup-launch' and policy['wifi_destination']=='wifi_settings.elf' and policy['radio_control_grants'] is False
  assert policy['completed_pending_frame_before_submit'] is True and policy['sliced_frame_identity'] is True
  assert local['quick_radios'] is False and '-DPORTABLE_QUICK_RADIOS' not in local['build_defines']
  manifest=json.loads((store/'springboard.json').read_text());caps={x['capability'] for x in manifest['requires']}
  assert {'storage.preferences','storage.key-value','runtime.realtime','board.battery','alarm.service','power.sleep'}<=caps
  assert not caps.intersection({'net.wifi','bluetooth.hci','audio.output'})
'''
 runner=once(runner,profile_guard,profile_guard+profile_checks)
 write('scripts/test_badge_prototype_execution.py',runner)
 print(o/'scripts/test_badge_prototype_execution.py')
if __name__=='__main__':main()

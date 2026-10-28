#!/usr/bin/env python3
"""Build a pinned richer Badge store. Offline only; full admission is separate."""
import argparse,json,os,subprocess,shutil,sys,hashlib
from pathlib import Path
from build_badge_home import sha,write,run,ROOT,BOARD,home_manifest
from cst9217_sdk import stage_sdk
from badge_app_inventory import collect as collect_app_inventory
from alarm_geometry_profile import qualify_alarm_geometry_recipe
from wifi_geometry_profile import qualify_wifi_geometry_recipe
from private_storage_profile import apply_private_bindings,private_bindings,qualify_private_grants
from temporary_quick_profile import qualify as qualify_temporary_quick
from springboard_gesture_profile import qualify as qualify_gesture
from home_raster_reuse_profile import qualify as qualify_home_reuse
import raster_traffic_profile
import time_sync_profile
P=BOARD/'prototype'
def git(root,*args):return subprocess.check_output(['git','-C',str(root),*args],text=True).strip()
# Canonical performance SDK identity, independent of selected native implementation.
PERFORMANCE_SDK_COMMIT='273bdbccf65647a803ab0586a418f8283732baec'
PERFORMANCE_SDK_OBJECTS={'sdk':'30d9a0464b6c03f0ae265317530720b58cc9d698','LICENSE':'b8c074800342d88eb4892d9ef79a65485d1bfcf9'}
def qualify_performance_sdk(trace,runtime,field='runtime_commit'):
 canonical=trace.get(field);identity=trace.get('runtime_sdk_identity')
 # Historical exact-checkout receipts remain valid without equivalence metadata.
 if canonical==runtime and identity is None:return
 if not isinstance(identity,dict) or canonical!=PERFORMANCE_SDK_COMMIT or identity.get('canonical_commit')!=canonical or identity.get('checkout_commit')!=runtime or identity.get('checkout_clean') is not True or identity.get('canonical_ancestor') is not True or not (identity.get('exact_git_objects')==PERFORMANCE_SDK_OBJECTS or (identity.get('canonical_git_objects')==PERFORMANCE_SDK_OBJECTS and isinstance(identity.get('unchanged_canonical_sdk_files'),int) and identity['unchanged_canonical_sdk_files']>0 and isinstance(identity.get('additive_sdk_files'),dict) and identity['additive_sdk_files'] and isinstance(identity.get('checkout_sdk_object'),str))):
  raise ValueError('Native RAM trace canonical SDK/current checkout mismatch')
def qualify_native_performance(record,enabled,runtime):
 trace=record.get('native_performance_trace',{})
 defined='-DPORTABLE_PERFORMANCE_TRACE' in record.get('build_defines',[])
 if trace.get('enabled',False) is not enabled or defined is not enabled:raise ValueError('Native RAM trace selection mismatch')
 if enabled:qualify_performance_sdk(trace,runtime)
 if enabled and (trace.get('capture_command_ascii_hex')!='706572660a' or trace.get('unsolicited_trace_output') is not False or trace.get('profile_changed') is not False):raise ValueError('Native RAM trace SDK/transport/profile mismatch')
def qualify_alarm_ready_drain(record,enabled):
 """Require actual shared builder selection, not only a product intent flag."""
 policy=record.get('foreground_idle',{}).get('alarm_ready_drain',{})
 flags=record.get('build_defines',[])
 selected=[x for x in flags if x.startswith('-DPORTABLE_ALARM_READY_')]
 expected=['-DPORTABLE_ALARM_READY_DRAIN','-DPORTABLE_ALARM_READY_BUDGET_MS=8u'] if enabled else []
 if sorted(selected)!=sorted(expected) or bool(policy.get('enabled'))!=enabled:
  raise ValueError('Alarm ready-drain defines/receipt mismatch')
 if enabled and (policy.get('budget_ms')!=8 or '-DALARM_SERVICE_TAGGED_V2' not in flags):
  raise ValueError('Alarm ready-drain tagged budget mismatch')
def qualify_sleep_defaults(home,settings,idle_seconds):
 """Admission checks actual shared builder receipts, not an assumed product mode."""
 for name,record in [('Home',home),('Settings',settings)]:
  policy=record.get('sleep_defaults',{})
  if policy.get('mode')!='hybrid' or policy.get('idle_seconds')!=idle_seconds or policy.get('persisted_preferences_take_precedence') is not True:
   raise ValueError(name+' must preserve staged HYBRID fallback and saved preferences')
def qualify_sleep_timer_controls(settings):
 """Selected Badge exposes both existing timers; no sleep backend/grants added."""
 policy=settings.get('sleep_defaults',{});preferences=settings.get('preferences',{});defines=settings.get('build_defines',[])
 if policy.get('idle_timer_editable') is not True or policy.get('deep_timer_editable') is not True or policy.get('sleep_backend') is not False:
  raise ValueError('Settings must expose both existing timers without owning sleep')
 if preferences.get('instance')!=1 or preferences.get('idle_timer_key')!='sleep_idle' or preferences.get('deep_timer_key')!='sleep_deep':
  raise ValueError('Settings timer preference contract mismatch')
 if '-DPORTABLE_SETTINGS_BOTH_TIMERS' not in defines or '-DPORTABLE_SETTINGS_IDLE_TIMER' in defines:
  raise ValueError('Settings both-timer source selection mismatch')
def qualify_interaction_selections(home,settings,springboard,trace,native_settings,sliced,round_settings=False):
 if home.get('interaction_trace',False) is not trace:raise ValueError('Home interaction trace selection mismatch')
 if (('-DWATCH_INTERACTION_TRACE' in home['artifacts']['default']['defines']) is not trace):raise ValueError('Home trace define mismatch')
 if (('-DPORTABLE_SETTINGS_NATIVE_GEOMETRY' in settings.get('build_defines',[])) is not native_settings):raise ValueError('Settings native geometry define mismatch')
 if native_settings and settings.get('native_settings_geometry',{}).get('native_font_rasters') is not True:raise ValueError('Settings native font geometry receipt missing')
 if (('-DPORTABLE_SETTINGS_ROUND_GEOMETRY' in settings.get('build_defines',[])) is not round_settings):raise ValueError('Settings round geometry define mismatch')
 if round_settings and (not native_settings or settings.get('round_settings_geometry',{}).get('flag_value')!=64):raise ValueError('Settings explicit round capability receipt missing')
 if springboard.get('sliced_native_frames',False) is not sliced:raise ValueError('Springboard sliced selection mismatch')
 if sliced and (type(springboard.get('raster_work_budget_ms')) is not int or springboard['raster_work_budget_ms']!=8):raise ValueError('Springboard native work budget mismatch')
 required=['-DPORTABLE_NOVA_SLICED_FRAMES','-DPORTABLE_FRAME_RECEIPTS','-DPORTABLE_RASTER_WORK_BUDGET_MS=8','-DPORTABLE_RASTER_SNAPSHOT','-DPORTABLE_RASTER_COOPERATIVE_INPUT','-DPORTABLE_INPUT_SAMPLE_INTERVAL_MS=2u','-DPORTABLE_RGB565_SPAN_FILL']
 if any((x in springboard.get('build_defines',[])) is not sliced for x in required):raise ValueError('Springboard sliced rendering defines mismatch')
def main():
 p=argparse.ArgumentParser(description=__doc__)
 for n in ['runtime','runtime-baseline','watch','watch-bringup','controllers','system','system-baseline','utilities','utilities-baseline','output']:p.add_argument('--'+n,type=Path,required=True)
 p.add_argument('--home-system',type=Path,help='Exact System source pinned by the selected Home native profile; distinct from app producer metadata source')
 p.add_argument('--productivity',type=Path,help='Exact Productivity source for the explicit Timecard increment')
 p.add_argument('--include-timecard',action='store_true',help='Select current native Timecard with private AppData; requires native alarms/foreground idle')
 p.add_argument('--native-glyph-coverage-cache',action='store_true',help='Explicit optional exact Home glyph coverage reuse')
 p.add_argument('--native-stroke-coverage-cache',action='store_true',help='Explicit optional exact Home stroke coverage reuse')
 p.add_argument('--picker-compositor-plan',action='store_true',help='Optional generic exact picker row/column composition plan')
 p.add_argument('--native-damage-replay',action='store_true',help='Optional exact completed-face damage replay with full-render fallback')
 p.add_argument('--interaction-trace',action='store_true',help='Sparse custody-safe Home boundary diagnostics')
 p.add_argument('--springboard-gesture-diagnostics',action='store_true',help='Explicit bounded Springboard-only terminal gesture diagnostics; requires native trace')
 p.add_argument('--native-performance-trace',action='store_true',help='Explicit current-SDK bounded RAM phases for Home and native Settings/Springboard')
 p.add_argument('--native-settings-ui',action='store_true',help='Capability-safe native Settings geometry')
 raster_traffic_profile.add_arguments(p)
 p.add_argument('--round-settings-ui',action='store_true',help='Opt in advertised circular panel chords for native Settings')
 p.add_argument('--sliced-native-frames',action='store_true',help='Cooperative native Springboard rendering and completed-frame hit maps')
 p.add_argument('--alarm-ready-drain',action='store_true',help='Opt in supported shared foreground clients to budgeted immediately-ready alarm reconciliation')
 p.add_argument('--time-server-sync',action='store_true',help='Shared Wi-Fi time sampling and alarm-safe UTC commit')
 p.add_argument('--time-server',default='pool.ntp.org',help='SNTP server for explicit selected Wi-Fi synchronization')
 p.add_argument('--native-wifi-geometry',action='store_true',help='Native WiFi safe-content presentation in the qualified QuickActions product cohort')
 p.add_argument('--quick-actions',action='store_true',help='Explicit Home copied preferences and round-safe QuickActions; requires native-alarms')
 p.add_argument('--springboard-quick-actions',action='store_true',help='Temporary app-local native Springboard Quick; not resident-owner architecture completion')
 p.add_argument('--native-alarm-geometry',action='store_true',help='Explicit native safe-content Alarm/Countdown presentation; requires native-alarms')
 p.add_argument('--native-alarms',action='store_true',help='Explicit UTC visual schedules and deadline wake; no recurring factory schedules')
 p.add_argument('--native-daily',action='store_true',help='Select native safe-content daily apps; requires foreground-idle')
 p.add_argument('--include-files',action='store_true',help='Include restricted installed-file browser; requires returning foreground idle')
 p.add_argument('--foreground-idle',action='store_true',help='Opt in shared RAM-retaining foreground Light idle; Home policy unchanged');p.add_argument('--cc',required=True);p.add_argument('--idle-seconds',type=int,default=30,help='Configurable Home inactivity default; saved preferences override');a=p.parse_args();out=a.output.resolve()
 raster_traffic_profile.validate(a,p)
 if a.time_server_sync and not (a.native_alarms and a.foreground_idle):p.error('Time sync requires existing alarm-safe clock edit and foreground lifecycle')
 if a.round_settings_ui and not a.native_settings_ui:p.error('round-settings-ui requires native-settings-ui')
 if a.include_timecard and (not a.native_alarms or a.productivity is None):p.error('include-timecard requires native-alarms and exact Productivity source')
 if a.productivity is not None and not a.include_timecard:p.error('productivity requires explicit include-timecard selection')
 if a.include_timecard and not a.quick_actions:p.error('include-timecard requires the copied-preference QuickActions product cohort')
 if a.native_wifi_geometry and not a.quick_actions:p.error('native-wifi-geometry requires the quick-actions product cohort')
 if a.quick_actions and not a.native_alarms:p.error('quick-actions requires native-alarms')
 if a.springboard_gesture_diagnostics and not a.native_performance_trace:p.error('springboard-gesture-diagnostics requires native-performance-trace')
 if a.alarm_ready_drain and not (a.native_alarms and a.foreground_idle):p.error('alarm-ready-drain requires native-alarms and foreground-idle')
 if a.springboard_quick_actions and not (a.quick_actions and a.sliced_native_frames):p.error('springboard-quick-actions requires Home copied preferences and sliced native frames')
 if a.native_alarm_geometry and not a.native_alarms:p.error('native-alarm-geometry requires native-alarms')
 if a.native_alarms and not a.home_system:p.error('native-alarms requires the explicit pinned Home system source role')
 if a.native_alarms and not (a.native_daily and a.include_files and a.foreground_idle):p.error('native-alarms requires native-daily, include-files and foreground-idle')
 if a.native_daily and not a.foreground_idle:p.error('native-daily requires foreground-idle')
 if a.include_files and not a.foreground_idle:p.error('include-files requires foreground-idle')
 if not 5<=a.idle_seconds<=3600:p.error('idle-seconds must be 5..3600')
 if out.exists() and any(out.iterdir()):p.error('Use an empty output')
 roots={n:getattr(a,n.replace('-','_')).resolve() for n in ['runtime','runtime-baseline','watch','watch-bringup','controllers','system','system-baseline','utilities','utilities-baseline']}
 if a.include_timecard:roots['productivity']=a.productivity.resolve()
 if a.native_alarms:roots['home-system']=a.home_system.resolve()
 lock=json.loads((P/'sources.lock.json').read_text());base=json.loads((BOARD/'home/sources.lock.json').read_text())
 for n,r in roots.items():
  k=n.replace('-baseline','').replace('watch-bringup','watch_bringup').replace('-','_');rev=(base if n.endswith('-baseline') else lock)[k]['commit']
  if git(r,'rev-parse','HEAD')!=rev or git(r,'status','--porcelain'):raise ValueError('Clean exact input required: '+n+' '+rev)
 out.mkdir(parents=True,exist_ok=True)
 env={**os.environ,'NATIVE_APP_CC':a.cc,'WATCH_APP_CC':a.cc}
 def cmd(*xs):subprocess.run(list(map(str,xs)),check=True,env=env)
 foreground_args=['--foreground-idle-helper-dir',ROOT/'components/portable-light-sleep','--foreground-idle-runtime-dir',roots['runtime'],'--foreground-idle-drivers-sdk-dir',ROOT/'sdk/driver','--foreground-idle-fallback-seconds',a.idle_seconds] if a.foreground_idle else []
 alarm_foreground_args=[*foreground_args,'--foreground-alarm-sleep','--clock-edit-authority'] if a.native_alarms else []
 if a.native_alarms:foreground_args += ['--foreground-alarm-sleep','--clock-edit-authority','--alarm-client','--tagged-alarm-utilities',roots['utilities']]
 if a.alarm_ready_drain:
  foreground_args += ['--alarm-ready-drain'];alarm_foreground_args += ['--alarm-ready-drain']
 catalog=json.loads((P/'catalog.json').read_text())
 if a.include_files:
  fm=json.loads((roots['system']/'Apps/native/file_browser.json').read_text())
  catalog['apps'].append({key:fm[key] for key in ['display_name','file_name','icon']})
 if a.native_alarms:
  for name in ['alarms','countdown']:
   fm=json.loads((roots['utilities']/'Apps'/(name+'.json')).read_text());catalog['apps'].append({key:fm[key] for key in ['display_name','file_name','icon']})
 if a.include_timecard:
  fm=json.loads((roots['productivity']/'Apps/native/timecard_current.json').read_text());catalog['apps'].append({key:fm[key] for key in ['display_name','file_name','icon']})
 write(out/'catalog.json',catalog)
 # Home is deliberately kept on its already qualified shared dependency cohort.
 cmd(sys.executable,ROOT/'scripts/build_badge_home.py','--runtime',roots['runtime-baseline'],'--watch',roots['watch'],'--controllers',roots['controllers'],'--system',roots['system-baseline'],'--utilities',roots['utilities-baseline'],'--cc',a.cc,'--output',out/'baseline')
 store=out/'store';shutil.copytree(out/'baseline/store',store)
 # Same maintained shared Home, with explicit generic product defaults, not a board app fork.
 cmd(sys.executable,roots['watch-bringup']/'scripts/build.py','--system',roots['home-system'] if a.native_alarms else roots['system-baseline'],'--utilities',roots['utilities'] if a.native_alarms else roots['utilities-baseline'],'--cc',a.cc,'--role','default','--time-authority','native','--unset-time-local','2026-01-01T00:00:00','--default-sleep-mode','hybrid','--default-idle-seconds',a.idle_seconds,'--output',out/'shared-home',*(['--native-alarm-v2','--clock-edit-authority'] if a.native_alarms else []),*(['--native-quick-actions'] if a.quick_actions else []),*(['--interaction-trace'] if a.interaction_trace else []),*(['--native-performance-runtime',roots['runtime']] if a.native_performance_trace else []),*(['--native-glyph-coverage-cache'] if a.native_glyph_coverage_cache else []),*(['--native-stroke-coverage-cache'] if a.native_stroke_coverage_cache else []),*(['--picker-compositor-plan'] if a.picker_compositor_plan else []),*(['--native-damage-replay'] if a.native_damage_replay else []))
 shutil.copy2(out/'shared-home/default.elf',store/'default.elf')
 home=home_manifest(json.loads((out/'shared-home/default.json').read_text()),json.loads((store/'boot.json').read_text())['app_capabilities'][0])
 if a.native_alarms:
  original=json.loads((out/'shared-home/default.json').read_text());alarm=[d for d in original['requires'] if d['capability']=='alarm.service']
  if alarm != [{'capability':'alarm.service','api':2}]:raise ValueError('Mandatory tagged alarm Home contract missing')
  home['optional']=[d for d in home['optional'] if d['capability']!='alarm.service'];home['requires'] += alarm
 if a.quick_actions:
  original=json.loads((out/'shared-home/default.json').read_text())
  pref=[d for d in original.get('requires',[])+original.get('optional',[]) if d['capability']=='storage.preferences']
  if pref != [{'capability':'storage.preferences','api':1}]:raise ValueError('Copied preference Home contract missing')
  home['optional']=[d for d in home['optional'] if d['capability']!='storage.preferences']
  home['requires']=[d for d in home['requires'] if d['capability']!='storage.preferences']+pref
 home_record=json.loads((out/'shared-home/build.json').read_text())
 home_reuse=qualify_home_reuse(home_record,a.native_glyph_coverage_cache,a.native_stroke_coverage_cache,roots['watch-bringup'],a.picker_compositor_plan,a.native_damage_replay)
 if home_record.get('native_quick_actions',False) is not a.quick_actions:raise ValueError('Home QuickActions selection mismatch')
 home_perf=home_record.get('native_performance_trace')
 if bool(home_perf) is not a.native_performance_trace:raise ValueError('Home RAM trace selection mismatch')
 if a.native_performance_trace:qualify_performance_sdk(home_perf,lock['runtime']['commit'],'runtime')
 write(store/'default.json',home)
 cmd(sys.executable,roots['watch']/'scripts/build.py','--system',roots['system-baseline'],'--utilities',roots['utilities-baseline'],'--cc',a.cc,'--role','clock','--output',out/'clock')
 cmd(sys.executable,roots['watch-bringup']/'scripts/build_bringup.py','--system',roots['system-baseline'],'--utilities',roots['utilities-baseline'],'--cc',a.cc,'--output',out/'bringup')
 cmd(sys.executable,roots['system']/'scripts/build_portable_settings.py','--settings-profile','native-time-touch','--sleep-settings','--sleep-timer-settings','--default-sleep-mode','hybrid','--default-idle-seconds',a.idle_seconds,'--native-time-runtime-repo',roots['runtime'] if a.native_performance_trace else roots['runtime-baseline'],'--display-rotation','0','--safe-viewport','--full-frames','--navigation','--return-app','default.elf','--output-dir',out/'settings',*foreground_args,*(['--native-settings-ui'] if a.native_settings_ui else []),*(['--round-settings-ui'] if a.round_settings_ui else []),*(['--sliced-native-frames'] if a.native_settings_ui and a.sliced_native_frames else []),*(['--native-performance-trace'] if a.native_performance_trace else []),*raster_traffic_profile.arguments(a,'settings'))
 cmd(sys.executable,roots['system']/'scripts/build_portable_springboard.py','--watch-native-ui','--time-profile','watch-native-time','--native-time-runtime-repo',roots['runtime'],'--display-rotation','0','--full-frames','--navigation','--return-app','default.elf','--catalog',out/'catalog.json','--output-dir',out/'springboard',*foreground_args,*(['--sliced-native-frames'] if a.sliced_native_frames else []),*(['--quick-actions','--native-copied-quick'] if a.springboard_quick_actions else []),*(['--native-performance-trace'] if a.native_performance_trace else []),*(['--springboard-gesture-diagnostics'] if a.springboard_gesture_diagnostics else []),*raster_traffic_profile.arguments(a,'springboard'))
 qualify_interaction_selections(home_record,json.loads((out/'settings/settings-build-record.json').read_text()),json.loads((out/'springboard/springboard-build-record.json').read_text()),a.interaction_trace,a.native_settings_ui,a.sliced_native_frames,a.round_settings_ui)
 for app in ('settings','springboard'):
  selected=json.loads((out/app/(app+'-build-record.json')).read_text())
  qualify_native_performance(selected,a.native_performance_trace,lock['runtime']['commit'])
  qualify_gesture(selected,a.springboard_gesture_diagnostics and app=='springboard',app)
 cmd(sys.executable,roots['system']/'scripts/build_portable_wifi.py','--safe-viewport','--display-rotation','0','--full-frames','--navigation','--return-app','default.elf','--wifi-instance','15','--output-dir',out/'wifi',*foreground_args,*(['--watch-native-ui'] if a.native_wifi_geometry else []),*(['--time-server-sync','--time-server',a.time_server] if a.time_server_sync else []))
 qualify_wifi_geometry_recipe(json.loads((out/'wifi/wifi_settings-build-record.json').read_text()),a.native_wifi_geometry)
 time_sync_profile.qualify(json.loads((out/'wifi/wifi_settings-build-record.json').read_text()),json.loads((out/'wifi/wifi_settings.json').read_text()),a.time_server_sync,a.time_server,lock['runtime']['commit'],(roots['runtime']/'sdk/driver/RiscTimeSampleV1.h').read_bytes() if a.time_server_sync else b'')
 cmd(sys.executable,roots['utilities']/'scripts/build_portable_apps.py','--system-apps',roots['system'],'--system-apps-revision',lock['system']['commit'],'--safe-viewport','--include-battery','--navigation','--native-stopwatch-runtime',roots['runtime'],'--output',out/'daily',*foreground_args,*(['--native-safe-content'] if a.native_daily else []))
 if a.include_files:
  cmd(sys.executable,roots['system']/'scripts/build_portable_file_browser.py','--watch-native-ui','--time-profile','watch-native-time','--native-time-runtime-repo',roots['runtime'],'--display-rotation','0','--navigation','--read-copy-only','--rgb-only','--home-app','default.elf','--return-app','default.elf','--output-dir',out/'files',*foreground_args)
 if a.native_alarms:
  cmd(sys.executable,roots['utilities']/'scripts/build_native_schedules_alarm.py','--runtime',roots['runtime'],'--system-apps',roots['system'],'--system-revision',lock['system']['commit'],'--cc',a.cc,'--output',out/'alarm-service','--clock-edit-authority')
  cmd(sys.executable,roots['utilities']/'scripts/build_badge_native_alarm_apps.py','--runtime',roots['runtime'],'--system-apps',roots['system'],'--system-revision',lock['system']['commit'],'--cc',a.cc,'--output',out/'alarm-apps',*alarm_foreground_args,*(['--native-alarm-presentation'] if a.native_alarm_geometry else []))
  presentation=json.loads((out/'alarm-apps/build-evidence.json').read_text()).get('native_alarm_geometry',False)
  if type(presentation) is not bool or presentation!=a.native_alarm_geometry:raise ValueError('Alarm aggregate geometry selection mismatch')
  for name in ('alarms','countdown'):
   qualify_alarm_geometry_recipe(json.loads((out/'alarm-apps'/(name+'-build-record.json')).read_text()),a.native_alarm_geometry)
 if a.include_timecard:
  cmd(sys.executable,roots['productivity']/'scripts/build_timecard_current.py','--system-apps',roots['system'],'--runtime',roots['runtime'],'--utilities',roots['utilities'],'--foreground-idle-helper-dir',ROOT/'components/portable-light-sleep','--foreground-idle-drivers-sdk-dir',ROOT/'sdk/driver','--foreground-idle-fallback-seconds',a.idle_seconds,'--output-dir',out/'timecard')
  timecard_record=json.loads((out/'timecard/timecard-build-record.json').read_text())
  timecard_manifest=json.loads((out/'timecard/timecard.json').read_text())
  if private_bindings(timecard_manifest,timecard_record)!={'storage.key-value':[1],'storage.app-data':[1]}:
   raise ValueError('Timecard requires exact preference and private AppData namespaces, never diagnostic fallback')
  if timecard_record['grant_bindings'].get('storage.preferences')!=[1] or not all('-D'+flag in timecard_record['build_defines'] for flag in ['PORTABLE_QUICK_ACTIONS','PORTABLE_PREFERENCE_STORE','PORTABLE_QUICK_ATOMIC_PREFERENCES']):
   raise ValueError('Timecard must preserve QuickActions using the selected copied preference broker')
 sys.path.insert(0,str(roots['watch']/'scripts'))
 from compact_current_elf import compact
 from normalize_xtensa_relocations import normalize
 from verify_psram_elf import verify
 applications={}
 for folder,names in [('clock',['clock']),('bringup',['bringup']),('settings',['settings']),('springboard',['springboard']),('daily',['calculator','stopwatch','battery']),('wifi',['wifi_settings']),*([('files',['file_browser'])] if a.include_files else []),*([('alarm-apps',['alarms','countdown'])] if a.native_alarms else []),*([('timecard',['timecard'])] if a.include_timecard else [])]:
  for name in names:
   source=out/folder/(name+'.elf');debug=out/folder/'debug'/(name+'.elf')
   if not debug.exists():normalize(source);compact(source,a.cc,debug_path=debug)
   safety=verify(debug,a.cc)
   for suffix in ['.elf','.json']:shutil.copy2(out/folder/(name+suffix),store/(name+suffix))
   applications[name]={'sha256':sha(store/(name+'.elf')),'original_elf':str(debug.relative_to(out)),'psram_safety':safety}
 sdk=stage_sdk(ROOT,roots['runtime'],out/'provider-sdk')
 for directory in [roots['watch']/'sdk/app',roots['watch']/'include',roots['controllers']/'include']:
  for h in directory.glob('*.h'):
   if not (sdk/h.name).exists():shutil.copy2(h,sdk/h.name)
 common=['-std=c11','-Os','-fPIC','-mtext-section-literals','-mlongcalls','-fvisibility=hidden','-ffreestanding','-fno-builtin','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-ffunction-sections','-fdata-sections','-nostdlib','-nostartfiles','-shared','-Wl,--gc-sections','-Wl,--no-relax','-Wl,--hash-style=sysv','-I'+str(ROOT/'components/co5300/include'),'-I'+str(sdk),'-I'+str(roots['controllers']/'include')]
 providers={}
 for name,directory in [('battery','axp2101_observer'),('key','axp2101_power_key'),('navigation','power_key_navigation'),('display-power','legacy_display_power'),('wifi','@wifi'),('sleep','gpio_key_sleep'),('wake-key','wake_key_filter'),('sensor-idle','qmi8658_idle'),*([('time-sample','time_sample')] if a.time_server_sync else [])]:
  target=store/name;target.mkdir();source=(roots['controllers']/'drivers/twatch_wifi/driver.c') if directory=='@wifi' else ROOT/'Drivers'/directory/'driver.c';elf=target/'driver.elf';mapping=out/(name+'.map');mapping.write_text('{ global: t5_driver_get; local: *; };\n')
  cmd(a.cc,*common,'-Wl,--version-script='+str(mapping),source,'-lgcc','-o',elf);normalize(elf);debug=out/'debug'/(name+'.elf');compact(elf,a.cc,debug_path=debug);safety=verify(debug,a.cc)
  shutil.copy2(source.with_name('manifest.json'),target/'manifest.json');providers[name]={'sha256':sha(elf),'psram_safety':safety}
 if a.native_alarms:
  target=store/'alarm-service';target.mkdir();elf=target/'driver.elf';shutil.copy2(out/'alarm-service/driver.elf',elf);normalize(elf);debug=out/'debug/alarm-service.elf';compact(elf,a.cc,debug_path=debug)
  shutil.copy2(out/'alarm-service/manifest.json',target/'manifest.json');providers['alarm-service']={'sha256':sha(elf),'psram_safety':verify(debug,a.cc),'profile':'native-utc-schedules-clock-edit'}
 board=json.loads((store/'board.json').read_text());next(d for d in board['devices'] if d['instance_id']==4)['bindings']={'i2c.bus':2}
 board['devices'].append({'instance_id':14,'chip':{'vendor':'x-powers','model':'axp2101-power-key','revision':'unspecified'},'compatible':'x-powers,axp2101-power-key','config_type':'peripheral.i2c-function','config_version':1,'config':{'parent_instance_id':4,'function_id':2},'bindings':{'i2c.bus':2}})
 board['devices'].append({'instance_id':15,'chip':{'vendor':'espressif','model':'esp32s3-wifi','revision':'unspecified'},'compatible':'espressif,esp32s3-wifi','config_type':'radio.integrated','config_version':1,'config':{'unit':0,'features':1},'bindings':{}})
 board['devices'].append({'instance_id':7,'chip':{'vendor':'qst','model':'qmi8658-idle','revision':'unspecified'},'compatible':'qst,qmi8658-idle','config_type':'peripheral.i2c','config_version':1,'config':{'bus_instance_id':101,'address':107,'chip_id':5,'irq':-1,'irq_active_high':False,'irq_pull_up':False},'bindings':{'i2c.bus':2}})
 board['devices'].append({'instance_id':17,'chip':{'vendor':'riscrte','model':'gpio-key-sleep','revision':'unspecified'},'compatible':'riscrte,gpio-key-sleep','config_type':'gpio.bank','config_version':1,'config':{'pins':[3],'active_high':True,'pull_up':False,'debounce_us':50000,'long_press_us':0,'click_min_us':0},'bindings':{'power.sleep-guard':7}})
 write(store/'board.json',board)
 boot=json.loads((store/'boot.json').read_text());boot['drivers'] += [{'manifest':'sensor-idle/manifest.json','instance_id':7},{'manifest':'sleep/manifest.json','instance_id':17},{'manifest':'wake-key/manifest.json','bindings':[{'capability':'input.power-key.raw','api':1,'instance_id':14},{'capability':'power.sleep','api':1,'instance_id':17}]},{'manifest':'wifi/manifest.json','instance_id':15},{'manifest':'battery/manifest.json','instance_id':4},{'manifest':'key/manifest.json','instance_id':14},{'manifest':'navigation/manifest.json','bindings':[{'capability':'input.power-key','api':1,'instance_id':0}]},{'manifest':'display-power/manifest.json','bindings':[{'capability':'display.output','api':1,'instance_id':5}]}]
 if a.time_server_sync:boot['drivers'].append({'manifest':'time-sample/manifest.json'})
 if a.native_alarms:
  policy=json.loads((out/'alarm-service/storage-policy.json').read_text())
  boot['drivers'].append({'manifest':'alarm-service/manifest.json','key_value':[{'key':key,**entry} for key,entry in policy.items()]})
 selected={'power.sleep':17,'display.output':5,'net.wifi':15,'input.touch.raw':6,'board.battery':4,'input.power-key':0,'input.navigation':0,'display.power':0,'runtime.realtime-control':0,'runtime.realtime':0,'runtime.provider-promotion':0,'storage.key-value':1,'storage.app-data':61,'storage.installed-files':0}
 if a.native_alarms:selected['alarm.service']=0
 if a.include_timecard:selected['storage.preferences']=1
 home_selected=dict(selected)
 if a.quick_actions:home_selected['storage.preferences']=1
 home=json.loads((store/'default.json').read_text());required=home['requires'];optional=home['optional'];grants=[]
 for d in required:
  if d['capability'] not in home_selected:raise ValueError('Unprovided Home requirement: '+str(d))
  grants.append({**d,'instance_id':home_selected[d['capability']]})
 boot['app_capabilities'][0]['grants']=grants
 for d in optional:
  if d['capability'] in home_selected:grants.append({**d,'instance_id':home_selected[d['capability']]})
 # Consume exact selected builder namespace records rather than overriding
 # app-private storage with the shared read-only-use preference grant.
 foreground_records={}
 if a.foreground_idle:
  for name,folder in [('settings','settings'),('springboard','springboard'),('wifi_settings','wifi'),*([('file_browser','files')] if a.include_files else [])]:
   foreground_records[name]=json.loads((out/folder/(name+'-build-record.json')).read_text())
  for app in json.loads((out/'daily/build-evidence.json').read_text())['apps']:foreground_records[app['id']]=app
  if a.native_alarms:
   for name in ['alarms','countdown']:foreground_records[name]=json.loads((out/'alarm-apps'/(name+'-build-record.json')).read_text())
  if a.include_timecard:foreground_records['timecard']=timecard_record
  for name,record in foreground_records.items():
   if not record.get('foreground_idle',{}).get('enabled') or 1 not in record['grant_bindings']['storage.key-value']:
    raise ValueError('Missing exact foreground policy receipt: '+name)
 alarm_ready_apps=[]
 for name,record in foreground_records.items():
  expected=a.alarm_ready_drain and name!='timecard'
  qualify_alarm_ready_drain(record,expected)
  if expected:alarm_ready_apps.append(name)
 # Optional services stay optional, but their grants are exact and packaged.
 for name in applications:
  app_selected=dict(selected)
  if name=='wifi_settings' and a.time_server_sync:app_selected['net.time-sample']=0
  if name=='springboard' and a.springboard_quick_actions:app_selected['storage.preferences']=1
  m=json.loads((store/(name+'.json')).read_text());req=m.get('requires',[]);g=[]
  for d in req:
   if d['capability'] not in app_selected:raise ValueError('Unprovided app requirement: '+name+' '+str(d))
   g.append({**d,'instance_id':{'stopwatch':2,'wifi_settings':6,'alarms':3,'countdown':3}[name] if name in ['stopwatch','wifi_settings','alarms','countdown'] and d['capability']=='storage.key-value' else app_selected[d['capability']]})
  for d in m.get('optional',[]):
   if d['capability'] in app_selected:g.append({**d,'instance_id':{'stopwatch':2,'wifi_settings':6,'alarms':3,'countdown':3}[name] if name in ['stopwatch','wifi_settings','alarms','countdown'] and d['capability']=='storage.key-value' else app_selected[d['capability']]})
  if name in foreground_records:
   namespaces=foreground_records[name]['grant_bindings']['storage.key-value']
   current={x['instance_id'] for x in g if x['capability']=='storage.key-value'}
   if not current.issubset(set(namespaces)):raise ValueError('Lost app-private storage: '+name)
   for namespace in namespaces:
    if namespace not in current:g.append({'capability':'storage.key-value','api':1,'instance_id':namespace})
  if name=='timecard' and a.include_timecard:
   g=apply_private_bindings(m,timecard_record,g);qualify_private_grants(m,timecard_record,g)
  if name=='springboard':
   qualify_temporary_quick(json.loads((out/'springboard/springboard-build-record.json').read_text()),m,g,a.springboard_quick_actions)
  boot['app_capabilities'].append({'manifest':name+'.json','grants':g})
 settings_record=json.loads((out/'settings/settings-build-record.json').read_text())
 raster_traffic_profile.qualify(settings_record,a.settings_alpha_geometry,a.raster_overwrite_clear,a.raster_underlay_exchange)
 raster_traffic_profile.qualify(json.loads((out/'springboard/springboard-build-record.json').read_text()),False,False,a.raster_underlay_exchange,a.nova_mask_bounds)
 if settings_record.get('sliced_native_frames',False) is not (a.native_settings_ui and a.sliced_native_frames):raise ValueError('Settings cooperative raster selection mismatch')
 if a.native_settings_ui and a.sliced_native_frames and settings_record.get('completed_frame_hit_maps') is not True:raise ValueError('Settings completed-frame hit authority missing')
 qualify_sleep_defaults(home_record,settings_record,a.idle_seconds)
 qualify_sleep_timer_controls(settings_record)
 write(store/'boot.json',boot)
 write(out/'APPLICATION-INVENTORY.json',collect_app_inventory(roots['system'],roots['utilities'],roots['controllers'],store,out/'catalog.json',roots.get('productivity')))
 if sha(store/'default.elf')!=sha(out/'shared-home/default.elf'):raise ValueError('Home byte identity changed')
 inputs=['scripts/time_sync_profile.py','scripts/raster_traffic_profile.py','scripts/home_raster_reuse_profile.py','scripts/springboard_gesture_profile.py','scripts/temporary_quick_profile.py','scripts/private_storage_profile.py','scripts/wifi_geometry_profile.py','scripts/alarm_geometry_profile.py','scripts/badge_app_inventory.py','scripts/build_badge_prototype.py','scripts/build_badge_home.py','boards/waveshare-esp32-s3-touch-amoled-1.75c/display-profile.h','boards/waveshare-esp32-s3-touch-amoled-1.75c/prototype/catalog.json','boards/waveshare-esp32-s3-touch-amoled-1.75c/prototype/sources.lock.json']
 for directory in ['components/co5300','components/i2c-scoped','Drivers/axp2101_observer','Drivers/axp2101_power_key','Drivers/power_key_navigation','Drivers/legacy_display_power','Drivers/legacy_power_compat','Drivers/qmi8658_accel','Drivers/gpio_key_sleep','Drivers/wake_key_filter','Drivers/qmi8658_idle','components/portable-light-sleep']:
  inputs += [str(x.relative_to(ROOT)) for x in (ROOT/directory).rglob('*') if x.is_file()]
 inputs.append('sdk/driver/RiscPowerKeyNeutralizeV1.h')
 if a.time_server_sync:
  inputs += ['Drivers/time_sample/driver.c','Drivers/time_sample/manifest.json','sdk/driver/RiscTimeSampleV1.h']
  if (ROOT/'sdk/driver/RiscTimeSampleV1.h').read_bytes()!=(roots['runtime']/'sdk/driver/RiscTimeSampleV1.h').read_bytes():raise ValueError('Time sampler canonical SDK drift')
 record={'schema':1,'time_server_sync':{'enabled':a.time_server_sync,'server':a.time_server if a.time_server_sync else None,'application':'wifi_settings' if a.time_server_sync else None,'authentication':'unauthenticated SNTP','clock_commit':'existing alarm-safe UTC transaction; timezone unchanged'},'raster_traffic':raster_traffic_profile.selection_record(a),'alarm_ready_drain':{'enabled':a.alarm_ready_drain,'apps':sorted(alarm_ready_apps),'budget_ms':8 if a.alarm_ready_drain else None,'home_unchanged':True,'timecard_not_adopted':True},'native_raster_caches':home_reuse,'springboard_gesture_diagnostics':a.springboard_gesture_diagnostics,'native_performance_trace':a.native_performance_trace,'native_performance_apps':['default','settings','springboard'] if a.native_performance_trace else [],'springboard_quick_actions':a.springboard_quick_actions,'interaction_trace':a.interaction_trace,'native_settings_geometry':a.native_settings_ui,'settings_sliced_native_frames':a.native_settings_ui and a.sliced_native_frames,'sliced_native_frames':a.sliced_native_frames,'include_timecard':a.include_timecard,'timecard_profile':timecard_record if a.include_timecard else None,'native_wifi_geometry':a.native_wifi_geometry,'quick_actions':a.quick_actions,'application_inventory_sha256':sha(out/'APPLICATION-INVENTORY.json'),'native_alarms':a.native_alarms,'native_alarm_geometry':a.native_alarm_geometry,'clock_edit_authority':a.native_alarms,'alarm_profile':'native-utc-schedules-clock-edit' if a.native_alarms else None,'native_daily':a.native_daily,'supported_app_increment':{'files':a.include_files,'storage':'read-only admitted installed manifest/ELF allowlist' if a.include_files else None},'springboard_presentation':{'profile':'watch-native-time','native_dimensions':True,'readonly_runtime_time':True},'foreground_idle':{'enabled':a.foreground_idle,'mode':'returning Light only','apps':sorted(foreground_records),'diagnostic_bringup':'not yet opted in; remains awake','home_policy_unchanged':not a.native_alarms},'profile':'round-prototype','time_authority':{'source':'native Runtime UTC','timezone':'shared saved time_zone; missing defaults UTC','external_rtc':False,'unset_local_default':'2026-01-01T00:00:00','default_only_when_unset':True},'sleep_policy':{'default_mode':'hybrid','sequence':'Light first; configured interval timer may advance to Deep','default_idle_seconds':a.idle_seconds,'saved_preferences_override':True,'wake':'PWRON SYS_OUT GPIO3 active high plus tagged schedule timer; no touch/motion' if a.native_alarms else 'PWRON SYS_OUT GPIO3 active high plus staged Light interval timer; no touch/motion'},'source_pins':lock,'shared_home_pins':base,'home':{'sha256':sha(store/'default.elf'),'unchanged_from_shared_build':True},'applications':applications,'providers':providers,'source_sha256':{n:sha(ROOT/n) for n in inputs},'store_files':{str(f.relative_to(store)):{'sha256':sha(f),'bytes':f.stat().st_size} for f in sorted(store.rglob('*')) if f.is_file()},'hardware_verified':False,'real_runtime_admission':False}
 write(out/'build.json',record);print('Prototype store built; full native import and graph admission still required')
if __name__=='__main__':main()

#!/usr/bin/env python3
"""Execute installed Timecard or explicit Points via the shared actual launcher/Runtime.
Use exact production recipes; the default preserves all Timecard scenarios.
Host instruction mapping, SDK/hardware and durable storage medium are modeled.
Target import/ELF admission is qualified separately; no physical device is used.
"""
import argparse,ast,json,os,hashlib,shutil,subprocess,sys,tempfile,re
from pathlib import Path
from badge_host_mapper import stage_runtime_mapper
from cst9217_sdk import stage_sdk
from compress_badge_rasters import compress_rasters
from badge_home_fixture_clock import controlled_home_source,home_watchdog_define
ROOT=Path(__file__).resolve().parents[1];TRACE=ROOT/'tests';BOARD=ROOT/'boards/waveshare-esp32-s3-touch-amoled-1.75c'
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
APP_PROBE='\nextern void badge_timecard_observe(const int*,const int*,const char*,const char*,const unsigned char*);\n__attribute__((visibility("default"))) void badge_timecard_ui_probe(void){\n int state[20]={tcn.active,screen_id,tcp_editor,selected,week_offset,editing_ymd,tcp_key_page,store_ready,tcn.count,day_count,tcp_time_format,tcn.w,tcn.h,portable_native_scene_ready(NULL,NULL),tcp_retained(),tcn.first,tcp_clock_valid,tcp_current_format_known,(int)tcp_cookie,0};\n int controls[40][6]={{0}};unsigned char chars[32]={0};\n for(unsigned i=0;i<tcn.count&&i<40;i++){controls[i][0]=tcn.controls[i].action;controls[i][1]=tcn.controls[i].arg;controls[i][2]=tcn.controls[i].r.x;controls[i][3]=tcn.controls[i].r.y;controls[i][4]=tcn.controls[i].r.w;controls[i][5]=tcn.controls[i].r.h;}\n for(unsigned i=0;i<32;i++)chars[i]=(unsigned char)portable_watch_key_character(tcp_key_page,i);\n badge_timecard_observe(state,&controls[0][0],tcp_entry,tcp_editor?tcp_editor_status:status_text,chars);\n}\n'
LAUNCHER_DECL='extern void badge_timecard_launcher_observe(unsigned,unsigned,unsigned,const int*,int,int);\n'
LAUNCHER_OBSERVE='int centers[19][2]={{0}};for(unsigned i=0;i<s->n&&i<19;i++){int dx=s->points[i].x+(int)(s->motion.x*16),dy=s->points[i].y+(int)(s->motion.y*16);centers[i][0]=api->screen_width()/2+nova_pixels(dx)/16;centers[i][1]=api->screen_height()/2+nova_pixels(dy)/16;}badge_timecard_launcher_observe(count,s->first,s->n,&centers[0][0],(int)(s->motion.x*16),(int)(s->motion.y*16));'
QUICK_PROBE='\n#include "PortableQuickLayout.h"\nextern void badge_timecard_quick_observe(const int*,const int*);\n__attribute__((visibility("default"))) void badge_timecard_quick_probe(void){\n pqa_layout l=pqa_layout_for(&quick.ui);const pqa_state*u=&quick.ui;\n int state[24]={pqa_visible(u),pqa_capture(u),u->position_q8,u->target_q8,u->brightness,quick.brightness,u->torch,u->dnd_valid,u->dnd_enabled,u->error_flags,u->wifi_valid,u->bluetooth_valid,u->radios_valid,u->contexts_available,u->audio_available,quick.retained,l.slider_left,l.slider_right,l.slider_y[0],l.slider_y[1],(int)u->pending,(int)quick_atomic_queue_count,u->save_pending,u->save_cancelled};\n const pqa_rect r[]={l.slider_hit[0],l.slider_hit[1],l.tiles[0],l.tiles[1],l.tiles[2],l.tiles[3],l.tiles[4],l.tiles[5],l.contexts,l.handle};int rects[10][4];for(unsigned i=0;i<10;i++){rects[i][0]=r[i].x;rects[i][1]=r[i].y;rects[i][2]=r[i].w;rects[i][3]=r[i].h;}\n badge_timecard_quick_observe(state,&rects[0][0]);\n}\n'
def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--app',choices=['timecard','points'],default='timecard',help='Explicit selected application; Timecard cases remain the default')
 p.add_argument('--scenario',action='append',help='Selected scenario(s) for this app; default runs its full preserved suite')
 p.add_argument('--home-system',type=Path,help='Exact separately pinned Home producer, required for Points successor')
 p.add_argument('--home-utilities',type=Path,help='Exact separately pinned Home SDK producer, required for Points successor')
 for n in ['runtime','watch','watch-bringup','controllers','system','system-baseline','utilities','utilities-baseline','productivity','store','output']:p.add_argument('--'+n,required=True,type=Path)
 p.add_argument('--compress-rasters',action='store_true',help='Verify and losslessly compress completed per-case PPMs');p.add_argument('--sanitize',action='store_true');p.add_argument('--foreground',action='store_true',help='Also execute returning foreground Light cases from target receipts');p.add_argument('--case',type=int,action='append',dest='cases',help='Run selected case indices for focused diagnosis');a=p.parse_args();a.output.unlink(missing_ok=True)
 roots={n:getattr(a,n.replace('-','_')).resolve() for n in ['runtime','watch','watch-bringup','controllers','system','system-baseline','utilities','utilities-baseline','productivity']}
 store=a.store.resolve();target=store.parent;record=json.loads((target/'build.json').read_text());assert record['profile']=='round-prototype';files_enabled=record.get('supported_app_increment',{}).get('files',False)
 points_mode=a.app=='points';points_enabled=record.get('include_points',False)
 assert type(points_enabled) is bool
 assert not points_mode or points_enabled,'Points mode requires selected mixed Points product'
 assert points_mode or not points_enabled,'Use explicit Points mode for the successor product'
 assert record.get('include_timecard') is True,'Selected Timecard product required'
 catalog_path=target/'catalog.json';launcher_catalog=json.loads(catalog_path.read_text())['apps']
 launcher_names=[x['file_name'] for x in launcher_catalog]
 assert len(launcher_names)==len(set(launcher_names)) and launcher_names.count('timecard.elf')==launcher_names.count('settings.elf')==1
 selected_executable='points_in_time.elf' if points_mode else 'timecard.elf'
 assert launcher_names.count(selected_executable)==1
 launcher_env={'TIMECARD_CATALOG_COUNT':str(len(launcher_names)),'TIMECARD_CATALOG_INDEX':str(launcher_names.index(selected_executable)),'TIMECARD_SETTINGS_INDEX':str(launcher_names.index('settings.elf'))}
 alarms_enabled=bool(record.get('native_alarms'));clock_edit_authority=bool(record.get('clock_edit_authority'))
 native_wifi_geometry=record.get('native_wifi_geometry',False);assert type(native_wifi_geometry) is bool
 native_alarm_geometry=bool(record.get('native_alarm_geometry'));quick_actions=record.get('quick_actions',False)
 assert not native_wifi_geometry or quick_actions,'Selected native WiFi product qualification requires Quick cohort'
 assert type(quick_actions) is bool,'Quick selection must be a JSON boolean'
 assert not quick_actions or (alarms_enabled and a.foreground),'Quick actions require the qualified native-alarm foreground profile'
 assert not native_alarm_geometry or alarms_enabled,'Native alarm geometry requires native alarms'
 assert clock_edit_authority==alarms_enabled,'Clock-edit authority must match native alarm selection'
 assert not alarms_enabled or record.get('alarm_profile')==('native-utc-mixed-points-clock-edit' if points_mode else 'native-utc-schedules-clock-edit')
 assert not alarms_enabled or (a.foreground and files_enabled),'Native alarms require full Files/foreground cohort'
 assert not files_enabled or a.foreground,'Files execution requires full foreground profile'
 assert {str(f.relative_to(store)):{'sha256':sha(f),'bytes':f.stat().st_size} for f in store.rglob('*') if f.is_file()}==record['store_files']
 lock=record['source_pins'] if points_mode else json.loads((BOARD/'prototype/sources.lock.json').read_text());base=json.loads((BOARD/'home/sources.lock.json').read_text())
 if points_mode:
  assert a.home_system and a.home_utilities,'Points requires explicit preserved Home producer roots'
  roots['home-system']=a.home_system.resolve();roots['home-utilities']=a.home_utilities.resolve()
 for n,r in roots.items():
  key=n.replace('-baseline','').replace('watch-bringup','watch_bringup').replace('home-','home_');expected=(base if n.endswith('-baseline') else lock)[key]['commit']
  assert subprocess.check_output(['git','-C',str(r),'rev-parse','HEAD'],text=True).strip()==expected
  assert not subprocess.check_output(['git','-C',str(r),'status','--porcelain'])
 W=roots['watch-bringup'];N=roots['runtime'];S=roots['system'];U=roots['utilities'];T=roots['controllers']
 sys.path.insert(0,str(W/'scripts'));from merged_sdk import headers
 home_record=json.loads((target/'shared-home/build.json').read_text());flags=home_record['artifacts']['default']['defines']
 assert type(home_record.get('native_quick_actions',False)) is bool,'Home Quick selection must be a JSON boolean'
 assert home_record.get('native_quick_actions',False)==quick_actions,'Home/product Quick profile mismatch'
 for flag in ['-DPORTABLE_QUICK_CAPABILITY_SAFE','-DPORTABLE_QUICK_SAFE_CONTENT']:
  assert (flag in flags)==quick_actions,'Home/product Quick compile flag mismatch: '+flag
 if alarms_enabled:assert '-DWATCH_NATIVE_ALARM_V2' in flags and '-DALARM_SERVICE_CLOCK_EDIT_V2' in flags and '-DWATCH_CLOCK_POINTS' not in flags
 sources={str(f):sha(f) for r in roots.values() for directory in ['Apps','apps','include','lib/PortableApps','sdk','src'] if (r/directory).exists() for f in (r/directory).rglob('*') if f.is_file()}
 for f in [ROOT/'tests/badge_timecard_execution.h',ROOT/'tests/badge_points_execution.h',ROOT/'tests/badge_points_execution.inc',ROOT/'tests/badge_timecard_execution.inc',Path(__file__).resolve(),ROOT/'tests/badge_timecard_execution.cpp',ROOT/'tests/badge_wifi_execution.inc']:sources[str(f)]=sha(f)
 if alarms_enabled:
  fixture=ROOT/'tests/badge_alarm_execution.inc';sources[str(fixture)]=sha(fixture)
 if quick_actions:
  fixture=ROOT/'tests/badge_quick_execution.inc';sources[str(fixture)]=sha(fixture)
 for directory in [ROOT/'components',ROOT/'Drivers',ROOT/'sdk/driver',BOARD/'supply']:
  for f in directory.rglob('*'):
   if f.is_file():sources[str(f)]=sha(f)
 sources[str(BOARD/'display-profile.h')]=sha(BOARD/'display-profile.h')
 sources[str(catalog_path)]=sha(catalog_path)
 if points_mode:
  for relative in ['scripts/badge_points_execution.py','scripts/points_product_profile.py','tests/badge_points_execution.h','tests/badge_points_execution.inc']:
   sources[str(ROOT/relative)]=sha(ROOT/relative)
 common=[home_watchdog_define(flags),'-pipe','-O1','-g1','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-missing-field-initializers'];san=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer'] if a.sanitize else []
 if record.get('time_server_sync',{}).get('enabled',False):common+=['-DBADGE_TIME_SERVER_SYNC=1']
 sources[str(TRACE/'badge_time_sample_fixture.h')]=sha(TRACE/'badge_time_sample_fixture.h')
 commands=[];results=[];rasters=a.output.parent/(a.output.stem+'-rasters');rasters.mkdir(parents=True,exist_ok=True)
 def run(xs,**kw):
  check_result=kw.pop('check_result',True)
  xs=list(map(str,xs));commands.append(xs);r=subprocess.run(xs,**kw)
  if r.returncode and check_result:
   print(getattr(r,'stdout','') or '');print(getattr(r,'stderr','') or '',file=sys.stderr);r.check_returncode()
  return r
 with tempfile.TemporaryDirectory(prefix='badge-prototype-exec-') as d:
  out=Path(d);host=out/'store';shutil.copytree(store,host)
  sdk=headers(out,roots['home-system'] if points_mode else S if alarms_enabled else roots['system-baseline'],roots['home-utilities'] if points_mode else U if alarms_enabled else roots['utilities-baseline'],W)
  providers=stage_sdk(ROOT,N,out/'provider-sdk')
  for directory in [W/'sdk/app',W/'sdk/driver',T/'include']:
   for h in directory.glob('*.h'):
    if not (providers/h.name).exists():shutil.copyfile(h,providers/h.name)
  home=[W/'apps/clock'/n for n in ['crown.c','viewport.c','capability_clients.c','quick_radios.c','nova/nova.c','points_projection.c','effects/divdi3.c']]+[roots['system-baseline']/'lib/PortableApps/src'/n for n in ['quick_actions.c','quick_render.c']]
  home.append(W/'apps/clock/watch_quick_session.c')
  if '-DWATCH_NATIVE_TIME_AUTHORITY' in flags:
   home += [roots['system-baseline']/'lib/PortableApps/src'/n for n in ['PortableTimeZone.c','PortableTimeZoneCatalog.c','PortableTimeZonePreference.c']]
  effect=out/'boot.o';run(['c++','-std=c++11',*common,*san,'-fPIC','-fvisibility=hidden','-fno-exceptions','-fno-rtti','-fno-threadsafe-statics','-I'+str(sdk),'-I'+str(W/'include'),'-c',W/'apps/clock/effects/boot.cpp','-o',effect])
  source_app={'default':(home+[effect],flags,[sdk,W/'include',roots['system-baseline']/'lib/PortableApps/src']),
    'bringup':([roots['watch-bringup']/'apps/bringup/main.c',roots['watch-bringup']/'apps/clock/viewport.c'],[],[roots['watch-bringup']/'include',providers])}
  if alarms_enabled:
   # The selected Home recipe, not the historical API1/Points composition.
   rec=home_record['artifacts']['default'];recipe=rec.get('compile_inputs')
   assert sha(host/'default.elf')==rec['sha256'],'Home receipt/store mismatch'
   assert recipe,'Native alarm Home requires exact production compile_inputs'
   home_paths=[Path(x) for x in recipe['sources']]
   home_inc=[Path(x) for x in recipe['include_directories']]
   assert recipe['definitions']==[x for x in flags if x.startswith('-D')] and len(home_paths)==len(set(home_paths))
   assert all(str(f) in rec['compile_command'] for f in home_paths)
   for f in home_paths:
    assert sha(f)==recipe['source_sha256'][str(f)],'Changed Home compiler input: '+str(f)
    sources[str(f)]=sha(f)
   for relative,digest in home_record['home_files_sha256'].items():
    assert sha(W/relative)==digest,'Changed selected Home source: '+relative
   for directory in home_inc:
    for f in directory.rglob('*'):
     if f.is_file():sources[str(f)]=sha(f)
   # Host instruction mapping cannot link an Xtensa object. Recompile the exact
   # Home boot-effect source with the same selected headers and host mechanics.
   object_paths=[f for f in home_paths if f.suffix=='.o'];assert len(object_paths)==1
   effect_recipe=rec['object_sources'][str(object_paths[0])]
   boot_source=Path(effect_recipe['source']);assert effect_recipe['language']=='c++'
   assert boot_source==W/'apps/clock/effects/boot.cpp' and sha(boot_source)==effect_recipe['source_sha256']
   assert str(boot_source) in effect_recipe['command']
   run(['c++','-std=c++11',*common,*san,'-fPIC','-fvisibility=hidden','-fno-exceptions','-fno-rtti','-fno-threadsafe-statics',*effect_recipe['definitions'],*['-I'+x for x in effect_recipe['include_directories']],'-c',boot_source,'-o',effect])
   source_app['default']=([effect if f.suffix=='.o' else f for f in home_paths],recipe['definitions'],home_inc)
  for name,folder,entry in [('settings','settings','settings_native_entry.c'),('springboard','springboard','springboard.c'),('wifi_settings','wifi','wifi_settings.c')]:
   rec=json.loads((target/folder/(name+'-build-record.json')).read_text());defines=rec['build_defines'] if name!='springboard' else ['-DPORTABLE_SAFE_VIEWPORT','-DPORTABLE_FORCE_FULL_FRAMES','-DPORTABLE_INPUT_NAVIGATION','-DPORTABLE_DISPLAY_ROTATION=0','-DPORTABLE_TOUCH_ROTATION=0','-DPORTABLE_RETURN_APP=\"default.elf\"'];inc=target/'settings/native-time-sdk/include' if name=='settings' else S/'lib/PortableApps/include'
   paths=[S/'Apps'/entry,S/'lib/PortableApps/src/adapter.c',target/folder/('springboard-catalog.c' if name=='springboard' else 'catalog.c')]
   if name=='settings':paths += [S/'lib/PortableApps/src'/n for n in ['PortableSetTime.c','PortableRealtimeClient.c','PortableTimeZone.c','PortableTimeZoneCatalog.c','PortableTimeZonePreference.c']]
   if name=='springboard':paths += [S/'lib/NativeApps/src/SingleFloatDivisionCompat.c']
   source_app[name]=(paths,defines,[inc,S/'lib/NativeApps/include'])
  for name in ['calculator','stopwatch','battery']:
   defines=['-DPORTABLE_SAFE_VIEWPORT','-DPORTABLE_FORCE_FULL_FRAMES','-DPORTABLE_INPUT_NAVIGATION'];paths=[U/'Apps'/(name+'.c'),S/'lib/PortableApps/src/adapter.c',target/'daily/catalog.c']
   if name=='stopwatch':defines+=['-DPORTABLE_STOPWATCH_NATIVE_UTC','-DPORTABLE_STOPWATCH_NATIVE_STANDALONE','-DPORTABLE_NATIVE_CUSTODY_FENCE','-DPORTABLE_APP_LAUNCH_GUARD'];paths += [S/'lib/PortableApps/src'/n for n in ['PortableRealtimeClient.c','PortableTimeZone.c','PortableTimeZoneCatalog.c']]
   source_app[name]=(paths,defines,[target/'daily/sdk',S/'lib/NativeApps/include'])
  # New foreground builds record the complete target recipe. Reuse it verbatim
  # for source files/defines/includes; only the host compiler/link mechanics differ.
  foreground_recipes={}
  records={name:json.loads((target/folder/(name+'-build-record.json')).read_text()) for name,folder in [('settings','settings'),('springboard','springboard'),('wifi_settings','wifi'),*([('file_browser','files')] if files_enabled else [])]}
  assert type(records['wifi_settings'].get('watch_native_ui',False)) is bool,'WiFi selection must be a JSON boolean'
  assert records['wifi_settings'].get('watch_native_ui',False)==native_wifi_geometry,'WiFi geometry receipt mismatch'
  assert not native_wifi_geometry or records['wifi_settings'].get('safe_content_native_raster') is True,'Native WiFi requires safe-content native raster receipt'
  assert ('-DPORTABLE_WIFI_NATIVE_PRESENTATION' in source_app['wifi_settings'][1])==native_wifi_geometry,'WiFi native compiler definition mismatch'
  daily=json.loads((target/'daily/build-evidence.json').read_text())
  assert bool(daily.get('native_safe_content'))==bool(record.get('native_daily')),'Target/daily native geometry profile mismatch'
  records.update({row['id']:row for row in daily['apps']})
  if alarms_enabled:
   alarm_apps=json.loads((target/'alarm-apps/build-evidence.json').read_text())
   assert bool(alarm_apps.get('native_alarm_geometry'))==native_alarm_geometry,'Target/alarm geometry profile mismatch'
   for name in ['alarms','countdown']:
    rec=json.loads((target/'alarm-apps'/(name+'-build-record.json')).read_text())
    assert rec['app']==name and rec['compile_inputs']
    assert bool(rec.get('native_alarm_geometry'))==native_alarm_geometry,'Alarm app geometry receipt mismatch: '+name
    native_defs=rec['compile_inputs']['definitions']
    assert ('-DPORTABLE_ALARM_NATIVE_PRESENTATION' in native_defs)==native_alarm_geometry,'Alarm geometry recipe mismatch: '+name
    if native_alarm_geometry:
     assert all('-D'+flag in native_defs for flag in ['PORTABLE_APP_OWNS_TOUCH_CHROME','PORTABLE_NOVA_UI','PORTABLE_RF_NATIVE_CANVAS','PORTABLE_SAFE_VIEWPORT','PORTABLE_WATCH_SAFE_INSETS','PORTABLE_WATCH_NATIVE_HEIGHT','PORTABLE_WATCH_COLOR_ONLY'])
     assert any(Path(path).name=='alarm_native_layout.h' for path in rec.get('compiled_dependencies_sha256',{})),'Missing exact native layout provenance'

    assert sha(target/'alarm-apps/debug'/(name+'.elf'))==rec['elf_sha256'],'Alarm app receipt/debug mismatch: '+name
    assert sha(host/(name+'.elf'))==sha(target/'alarm-apps'/(name+'.elf')),'Alarm app compacted/store mismatch: '+name
    for path,digest in rec.get('compiled_dependencies_sha256',{}).items():
     assert sha(path)==digest,'Changed alarm dependency: '+path
     sources[path]=digest
    records[name]=rec
  rec=json.loads((target/'timecard/timecard-build-record.json').read_text())
  assert rec==record['timecard_profile'],'Timecard target/product receipt mismatch'
  assert rec['manifest']==json.loads((store/'timecard.json').read_text())
  assert sha(host/'timecard.elf')==record['applications']['timecard']['sha256']
  assert rec['source_repositories']['productivity']['commit']==lock['productivity']['commit']
  from package_badge_prototype import qualify_timecard_profile
  policies=[x for x in json.loads((store/'boot.json').read_text())['app_capabilities'] if x['manifest']=='timecard.json'];assert len(policies)==1
  qualify_timecard_profile(record,rec['manifest'],policies[0]['grants'])
  assert json.loads((target/'catalog.json').read_text())['apps'][9]['file_name']=='timecard.elf'
  for path,digest in rec['consumed_source_sha256'].items():
   assert sha(path)==digest,'Changed Timecard dependency: '+path
   sources[path]=digest
  records['timecard']=rec
  if points_mode:
   from badge_points_execution import selected_recipe
   records['points_in_time']=selected_recipe(target,store,record,sha)
   sources.update(records['points_in_time']['consumed_source_sha256'])
  for name in ['timecard',*(['points_in_time'] if points_mode else []),'settings','springboard','wifi_settings','calculator','stopwatch','battery',*(['file_browser'] if files_enabled else []),*(['alarms','countdown'] if alarms_enabled else [])]:
   rec=records[name];receipt=rec.get('foreground_idle',daily.get('foreground_idle') if name in ['calculator','stopwatch','battery'] else None)
   if not receipt or not receipt.get('enabled'):
    assert not a.foreground,'Missing foreground receipt for '+name
    if (name=='springboard' and rec.get('watch_native_ui')) or (name in ['calculator','stopwatch','battery'] and daily.get('native_safe_content')):
     # Never silently substitute the legacy grid/time build for Watch-native.
     # Current nonforeground builders do not emit this provenance: fail closed.
     if not rec.get('compile_inputs'):
      raise SystemExit('Native UI production recipe provenance is required: missing compile_inputs; nonforeground execution is not qualified')
     recipe=rec['compile_inputs'];paths=[Path(x) for x in recipe['sources']];defines=recipe['definitions'];inc=[Path(x) for x in recipe['include_directories']]
     assert len(paths)==len(set(paths)) and all(str(f) in rec['compile_command'] for f in paths)
     for f in paths:
      assert sha(f)==recipe['source_sha256'][str(f)],'Changed compiler input: '+str(f)
      sources[str(f)]=sha(f)
     for include in inc:
      for header in include.rglob('*'):
       if header.is_file():sources[str(header)]=sha(header)
     for header,digest in rec.get('native_time_sdk',{}).get('sha256',{}).items():
      matches=[include/header for include in inc if (include/header).is_file()]
      assert matches and sha(matches[0])==digest,'Changed native-time staged header: '+header
     source_app[name]=(paths,defines,inc)
    continue
   recipe=rec['compile_inputs'];paths=[Path(x) for x in recipe['sources']];defines=recipe['definitions'];inc=[Path(x) for x in recipe['include_directories']]
   assert '-DPORTABLE_FOREGROUND_IDLE' in defines and len(paths)==len(set(paths))
   assert Path(receipt['compiled_include_directory']) in inc
   assert all(str(f) in rec['compile_command'] for f in paths)
   staged=Path(receipt['compiled_include_directory'])
   for relative,origin in receipt['header_origins'].items():
    header=staged/relative;assert header.is_file() and sha(header)==origin['sha256'],'Changed staged header: '+str(header)
   for header in staged.rglob('*'):
    if header.is_file():sources[str(header)]=sha(header)
   for relative,digest in receipt.get('time_sha256',{}).items():
    header=staged.parent/'time'/relative;assert header.is_file() and sha(header)==digest,'Changed staged time input: '+str(header)
    sources[str(header)]=sha(header)
   neutral=ROOT/'sdk/driver/RiscPowerKeyNeutralizeV1.h';sources[str(neutral)]=sha(neutral)
   for f in paths:
    digest=rec['consumed_source_sha256'][str(f)] if name=='points_in_time' else recipe['source_sha256'][str(f)]
    assert sha(f)==digest,'Changed compiler input: '+str(f)
    sources[str(f)]=sha(f)
   source_app[name]=(paths,defines,inc);foreground_recipes[name]={'compile_inputs':recipe,'foreground_idle':receipt}
   if name=='wifi_settings' and native_wifi_geometry:
    foreground_recipes[name].update({key:rec[key] for key in ('watch_native_ui','safe_content_native_raster','build_defines','display_rotation','touch_rotation')})
   if name in ['alarms','countdown']:foreground_recipes[name]['native_alarm_geometry']=native_alarm_geometry
  native_geometry_names=[]
  for name in ['calculator','stopwatch','battery']:
   defs=source_app[name][1]
   enabled='-DPORTABLE_UTILITIES_NATIVE_SAFE_CONTENT' in defs
   assert enabled==bool(daily.get('native_safe_content')),'Native geometry recipe/profile mismatch: '+name
   if enabled:
    assert all('-D'+flag in defs for flag in ['PORTABLE_APP_OWNS_TOUCH_CHROME','PORTABLE_NOVA_UI','PORTABLE_RF_NATIVE_CANVAS','PORTABLE_SAFE_VIEWPORT','PORTABLE_WATCH_SAFE_INSETS','PORTABLE_WATCH_NATIVE_HEIGHT','PORTABLE_WATCH_COLOR_ONLY'])
    for header,digest in records[name]['native_geometry_source_sha256'].items():
     assert sha(Path(header))==digest,'Changed native geometry source: '+header
     sources[header]=digest
    native_geometry_names.append(name)
  if a.foreground:assert len(foreground_recipes)==(7 if files_enabled else 6)+(2 if alarms_enabled else 0)+1+(1 if points_mode else 0)
  if alarms_enabled:
   assert len(foreground_recipes)==10+int(points_mode)
   for name in ['default',*foreground_recipes]:
    assert '-DALARM_SERVICE_CLOCK_EDIT_V2' in source_app[name][1],'Missing exact clock-edit client recipe flag: '+name
  settings_native_geometry='-DPORTABLE_SETTINGS_NATIVE_GEOMETRY' in source_app['settings'][1]
  assert settings_native_geometry==bool(records['settings'].get('native_settings_geometry')), 'Settings target receipt/actual host recipe selector mismatch'
  if settings_native_geometry:assert records['settings']['native_settings_geometry'].get('native_font_rasters') is True
  sources[str(ROOT/'scripts/badge_home_fixture_clock.py')]=sha(ROOT/'scripts/badge_home_fixture_clock.py')
  for fixture_name in ['badge_settings_geometry_fixture.inc','badge_settings_geometry_probe.inc']:
   sources[str(TRACE/fixture_name)]=sha(TRACE/fixture_name)
  exercised_apps={'default','springboard','points_in_time'} if points_mode else set(source_app)
  for name,(paths,defines,inc) in source_app.items():
   if name not in exercised_apps:continue
   objects=[]
   for i,f in enumerate(paths):
    rename=['-Dapp_main=badge_source_main']
    if name=='timecard' and f.name=='timecard_portable.c':
     original=f;text=f.read_text();assert text.count('void app_main(void)')==1
     f=out/'timecard-entry-observed.c';f.write_text(text.replace('void app_main(void)','void badge_source_main(void)')+APP_PROBE)
     rename=[];inc=[*inc,original.parent]
    if name=='points_in_time' and f.name=='points_in_time.c':
     from badge_points_execution import APP_PROBE as POINTS_PROBE
     original=f;f=out/'points-entry-observed.c';f.write_text('#include '+json.dumps(str(original))+'\n'+POINTS_PROBE);inc=[*inc,original.parent]
    if name=='springboard' and f.name=='springboard.c':
     original=f;partial=S/'Apps/springboard_nova.inc';text=partial.read_text();needle='    api->present(false);s->visible_x='
     assert text.count(needle)==1
     # Sliced presentation publishes a hit map only after its exact token completes.
     # The old render hook lives in the unselected synchronous branch.
     completed='            s->visible_valid=true;'
     assert text.count(completed)==1
     visible=LAUNCHER_OBSERVE.replace('(int)(s->motion.x*16)','s->visible_x').replace('(int)(s->motion.y*16)','s->visible_y')
     text=text.replace(completed,completed+'\n            if(s->visible_first==s->first && s->visible_n==s->n){'+visible+'}')
     observed=out/'springboard-nova-observed.inc';observed.write_text(LAUNCHER_DECL+text.replace(needle,'    '+LAUNCHER_OBSERVE+'\n'+needle))
     text=original.read_text();assert text.count('#include "springboard_nova.inc"')==1
     f=out/'springboard-entry-observed.c';f.write_text(text.replace('#include "springboard_nova.inc"','#include '+json.dumps(str(observed))))
     inc=[*inc,original.parent];sources[str(partial)]=sha(partial)
    if name=='settings' and f.name=='settings_native_entry.c':
     text=f.read_text();assert text.count('void app_main(void)')==1
     overlay=out/'settings-entry.c';overlay.write_text(text.replace('void app_main(void)','void badge_source_main(void)'));f=overlay;rename=[];inc=[*inc,S/'Apps']
    if alarms_enabled and f.name=='adapter.c':
     adapter_original=f;alarm_original=f.parent/'alarm.inc';alarm_text=alarm_original.read_text()
     signature='static bool alarm_restore(const uint16_t *saved) {'
     end='    present(false);return portable_paper_frame_drain() && !failed && display_settled;\n}'
     assert alarm_text.count(signature)==1 and alarm_text.count(end)==1,'Changed actual alarm restore boundary'
     alarm_text=alarm_text.replace(signature,signature.replace('alarm_restore','badge_source_alarm_restore'))
     wrapper='\nextern void badge_alarm_restore_phase(int);\nstatic bool alarm_restore(const uint16_t *saved){badge_alarm_restore_phase(1);bool result=badge_source_alarm_restore(saved);badge_alarm_restore_phase(0);return result;}\n'
     alarm_text=alarm_text.replace(end,end+wrapper)
     overlay_dir=out/(name+'-restore-observed');overlay_dir.mkdir()
     alarm_overlay=overlay_dir/'alarm.inc';alarm_overlay.write_text(alarm_text)
     adapter_text=adapter_original.read_text();assert adapter_text.count('#include "alarm.inc"')==1
     f=overlay_dir/'adapter.c';f.write_text(adapter_text.replace('#include "alarm.inc"','#include '+json.dumps(str(alarm_overlay))))
     sources[str(adapter_original)]=sha(adapter_original);sources[str(alarm_original)]=sha(alarm_original)
     inc=[*inc,adapter_original.parent]
    if name in foreground_recipes:
     # Read-only observation overlays expose real model state and transaction
     # results. They do not synthesize input, change data or replace execution.
     extra=''
     if name=='settings' and f.name=='adapter.c':
      extra='\n__attribute__((visibility("default"))) unsigned long long badge_foreground_probe(char *out,size_t cap){snprintf(out,cap,"%u/%u/%u %u:%u:%u page=%u field=%u",settings_draft.year,settings_draft.month,settings_draft.day,settings_draft.hour,settings_draft.minute,settings_draft.second,sv_page,editor_field);return sv_page==SV_VALUE&&editor_field==0&&settings_draft.year==2027;}\n'
     if name=='calculator' and f.name=='calculator.c':
      extra='\n#include <stdio.h>\n__attribute__((visibility("default"))) unsigned long long badge_foreground_probe(char *out,size_t cap){snprintf(out,cap,"%s pending=%c accumulator=%lld editing=%u",calculator_state.text,calculator_state.pending?calculator_state.pending:\'0\',(long long)calculator_state.accumulator,(unsigned)calculator_state.editing);return !strcmp(calculator_state.text,"8")&&calculator_state.pending==\'+\'&&calculator_state.accumulator==7000000;}\n'
     if name=='stopwatch' and f.name=='stopwatch.c':
      extra='\n#include <stdio.h>\n__attribute__((visibility("default"))) unsigned long long badge_foreground_probe(char *out,size_t cap){snprintf(out,cap,"running=%u elapsed=%lu uncertain=%u",(unsigned)clock_state.running,(unsigned long)clock_state.elapsed_ms,(unsigned)save_uncertain);return clock_state.running&&!save_uncertain?clock_state.elapsed_ms:0;}\n'
     if name=='file_browser' and f.name=='file_browser.c':
      extra='\nextern void badge_files_observe(unsigned,const char*,const char*,const unsigned char*,unsigned,int);\n__attribute__((visibility("default"))) unsigned long long badge_foreground_probe(char *out,size_t cap){int readonly=fb_volume&&!fb_volume->file_open_write&&!fb_volume->file_write&&!fb_volume->remove&&!risc_storage_volume_extension(fb_volume);badge_files_observe(fb_mode,fb_editor,fb_file_path,fb_preview,fb_preview_count,readonly);snprintf(out,cap,"loaded=%u count=%u mode=%u page=%u manage=%u path=%s draft=%s",(unsigned)fb_loaded,fb_count,fb_mode,fb_page,(unsigned)fb_manage,fb_path,fb_editor);return fb_loaded&&fb_count&&!fb_manage&&portable_file_browser_safe()&&(fb_mode!=FB_FILTER||!strcmp(fb_editor,"a"))&&(fb_mode!=FB_PREVIEW||fb_preview_count); }\n'
     if name=='wifi_settings' and f.name=='wifi_settings.c':
      extra='\n__attribute__((visibility("default"))) unsigned long long badge_foreground_probe(char *out,size_t cap){snprintf(out,cap,"page=%u scanning=%u joining=%u cleanup=%u draft=%u",page,(unsigned)scanning,(unsigned)joining,(unsigned)cleanup_pending,(unsigned)draft_dirty);return scanning||joining||cleanup_pending;}\n'
     if name=='wifi_settings' and f.name=='wifi_settings.c' and native_wifi_geometry:
      extra=extra.replace('snprintf(out,cap,','badge_wifi_native_probe();snprintf(out,cap,')
      geometry='\n#include "PortableWifiNativeLayout.h"\nextern void badge_wifi_observe(const unsigned*,const int*,const char*,const char*,const char*,const char*,const char*);\nstatic void badge_wifi_native_probe(void){if(!wa||!opened)return;wifi_native_layout l;if(!wifi_native_layout_open(&l,wa->screen_width(),wa->screen_height()))return;unsigned s[14]={wa->screen_width(),wa->screen_height(),page,choice,key_page,l.row_count,scanning,joining,cleanup_pending,draft_dirty,scan_result.count,armed,open_network,saved_state};wifi_native_rect r[47]={l.back,l.footer[0],l.footer[1],l.footer[2]};for(unsigned i=0;i<8;i++)r[4+i]=l.rows[i];for(unsigned i=0;i<PWK_COUNT;i++)r[12+i]=l.keys[i];int rects[47][4];for(unsigned i=0;i<47;i++){rects[i][0]=r[i].x;rects[i][1]=r[i].y;rects[i][2]=r[i].w;rects[i][3]=r[i].h;}badge_wifi_observe(s,&rects[0][0],editor,credentials.ssid,credentials.password,view.entry,wifi_message);}\n'
      extra=geometry+extra
     if extra:
      original=f;f=out/(name+'-observed-'+f.name);f.write_text('#include '+json.dumps(str(original))+'\n'+extra)
     if f.name=='foreground_light_sleep.c':
      original=f;f=out/(name+'-observed-light.c');f.write_text('#define risc_runtime_sleep_prepare_v1 badge_native_prepare_original\n#define risc_runtime_sleep_resume_v1 badge_native_resume_original\n#include "RiscRuntimeSleepV1.h"\n#undef risc_runtime_sleep_prepare_v1\n#undef risc_runtime_sleep_resume_v1\nextern void badge_sleep_transaction(int,unsigned long long,int);\nstatic inline int32_t risc_runtime_sleep_prepare_v1(const risc_runtime_api_v1 *r,uint64_t *t){int32_t z=badge_native_prepare_original(r,t);badge_sleep_transaction(1,*t,z);return z;}\nstatic inline int32_t risc_runtime_sleep_resume_v1(const risc_runtime_api_v1 *r,uint64_t t){int32_t z=badge_native_resume_original(r,t);badge_sleep_transaction(0,t,z);return z;}\n#include '+json.dumps(str(original))+'\n')
    if alarms_enabled:
     extra=''
     # These observers only read the real production state. No schedule, ticket,
     # app draft or service result is synthesized by an observation overlay.
     if name=='default' and f.name=='crown.c':
      extra='\nextern void badge_alarm_home_observe(int,unsigned,unsigned,int);\n__attribute__((visibility("default"))) void badge_alarm_ui_probe(void){badge_alarm_home_observe(clock_alarm_modal,clock_alarm.status.occurrence.generation,clock_alarm.status.state,clock_alarm.status.error); }\n'
     if f.name=='adapter.c' or f.name.endswith('-observed-adapter.c'):
      extra='\nextern void badge_alarm_home_observe(int,unsigned,unsigned,int);extern void badge_alarm_touch_observe(int,int);\n__attribute__((visibility("default"))) void badge_alarm_adapter_probe(void){badge_alarm_home_observe(alarm_modal,alarms.status.occurrence.generation,alarms.status.state,alarms.status.error);badge_alarm_touch_observe(touch.grant.api!=NULL,touch.subscription!=0); }\n'
     if name in ['alarms','countdown'] and f.name==name+'.c':
      extra='\nextern void badge_alarm_app_observe(unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,int,int);\n__attribute__((visibility("default"))) void badge_alarm_ui_probe(void){badge_alarm_app_observe(DAILY_ALARM_KIND,values[0],values[1],values[2],writer.saved.revision,writer.saved.deadline,editing,native_retained); }\n__attribute__((visibility("default"))) unsigned long long badge_foreground_probe(char*out,size_t cap){snprintf(out,cap,"%u:%u:%u editing=%u revision=%u deadline=%u retained=%u",values[0],values[1],values[2],(unsigned)editing,writer.saved.revision,writer.saved.deadline,(unsigned)native_retained);return ((unsigned long long)values[0]<<32)|((unsigned long long)values[1]<<16)|values[2];}\n'
     if quick_actions and name=='default' and f.name=='crown.c':
      quick_probe='\n#include "PortableQuickLayout.h"\nextern void badge_quick_home_observe(const int*,const int*);extern void badge_timecard_quick_observe(const int*,const int*);\nstatic void badge_quick_home_probe(void){if(!rt)return;pqa_layout l=pqa_layout_for(&clock_quick.ui);const pqa_state*u=&clock_quick.ui;int state[24]={pqa_visible(u),pqa_capture(u),u->position_q8,u->target_q8,u->brightness,clock_quick.brightness,u->torch,u->dnd_valid,u->dnd_enabled,u->error_flags,u->wifi_valid,u->bluetooth_valid,u->radios_valid,u->contexts_available,u->audio_available,clock_quick.retained,l.slider_left,l.slider_right,l.slider_y[0],l.slider_y[1],(int)u->pending,(int)quick_atomic_queue_count,u->save_pending,u->save_cancelled};const pqa_rect r[]={l.slider_hit[0],l.slider_hit[1],l.tiles[0],l.tiles[1],l.tiles[2],l.tiles[3],l.tiles[4],l.tiles[5],l.contexts,l.handle};int rects[10][4];for(unsigned i=0;i<10;i++){rects[i][0]=r[i].x;rects[i][1]=r[i].y;rects[i][2]=r[i].w;rects[i][3]=r[i].h;}badge_quick_home_observe(state,&rects[0][0]);badge_timecard_quick_observe(state,&rects[0][0]);}\n'
      extra=quick_probe+extra.replace('badge_alarm_ui_probe(void){','badge_alarm_ui_probe(void){badge_quick_home_probe();')
     if native_alarm_geometry and name in ['alarms','countdown'] and f.name==name+'.c':
      # Read-only bridge calls the actual production layout and observes nested
      # page/field. It never writes app state or dispatches an app action.
      extra=extra.replace('badge_alarm_ui_probe(void){','badge_alarm_ui_probe(void){badge_alarm_native_geometry_probe();')
      extra=extra.replace('retained=%u",values[0]','retained=%u page=%u field=%u",values[0]')
      extra=extra.replace('(unsigned)native_retained);return','(unsigned)native_retained,alarm_page,alarm_field);return')
      geometry='\nextern void badge_alarm_geometry_observe(unsigned,unsigned,unsigned,unsigned,const int*);\nstatic void badge_alarm_native_geometry_probe(void){if(!app||native_retained)return;alarm_native_layout l=alarm_native_layout_make(app->screen_width(),app->screen_height(),DAILY_ALARM_KIND,alarm_page);if(!l.valid)return;const alarm_native_rect r[]={l.back,l.edit,l.primary,l.cancel,l.retry,l.fields[0],l.fields[1],l.fields[2],l.minus,l.plus,l.ampm,l.done};int rects[12][4];for(unsigned i=0;i<12;i++){rects[i][0]=r[i].x;rects[i][1]=r[i].y;rects[i][2]=r[i].width;rects[i][3]=r[i].height;}badge_alarm_geometry_observe(app->screen_width(),app->screen_height(),alarm_page,alarm_field,&rects[0][0]);}\n'
      extra=geometry+extra
     if not native_alarm_geometry and name in ['alarms','countdown'] and f.name==name+'.c':
      extra='\nextern void badge_alarm_generic_geometry_observe(unsigned,unsigned);\n'+extra.replace('badge_alarm_ui_probe(void){','badge_alarm_ui_probe(void){if(app&&!native_retained)badge_alarm_generic_geometry_observe(app->screen_width(),app->screen_height());')
     if name=='timecard' and (f.name=='adapter.c' or f.name.endswith('-observed-adapter.c')):extra+=QUICK_PROBE
     if extra:
      original=f;f=out/(name+'-alarm-observed-'+f.name)
      if quick_actions and name=='default' and original.name=='crown.c':
       # Explicit host-only provider-result fault below the real client parser.
       # Unknown results still traverse its production retained-custody checks.
       atomic_header=next(directory/'PortablePreferenceAtomic.h' for directory in inc if (directory/'PortablePreferenceAtomic.h').is_file())
       atomic_text=atomic_header.read_text();collect='int rc=s->api->collect(s->api->context,s->token,&receipt);'
       assert atomic_text.count(collect)==1
       atomic_text=atomic_text.replace(collect,collect+'if(rc==RISC_ATOMIC_OK)rc=badge_quick_collect_result(rc);')
       calls={name:0 for name in ['state','get','submit','collect','cancel']}
       def observe_preference_call(match):
        calls[match.group(2)]+=1
        return 'badge_quick_preference_call();'+('badge_quick_submit_observe();' if match.group(2)=='submit' else '')+match.group(1)
       atomic_text=re.sub(r'((?:int )?rc=(?:s->)?api->(state|get|submit|collect|cancel)\([^;\n]*\);)',observe_preference_call,atomic_text)
       assert calls=={'state':4,'get':3,'submit':1,'collect':1,'cancel':1},'Changed selected preference callback surface'
       atomic_overlay=out/'quick-observed-preference-atomic.h';atomic_overlay.write_text('extern int badge_quick_collect_result(int);extern void badge_quick_preference_call(void);extern void badge_quick_submit_observe(void);\n'+atomic_text)
       sources[str(atomic_header)]=sha(atomic_header)
       text=controlled_home_source(original.read_text());capture='quick_atomic_capture(quick_actions)';assert text.count(capture)==1
       text=text.replace(capture,'badge_quick_capture_observed(rt,quick_actions,quick_atomic_capture(quick_actions))')
       alarm_source=original.parent/'clock_alarm.inc';alarm_text=alarm_source.read_text()
       preempt='clock_alarm_cancel_input();clock_alarm_modal=true;'
       assert alarm_text.count(preempt)==1
       alarm_text=alarm_text.replace(preempt,'badge_quick_preempt_observe(pqa_visible(&clock_quick.ui),pqa_capture(&clock_quick.ui),clock_quick.ui.pending,clock_alarm.status.occurrence.generation);'+preempt)
       alarm_overlay=out/'quick-observed-clock-alarm.inc';alarm_overlay.write_text('extern void badge_quick_preempt_observe(int,int,unsigned,unsigned);\n'+alarm_text)
       sources[str(alarm_source)]=sha(alarm_source)
       assert text.count('#include "clock_alarm.inc"')==1
       text=text.replace('#include "clock_alarm.inc"','#include '+json.dumps(str(alarm_overlay)))
       pending_source=original.parent/'quick_pending.inc';pending_text=pending_source.read_text()
       pending_clear='u->save_pending=false;u->save_cancelled=false;'
       assert pending_text.count(pending_clear)==1,'Changed selected pending cancellation boundary'
       pending_text=pending_text.replace(pending_clear,'badge_quick_pending_cancel_observe(u->save_cancelled);'+pending_clear,1)
       pending_overlay=out/'quick-observed-pending.inc';pending_overlay.write_text('extern void badge_quick_pending_cancel_observe(int);\n'+pending_text)
       sources[str(pending_source)]=sha(pending_source)
       assert text.count('#include "quick_pending.inc"')==1
       text=text.replace('#include "quick_pending.inc"','#include '+json.dumps(str(pending_overlay)))
       prefix='#include <stdbool.h>\n#include <stdint.h>\n#include "RiscRuntimeV1.h"\nextern bool badge_quick_capture_observed(const risc_runtime_api_v1*,uint32_t,bool);\n#include '+json.dumps(str(atomic_overlay))+'\n'
       f.write_text(prefix+text+'\n'+extra);inc=[*inc,original.parent]
      elif name=='timecard' and original.name=='adapter.c':
       # Actual collect runs first. Only the dedicated negative scenario may
       # change its successful result; consumer custody checks remain unchanged.
       atomic_header=next(directory/'PortablePreferenceAtomic.h' for directory in inc if (directory/'PortablePreferenceAtomic.h').is_file())
       atomic_text=atomic_header.read_text();collect='int rc=s->api->collect(s->api->context,s->token,&receipt);'
       assert atomic_text.count(collect)==1
       atomic_text=atomic_text.replace(collect,collect+'if(rc==RISC_ATOMIC_OK)rc=badge_timecard_collect_result(rc);')
       calls={name:0 for name in ['state','get','submit','collect','cancel']}
       def observe_timecard_preference_call(match):
        calls[match.group(2)]+=1
        return 'badge_timecard_preference_call();'+('badge_timecard_submit_observe();' if match.group(2)=='submit' else '')+match.group(1)
       atomic_text=re.sub(r'((?:int )?rc=(?:s->)?api->(state|get|submit|collect|cancel)\([^;\n]*\);)',observe_timecard_preference_call,atomic_text)
       assert calls=={'state':4,'get':3,'submit':1,'collect':1,'cancel':1},'Changed Timecard preference callback surface'
       atomic_overlay=out/'timecard-observed-preference-atomic.h';atomic_overlay.write_text('extern int badge_timecard_collect_result(int);extern void badge_timecard_preference_call(void);extern void badge_timecard_submit_observe(void);\n'+atomic_text)
       sources[str(atomic_header)]=sha(atomic_header)
       f.write_text('#include '+json.dumps(str(atomic_overlay))+'\n#include '+json.dumps(str(original))+'\n'+extra)
      elif name=='default' and original.name=='crown.c':
       f.write_text(controlled_home_source(original.read_text())+'\n'+extra);inc=[*inc,original.parent]
      else:f.write_text('#include '+json.dumps(str(original))+'\n'+extra)
    if name=='settings' and 'adapter.c' in f.name:
     original=f;f=out/'settings-layout-observed-adapter.c'
     f.write_text('#include '+json.dumps(str(original))+'\n#include '+json.dumps(str(TRACE/'badge_settings_geometry_probe.inc'))+'\n')
    if name=='default' and f.name=='crown.c':
     original=f;f=out/'home-controlled-observation.c';f.write_text(controlled_home_source(original.read_text()));inc=[*inc,original.parent]
    if f.suffix=='.o':objects.append(f);continue
    obj=out/(name+str(i)+'.o');run(['cc','-std=c11',*common,*san,'-fPIC','-fvisibility=hidden',*rename,*defines,*['-I'+str(x) for x in inc],'-c',f,'-o',obj]);objects.append(obj)
   observer_declarations='';observer_enter='';observer_exit=''
   if alarms_enabled:
    observer_declarations='extern void badge_alarm_ui_probe(void) __attribute__((weak));extern void badge_alarm_adapter_probe(void) __attribute__((weak));extern void badge_alarm_ui_register(void(*)(void));extern void badge_timecard_ui_probe(void) __attribute__((weak));extern void badge_timecard_quick_probe(void) __attribute__((weak));static void badge_ui_observe(void){if(badge_timecard_ui_probe)badge_timecard_ui_probe();if(badge_timecard_quick_probe)badge_timecard_quick_probe();if(badge_alarm_adapter_probe)badge_alarm_adapter_probe();if(badge_alarm_ui_probe)badge_alarm_ui_probe();}'
    if points_mode:observer_declarations=observer_declarations.replace('static void badge_ui_observe(void){','extern void badge_points_ui_probe(void) __attribute__((weak));static void badge_ui_observe(void){if(badge_points_ui_probe)badge_points_ui_probe();')
    observer_enter='badge_alarm_ui_register(badge_ui_observe);';observer_exit='badge_alarm_ui_register(0);'
   observer_declarations+='extern void badge_settings_geometry_probe(int*) __attribute__((weak));extern void badge_settings_geometry_register(void(*)(int*));'
   wrapper=out/(name+'-entry.c');wrapper.write_text(observer_declarations+'\n#include <stddef.h>\nextern void badge_source_main(void);extern void badge_app_enter(const char*);extern void badge_app_exit(const char*);extern unsigned long long badge_foreground_probe(char*,size_t) __attribute__((weak));extern void badge_probe_register(unsigned long long (*)(char*,size_t));__attribute__((visibility("default"))) void app_main(void){badge_app_enter('+json.dumps(name)+');badge_probe_register(badge_foreground_probe);badge_settings_geometry_register(badge_settings_geometry_probe);'+observer_enter+'badge_source_main();badge_app_exit('+json.dumps(name)+');badge_probe_register(0);badge_settings_geometry_register(0);'+observer_exit+'}\n')
   run(['cc',*san,'-shared',*objects,wrapper,'-o',host/(name+'.elf')])
   # Every app has a distinct host object set. Once its module is linked,
   # release only completed attempt-owned objects (never target/source inputs).
   for obj in objects:
    if obj.parent==out and obj.suffix=='.o':obj.unlink()
  driver_sources={'sensor-idle':ROOT/'Drivers/qmi8658_idle/driver.c','wake-key':ROOT/'Drivers/wake_key_filter/driver.c','sleep':ROOT/'Drivers/gpio_key_sleep/driver.c','i2c':ROOT/'components/i2c-scoped/driver.c','display':ROOT/'components/co5300/facade/driver.c','supply':BOARD/'supply/driver.c','touch':ROOT/'Drivers/cst9217_touch/driver.c','battery':ROOT/'Drivers/axp2101_observer/driver.c','key':ROOT/'Drivers/axp2101_power_key/driver.c','navigation':ROOT/'Drivers/power_key_navigation/driver.c','display-power':ROOT/'Drivers/legacy_display_power/driver.c','wifi':T/'drivers/twatch_wifi/driver.c'}
  if record.get('time_server_sync',{}).get('enabled',False):driver_sources['time-sample']=ROOT/'Drivers/time_sample/driver.c'
  for name,f in driver_sources.items():run(['cc','-std=c11',*common,*san,'-fPIC','-shared','-fvisibility=hidden',*['-I'+str(x) for x in [providers,ROOT/'components/co5300/include',W/'include',T/'include']],*(['-include',BOARD/'display-profile.h'] if name=='display' else []),f,'-o',host/name/'driver.elf'])
  alarm_service_record=None
  if alarms_enabled:
   alarm_service_record=json.loads((target/'alarm-service/build-evidence.json').read_text())
   if points_mode:
    from points_product_profile import qualify_service
    qualify_service(alarm_service_record,json.loads((target/'alarm-service/storage-policy.json').read_text()))
   else:assert alarm_service_record['profile']=='native-utc-schedules-clock-edit' and not alarm_service_record['points_selected']
   expected_keys={**{k:{'namespace':3,'access':'read'} for k in ['alarm_utc_cfg','timer_utc_cfg']},**{k:{'namespace':4,'access':'read-write'} for k in ['alarm_utc_occ','timer_utc_occ','clock_edit_auth']},**{k:{'namespace':1,'access':'read'} for k in ['alert_mode','alert_dnd']}}
   if points_mode:expected_keys.update({'points_utc_cfg':{'namespace':5,'access':'read'},'points_utc_occ':{'namespace':4,'access':'read-write'},'time_zone':{'namespace':1,'access':'read'}})
   assert alarm_service_record['bound_keys']==expected_keys
   assert json.loads((target/'alarm-service/storage-policy.json').read_text())==expected_keys
   for path,digest in alarm_service_record['dependencies_sha256'].items():
    assert sha(path)==digest,'Changed alarm service dependency: '+path
    sources[path]=digest
   command=alarm_service_record['command']
   service_sources=[Path(x) for x in command if str(x).endswith('.c')]
   service_inc=[x for x in command if x.startswith('-I')]
   service_defines=[x for x in command if x.startswith('-D')]
   assert service_sources==[U/'Services/alarm_service/service.c',*([S/'lib/PortableApps/src/PortableTimeZone.c',S/'lib/PortableApps/src/PortableTimeZoneCatalog.c'] if points_mode else [])]
   assert set(service_defines)=={'-D'+x for x in alarm_service_record['defines']}
   assert '-DALARM_SERVICE_CLOCK_EDIT_V2' in service_defines
   if not points_mode:assert '-DALARM_NATIVE_SCHEDULES_ONLY' in service_defines and not any('POINTS' in x for x in service_defines)
   service_manifest=json.loads((target/'alarm-service/manifest.json').read_text())
   assert [x['api'] for x in service_manifest['requires'] if x['capability']=='storage.key-value.bound']==[2],'Clock-edit marker requires explicit bound KV API2'
   bound_header=N/'sdk/driver/RiscBoundKeyValueV2.h'
   assert re.search(r'#define\s+RISC_BOUND_KEY_VALUE_API_V2\s+2u\b',bound_header.read_text())
   assert re.search(r'#define\s+RISC_BOUND_KEY_VALUE_V2_BLOB_MAX\s+2048u\b',bound_header.read_text()),'Unreviewed bound KV API2 blob limit'
   sources[str(bound_header)]=sha(bound_header)
   candidates=[f.parent for f in host.rglob('manifest.json') if json.loads(f.read_text()).get('provides')==service_manifest.get('provides')]
   assert len(candidates)==1,'Ambiguous alarm service install path'
   assert sha(target/'alarm-service/driver.elf')==alarm_service_record['elf_sha256'],'Alarm service receipt/source ELF mismatch'
   assert sha(target/'debug/alarm-service.elf')==alarm_service_record['elf_sha256'],'Alarm service compaction input mismatch'
   # Explicit negative-case-only callback-output fault seam. The underlying
   # production function always runs; only case29 alters its returned ticket.
   # No observation/fault callback runs after a retained or failed result.
   service_text=service_sources[0].read_text()
   # Observe entry, including status-only calls that need not touch a provider.
   for function in ['prepare_sleep','step','status','refresh','resume_sleep','acknowledge','stop_only']:
    pattern=r'(static int32_t '+function+r'\([^\n]*\) \{)'
    service_text,count=re.subn(pattern,r'\1 badge_alarm_service_call();',service_text)
    assert count==1,'Missing/ambiguous service dispatch: '+function
   service_text='extern void badge_alarm_service_call(void);\n'+service_text
   declaration='static int32_t prepare_sleep(void *c,alarm_sleep_v1 *out) {'
   marker='static int32_t resume_sleep(void *c,const alarm_sleep_v1 *decision) {'
   assert service_text.count(declaration)==1 and service_text.count(marker)==1
   service_text=service_text.replace(declaration,'static int32_t badge_source_prepare_sleep(void *c,alarm_sleep_v1 *out) {')
   service_text=service_text.replace(marker,'extern void badge_alarm_ticket_fault(void*,int);\nstatic int32_t prepare_sleep(void*c,alarm_sleep_v1*out){int32_t rc=badge_source_prepare_sleep(c,out);if(rc==ALARM_OK)badge_alarm_ticket_fault(out,rc);return rc;}\n'+marker)
   resume_declaration='static int32_t resume_sleep(void *c,const alarm_sleep_v1 *decision) {'
   resume_marker='static int32_t stop_only_internal(void) {'
   assert service_text.count(resume_declaration)==1 and service_text.count(resume_marker)==1
   service_text=service_text.replace(resume_declaration,'static int32_t badge_source_resume_sleep(void *c,const alarm_sleep_v1 *decision) {')
   service_text=service_text.replace(resume_marker,'extern void badge_alarm_resume_observe(const void*,int);\nstatic int32_t resume_sleep(void*c,const alarm_sleep_v1*decision){badge_alarm_resume_observe(decision,0);int32_t rc=badge_source_resume_sleep(c,decision);if(rc==ALARM_OK)badge_alarm_resume_observe(decision,1);return rc;}\n'+resume_marker)
   # Cut only the negative recovery case after the real service durably grants
   # its clock-edit permit and before Settings can seed RTC. Never callback
   # after retained/failure, and never bypass begin's production persistence.
   protocol_name='clock_edit_mixed_protocol.inc' if points_mode else 'clock_edit_protocol.inc'
   protocol=service_sources[0].parent/protocol_name
   assert str(protocol) in alarm_service_record['dependencies_sha256']
   protocol_text=protocol.read_text()
   begin_decl='static int32_t edit_begin(void*c,uint32_t requested,alarm_clock_edit_permit_v2*out){'
   begin_next='static int32_t edit_finish(void*c,const alarm_clock_edit_permit_v2*p){'
   assert protocol_text.count(begin_decl)==1 and protocol_text.count(begin_next)==1
   protocol_text=protocol_text.replace(begin_decl,'static int32_t badge_source_edit_begin(void*c,uint32_t requested,alarm_clock_edit_permit_v2*out){')
   protocol_text=protocol_text.replace(begin_next,'extern void badge_clock_edit_begin_result(int);\nstatic int32_t edit_begin(void*c,uint32_t requested,alarm_clock_edit_permit_v2*out){int32_t rc=badge_source_edit_begin(c,requested,out);if(rc==ALARM_OK)badge_clock_edit_begin_result(rc);return rc;}\n'+begin_next)
   protocol_overlay=out/'observed-clock-edit-protocol.inc';protocol_overlay.write_text(protocol_text)
   protocol_include='#include '+json.dumps(protocol_name)
   assert service_text.count(protocol_include)==1
   service_text=service_text.replace(protocol_include,'#include '+json.dumps(str(protocol_overlay)))
   service_overlay=out/'alarm-service-ticket-fault.c';service_overlay.write_text(service_text)
   run(['cc','-std=c11',*common,*san,'-fPIC','-shared','-fvisibility=hidden',*service_defines,*service_inc,'-I'+str(service_sources[0].parent),service_overlay,*service_sources[1:],'-o',candidates[0]/'driver.elf'])
  host_mapper=stage_runtime_mapper(N,out/'host-mapper')
  sources[str(ROOT/'scripts/badge_host_mapper.py')]=sha(ROOT/'scripts/badge_host_mapper.py')
  sources[str(host_mapper/'esp_dlfcn.h')]=sha(host_mapper/'esp_dlfcn.h')
  host_mapper_receipt=json.loads((host_mapper/'staging.json').read_text())
  runtime_sources=[N/x for x in ['src/bootstrap/Json.cpp','src/bootstrap/Board.cpp','src/bootstrap/Runtime.cpp','src/runtime/drivers/ProviderGraphV2.cpp','src/runtime/drivers/ProviderModuleV2.cpp','src/ports/esp32s3/CpuPort.cpp','src/runtime/streams/AppStreamSessions.cpp','src/runtime/streams/ProviderQueueHost.cpp','src/runtime/storage/AppDataFiles.cpp']]
  exe=out/'execute';run(['c++','-std=c++17',*common,*(['-O0'] if a.sanitize else []),*san,'-rdynamic','-DRISC_STAGE_LOGS=1','-DRISC_APP_REQUIREMENT_ROWS=24',*(['-DBADGE_POINTS_EXECUTION'] if points_mode else []),*(['-DBADGE_QUICK_ACTIONS','-DRISC_RUNTIME_PREFERENCE_STORE=1','-DRISC_RUNTIME_ATOMIC_KEY_VALUE=0'] if quick_actions else []),*(['-DBADGE_WIFI_NATIVE_GEOMETRY'] if native_wifi_geometry else []),*(['-DBADGE_ALARM_NATIVE_GEOMETRY'] if native_alarm_geometry else []),*(['-DBADGE_NATIVE_ALARMS','-DALARM_NATIVE_UTC','-I'+str(U/'lib/Alarm/include')] if alarms_enabled else []),'-I'+str(host_mapper),*['-I'+str(N/x) for x in ['src','sdk/app','sdk/driver','sdk/hardware','lib/ArduinoJson/src','test','test/drivers/stubs','test/realtime_shim','test/retained_wake_shim']],*runtime_sources,ROOT/'tests/badge_timecard_execution.cpp','-ldl',*(['-no-pie'] if a.sanitize else []),'-o',exe])
  scenarios=['save','cold-load','light-draft','alarm-draft','quick-shared','quick-retained']
  if points_mode:
   from badge_points_execution import SCENARIOS
   scenarios=SCENARIOS
  if a.scenario:
   assert len(a.scenario)==len(set(a.scenario)) and set(a.scenario)<=set(scenarios),'Unknown/duplicate selected scenario'
   scenarios=a.scenario
  for scenario in scenarios:
   env={**os.environ,**launcher_env,'TIMECARD_SCENARIO':scenario,'ASAN_OPTIONS':'detect_leaks=0:halt_on_error=1','UBSAN_OPTIONS':'halt_on_error=1','BADGE_SETTINGS_NATIVE_GEOMETRY_EXPECTED':str(int(settings_native_geometry)),'BADGE_PROTOTYPE_RASTERS':str(rasters.resolve()),'BADGE_WATCH_SPRINGBOARD':'1','BADGE_PROTOTYPE_NATIVE_SAFE_CONTENT':','.join(native_geometry_names),'BADGE_PROTOTYPE_SAFE_VIEWPORT':','.join(name for name,(_,defs,_) in source_app.items() if '-DPORTABLE_SAFE_VIEWPORT' in defs)}
   r=run([exe,host,'0'],text=True,capture_output=True,timeout=300,env=env,check_result=False)
   a.output.with_name(a.output.stem+'-'+scenario+'.log').write_text(r.stdout+'\nSTDERR\n'+r.stderr)
   r.check_returncode()
   assert ('PASS actual Runtime Points' if points_mode else 'PASS actual Runtime Timecard') in r.stdout,(scenario,r.stdout,r.stderr)
   results.append({'target_index':0,'scenario':scenario,'returncode':r.returncode,'stdout':r.stdout,'stderr':r.stderr});print(scenario,r.returncode,flush=True)
   if a.compress_rasters:compress_rasters(rasters)
   if scenario!='save' and (host/'host-appdata').exists():shutil.rmtree(host/'host-appdata')
  for path,digest in sources.items():assert sha(path)==digest,'Input changed: '+path
 a.output.parent.mkdir(parents=True,exist_ok=True)
 a.output.write_text(json.dumps({'schema':1,'qualification':'badge-points' if points_mode else 'badge-timecard','selected_application':a.app,'host_compiled_apps':sorted(exercised_apps),'points_recipe':records.get('points_in_time'),'alarm_service_recipe':alarm_service_record,'launcher_catalog':launcher_catalog,'launcher_catalog_path':str(catalog_path),'launcher_catalog_sha256':sha(catalog_path),'passed':True,'sanitize':a.sanitize,'foreground':a.foreground,'include_timecard':True,'timecard_recipe':records['timecard'],'foreground_recipes':foreground_recipes,'source_pins':lock,'store_files':record['store_files'],'target_build_sha256':sha(target/'build.json'),'driver_revision':None if points_mode else subprocess.check_output(['git','-C',str(ROOT),'rev-parse','HEAD'],text=True).strip(),'driver_source_identity':'captured source_sha256; local snapshot, not a fabricated commit' if points_mode else 'git HEAD plus source_sha256','fixture_sha256':{str(f.relative_to(ROOT)):sha(f) for f in [Path(__file__).resolve(),ROOT/'tests/badge_timecard_execution.cpp',ROOT/'tests/badge_timecard_execution.h',ROOT/'tests/badge_points_execution.h',ROOT/'tests/badge_points_execution.inc',ROOT/'tests/badge_timecard_execution.inc',ROOT/'tests/badge_alarm_execution.inc',ROOT/'tests/badge_quick_execution.inc',ROOT/'tests/badge_wifi_execution.inc']},'host_preference_store':1,'host_atomic_key_value':0,'native_alarms':alarms_enabled,'quick_actions':quick_actions,'native_wifi_geometry':native_wifi_geometry,'case_count':len(results),'host_recompilation':True,'host_mapper_staging':host_mapper_receipt,'source_sha256':sources,'target_instruction_execution':False,'physical_tested':False,'modeled':['Actual Runtime graph and selected app/provider sources with host instruction mapping','SDK hardware/time/input and POSIX AppData medium',*(['Read-only Points scene/control maps and launcher centers','Synthetic preexisting Points configuration/time; edits and occurrence ACKs use production app/service'] if points_mode else ['Read-only actual Timecard scene/control/Quick layout and launcher centers','One negative-case provider collect result changed only after actual OK; production client guards unmodified; preference callback entries observed']),'Separate target ELF/admission qualification required'],'commands':commands,'results':results},indent=2)+'\n')
if __name__=='__main__':main()

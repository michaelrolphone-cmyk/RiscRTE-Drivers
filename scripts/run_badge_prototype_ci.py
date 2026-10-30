#!/usr/bin/env python3
"""One offline build/admission/navigation/package cohort; never a device operation."""
import argparse,subprocess,sys,json,os,hashlib,shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
SUCCESSOR_FLAGS=['alarm-ready-drain', 'nova-mask-bounds', 'settings-alpha-geometry', 'raster-overwrite-clear', 'raster-underlay-exchange', 'time-server-sync', 'interaction-trace', 'native-glyph-coverage-cache', 'native-stroke-coverage-cache', 'picker-compositor-plan', 'native-damage-replay', 'native-settings-ui', 'round-settings-ui', 'sliced-native-frames', 'native-performance-trace', 'springboard-quick-actions', 'springboard-gesture-diagnostics']
def run_logged(command,log,env):
 with log.open('w') as stream:
  try:subprocess.run(command,check=True,env=env,stdout=stream,stderr=subprocess.STDOUT)
  except subprocess.CalledProcessError:
   stream.flush()
   # Keep the complete immutable log artifact, and expose bounded diagnostics
   # in the Actions job so a failed child never becomes only an exit status.
   data=log.read_text(errors='replace')
   print('FAILED child '+str(log)+'\n'+data[-24000:],file=sys.stderr,flush=True)
   raise
def execution_args(w,target,output,sanitize=False,foreground=False):
 args=['--runtime',w/'runtime-prototype','--watch',w/'watch','--watch-bringup',w/'watch-prototype','--controllers',w/'controllers','--system',w/'system-prototype','--system-baseline',w/'system','--utilities',w/'utilities-prototype','--utilities-baseline',w/'utilities','--store',target/'store','--output',output]
 return args+(['--sanitize'] if sanitize else [])+(['--foreground'] if foreground else [])
def copy_recovery_receipts(target,out,evidence):
 from hybrid_evidence_gate import qualify_hybrid
 from springboard_evidence_gate import qualify_springboard
 from settings_evidence_gate import qualify_settings
 from wifi_gap_evidence_gate import qualify_wifi_gap
 from picker_evidence_gate import qualify_picker
 built=json.loads((target/'build.json').read_text());digest=hashlib.sha256((target/'build.json').read_bytes()).hexdigest()
 if built.get('include_timecard'):
  from timecard_evidence_gate import qualify_timecard
 for lane,sanitize in [('normal',False),('sanitized',True)]:
  if built.get('springboard_quick_actions'):
   from local_quick_evidence_gate import qualify as qualify_local_quick
   from execution_source_inputs import validated_inputs
   source=out/('local-quick-'+lane+'.json');receipt=json.loads(source.read_text())
   reference=validated_inputs(json.loads((out/'execution-sanitized.json').read_text()))
   qualify_local_quick(built,receipt,sanitize,ROOT,reference)
   shutil.copyfile(source,evidence/source.name)
  for prefix,validator in [('hybrid-short',qualify_hybrid),('springboard',qualify_springboard),('settings-timers',qualify_settings),('picker',qualify_picker),*([('wifi-gap',qualify_wifi_gap)] if built.get('native_wifi_geometry') else []),*([('timecard',qualify_timecard)] if built.get('include_timecard') else [])]:
   source=out/(prefix+'-'+lane+'.json');receipt=json.loads(source.read_text())
   args=[built,receipt,sanitize,built['store_files'],digest]
   if prefix in ('hybrid-short','settings-timers','wifi-gap','timecard','picker'):args.append(ROOT)
   validator(*args);shutil.copyfile(source,evidence/source.name)
def main():
 p=argparse.ArgumentParser(description=__doc__)
 for flag in SUCCESSOR_FLAGS:p.add_argument('--'+flag,action='store_true')
 p.add_argument('--spiffs-source',type=Path)
 p.add_argument('--reuse-columns',action='store_true');p.add_argument('--queued-pixels',action='store_true');p.add_argument('--reuse-unchanged-stripes',action='store_true');p.add_argument('--native-stripe-rows',type=int,choices=(4,8,16,32),default=4)
 p.add_argument('--foreground-idle',action='store_true');p.add_argument('--include-files',action='store_true');p.add_argument('--native-daily',action='store_true');p.add_argument('--native-alarms',action='store_true');p.add_argument('--native-alarm-geometry',action='store_true');p.add_argument('--native-wifi-geometry',action='store_true');p.add_argument('--quick-actions',action='store_true');p.add_argument('--include-timecard',action='store_true');p.add_argument('--workspace',type=Path,required=True);p.add_argument('--idf',type=Path,required=True);p.add_argument('--cc',required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();w=a.workspace.resolve();out=a.output.resolve()
 if a.include_timecard and not a.quick_actions:p.error('include-timecard requires selected Quick cohort')
 if a.native_wifi_geometry and not a.quick_actions:p.error('native-wifi-geometry requires the quick-actions product cohort')
 if a.quick_actions and not a.native_alarms:p.error('quick-actions requires native-alarms')
 if a.native_alarm_geometry and not a.native_alarms:p.error('native-alarm-geometry requires native-alarms')
 if a.native_alarms and not (a.native_daily and a.foreground_idle and a.include_files):p.error('native-alarms requires native-daily, foreground-idle and include-files')
 if a.native_daily and not a.foreground_idle:p.error('native-daily requires foreground-idle')
 if a.include_files and not a.foreground_idle:p.error('include-files requires foreground-idle')
 if out.exists() and any(out.iterdir()):p.error('Use empty output for a new immutable cohort')
 out.mkdir(parents=True,exist_ok=True);env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0','IDF_COMPONENT_MANAGER':'0','PYTHONDONTWRITEBYTECODE':'1'}
 calls=[]
 successor_args=['--'+flag for flag in SUCCESSOR_FLAGS if getattr(a,flag.replace('-','_'))]
 # Validate selections before any build/test output is created by a child.
 import raster_traffic_profile
 raster_traffic_profile.validate(a,p)
 if a.time_server_sync and not (a.native_alarms and a.foreground_idle):p.error('time-server-sync requires native alarms and foreground lifecycle')
 if a.round_settings_ui and not a.native_settings_ui:p.error('round-settings-ui requires native-settings-ui')
 if a.springboard_gesture_diagnostics and not a.native_performance_trace:p.error('springboard-gesture-diagnostics requires native-performance-trace')
 if a.springboard_quick_actions and not (a.quick_actions and a.sliced_native_frames):p.error('springboard-quick-actions requires quick-actions and sliced-native-frames')
 if a.alarm_ready_drain and not (a.native_alarms and a.foreground_idle):p.error('alarm-ready-drain requires native alarms and foreground lifecycle')
 def run(script,*args):
  command=[sys.executable,str(ROOT/'scripts'/script),*map(str,args)];calls.append(command)
  run_logged(command,out/(str(len(calls))+'-'+Path(script).stem+'.log'),env)
 def external(script,*args):
  command=[sys.executable,str(script),*map(str,args)];calls.append(command)
  run_logged(command,out/(str(len(calls))+'-'+Path(script).stem+'.log'),env)
 def shell(script,mode):
  command=['bash',str(script)];calls.append(command)
  run_logged(command,out/(str(len(calls))+'-'+script.stem+'-'+mode+'.log'),{**env,'SANITIZE':'1' if mode=='sanitized' else '0'})
 # Native linking is independently useful evidence even if an owning test later fails.
 # All required tests still run below, and any failure still fails this cohort.
 native=out/'native';target=out/'target'
 run('build_badge_native.py','--prototype','--runtime',w/'runtime-prototype','--idf',a.idf,'--cc',a.cc,'--python',sys.executable,'--output',native,*(['--quick-actions'] if a.quick_actions else []),*(['--time-server-sync'] if a.time_server_sync else []),*(['--performance-trace'] if a.native_performance_trace else []),*(['--spiffs-source',a.spiffs_source] if a.spiffs_source else []),*(['--reuse-columns'] if a.reuse_columns else []),*(['--queued-pixels'] if a.queued_pixels else []),'--native-stripe-rows',a.native_stripe_rows,*(['--reuse-unchanged-stripes'] if a.reuse_unchanged_stripes else []))
 external(ROOT/'tests/test_badge_source_pack.py')
 external(w/'runtime-prototype/test/test_native_appdata_layout.py')
 for mode in ['normal','sanitized']:
  external(w/'runtime-prototype/test/run_native_app_data_geometry_test.py','--output',out/('appdata-geometry-'+mode),*(['--sanitize'] if mode=='sanitized' else []))
 external(w/'system-prototype/scripts/test_native_only_settings.py','--runtime',w/'runtime')
 external(w/'system-prototype/scripts/test_safe_viewport_raster.py')
 external(w/'utilities-prototype/scripts/test_native_standalone_stopwatch.py','--system',w/'system-prototype','--runtime',w/'runtime-prototype')
 external(w/'watch-prototype/scripts/test_bringup_native.py','--output',out/'bringup')
 external(w/'watch-prototype/scripts/test_sleep_defaults.py','--system',w/'system','--utilities',w/'utilities')
 external(w/'watch-prototype/scripts/test_native_time_authority.py','--system',w/'system','--utilities',w/'utilities')
 external(w/'watch-prototype/scripts/test_native_time_default.py','--system',w/'system','--utilities',w/'utilities')
 external(w/'watch-prototype/scripts/test_meridiem_aperture.py')
 external(w/'watch-prototype/scripts/test_meridiem_artwork.py')
 external(w/'watch-prototype/scripts/test_home_native_height.py')
 run('verify_usb_hid_preservation.py')
 external(ROOT/'test/parity/test_cst_inventory_preservation.py')
 run('test_gpio_key_sleep.py','--runtime',w/'runtime-prototype')
 command=['bash',str(ROOT/'test/run_cst9217_touch_test.sh'),str(w/'runtime-prototype')];calls.append(command)
 with (out/(str(len(calls))+'-cst-owning.log')).open('w') as f:subprocess.run(command,check=True,env=env,stdout=f,stderr=subprocess.STDOUT)
 if a.native_daily:external(w/'utilities-prototype/scripts/test_native_safe_content.py','--system',w/'system-prototype','--system-revision',json.loads((ROOT/'boards/waveshare-esp32-s3-touch-amoled-1.75c/prototype/sources.lock.json').read_text())['system']['commit'],'--runtime',w/'runtime-prototype')
 if a.include_files:
  external(ROOT/'tests/test_badge_app_inventory.py')
  external(ROOT/'tests/test_badge_files_evidence_gate.py')
  external(w/'system-prototype/scripts/test_watch_files_profile.py','--runtime',w/'runtime-prototype','--output',out/'files-shared-normal')
  external(w/'system-prototype/scripts/test_watch_files_profile.py','--runtime',w/'runtime-prototype','--output',out/'files-shared-sanitized','--sanitize')
 if a.foreground_idle:
  external(w/'system-prototype/scripts/test_foreground_idle.py','--runtime',w/'runtime-prototype','--output',out/'foreground-shared')
  run('test_foreground_idle_preferences.py','--runtime',w/'runtime-prototype','--system',w/'system-prototype')
  for sanitizer in [None,'address','undefined']:
   run('test_portable_light_sleep.py','--runtime',w/'runtime-prototype','--system',w/'system-prototype',*(['--sanitize',sanitizer] if sanitizer else []))
 if a.native_alarms:
  external(w/'system-prototype/scripts/test_settings_native_alarm_service.py','--runtime',w/'runtime-prototype','--utilities',w/'utilities-prototype','--output',out/'settings-clock-service')
  external(w/'system-prototype/scripts/test_settings_native_alarm.py','--runtime',w/'runtime-prototype','--utilities',w/'utilities-prototype','--output',out/'settings-clock-edit')
  external(w/'system-prototype/scripts/test_compact_ascii_glyphs.py','--output',out/'compact-ascii-glyphs')
  for sanitizer in [None,'address','undefined']:run('test_portable_alarm_light_sleep.py','--runtime',w/'runtime-prototype','--system',w/'system-prototype',*(['--sanitize',sanitizer] if sanitizer else []))
  external(w/'system-prototype/scripts/test_foreground_alarm.py','--runtime',w/'runtime-prototype','--utilities',w/'utilities-prototype','--output',out/'foreground-alarm')
  external(w/'watch-prototype/scripts/test_native_alarm.py','--system',w/'home-system','--utilities',w/'utilities-prototype','--output',out/'home-alarm','--clock-edit-authority',*(['--native-quick-actions'] if a.quick_actions else []))
  revision=json.loads((ROOT/'boards/waveshare-esp32-s3-touch-amoled-1.75c/prototype/sources.lock.json').read_text())['system']['commit']
  for script in ['test_native_schedules_alarm.py','test_badge_native_alarm_apps.py']:
   external(w/'utilities-prototype/scripts'/script,'--runtime',w/'runtime-prototype','--system-apps',w/'system-prototype','--system-revision',revision,'--output',out/script.removesuffix('.py'),'--clock-edit-authority',*(['--foreground-alarm-sleep'] if script=='test_badge_native_alarm_apps.py' else []))
 if a.native_alarm_geometry:
  for script in ['tests_native_alarm_geometry.py','test_native_alarm_adapter.py']:
   external(w/'utilities-prototype/scripts'/script,'--runtime',w/'runtime-prototype','--system-apps',w/'system-prototype','--system-revision',revision,'--output',out/script.removesuffix('.py'),'--clock-edit-authority')
 if a.quick_actions:
  external(ROOT/'tests/test_badge_quick_evidence_gate.py')
  for mode in ['normal','sanitized']:shell(w/'runtime-prototype/test/run_preference_store_runtime_test.sh',mode)
  external(w/'watch-prototype/scripts/test_quick_pending_barrier.py','--system',w/'system-prototype','--output',out/'quick-pending-barrier')
  external(w/'system-prototype/scripts/test_quick_safe_content.py','--output',out/'quick-safe-content')
  external(w/'watch-prototype/scripts/test_quick_capability_radios.py','--system',w/'system-prototype','--output',out/'quick-radios')
  external(w/'watch-prototype/scripts/test_generic_preferences.py','--system',w/'system-prototype','--utilities',w/'utilities-prototype','--output',out/'quick-preferences','--native-quick-actions')
 if a.native_wifi_geometry:
  external(ROOT/'tests/test_badge_wifi_evidence_gate.py')
  external(w/'system-prototype/scripts/test_wifi_native.py','--output',out/'wifi-native')
 if a.include_timecard:
  external(ROOT/'tests/test_badge_timecard_package.py')
  external(ROOT/'tests/test_timecard_evidence_gate.py')
  external(ROOT/'test/test_timecard_product_selection.py')
  external(w/'scene-qualification-tools/scripts/test_native_scene_provenance.py')
  common=['--system-apps',w/'system-prototype','--runtime',w/'runtime-prototype']
  external(w/'productivity-prototype/scripts/test_timecard_current_build.py',*common,'--utilities',w/'utilities-prototype','--drivers',ROOT)
  for mode in ['normal','sanitized']:
   san=['--sanitize'] if mode=='sanitized' else []
   external(w/'system-prototype/scripts/test_quick_pending_frame.py','--output',out/('quick-pending-helper-'+mode),*san)
   external(w/'system-prototype/scripts/test_quick_pending_input.py','--runtime',w/'runtime-prototype','--alarm-sdk',w/'timecard-alarm-sdk/lib/Alarm/include','--output',out/('quick-pending-input-'+mode),*san)
   external(w/'scene-qualification-tools/scripts/test_native_scene.py','--runtime',w/'runtime-prototype','--alarm-sdk',w/'timecard-alarm-sdk/lib/Alarm/include','--system-under-test',w/'system-prototype','--system-revision',json.loads((ROOT/'boards/waveshare-esp32-s3-touch-amoled-1.75c/prototype/sources.lock.json').read_text())['system']['commit'],'--parity-source',w/'timecard-system-baseline','--baseline',w/'timecard-pre-scene-baseline','--output',out/('timecard-scene-'+mode),*san)
   external(w/'productivity-prototype/scripts/test_timecard_current_contract.py',*common,'--alarm-sdk',w/'timecard-alarm-sdk/lib/Alarm/include','--output',out/('timecard-contract-'+mode),*san)
   external(w/'productivity-prototype/scripts/test_timecard_current_ui.py',*common,'--utilities',w/'utilities-prototype','--drivers',ROOT,'--output',out/('timecard-ui-'+mode),*san)
  external(w/'productivity-prototype/scripts/test_timecard_current_legacy.py','--baseline',w/'timecard-productivity-baseline','--system-apps',w/'system-prototype','--sdk',out/'timecard-contract-normal/include','--output',out/'timecard-legacy.json')
 for mode in ['normal','sanitized']:
  san=['--sanitize'] if mode=='sanitized' else []
  external(w/'watch-prototype/scripts/test_native_alpha_coordinate.py','--output',out/('alpha-coordinate-'+mode+'.json'),*san)
  external(w/'watch-prototype/scripts/test_native_alpha_parity.py','--baseline',w/'watch-alpha-baseline','--output',out/('alpha-parity-'+mode),*san)
 if a.time_server_sync:
  external(ROOT/'test/test_time_sync_profile.py')
  external(w/'system-prototype/scripts/test_time_server_sync.py','--runtime',w/'runtime-prototype','--utilities',w/'utilities-prototype','--output',out/'time-sync-owning')
 run('build_badge_prototype.py','--runtime',w/'runtime-prototype','--runtime-baseline',w/'runtime','--watch',w/'watch','--watch-bringup',w/'watch-prototype','--controllers',w/'controllers','--system',w/'system-prototype','--system-baseline',w/'system','--utilities',w/'utilities-prototype','--utilities-baseline',w/'utilities','--cc',a.cc,'--output',target,*(['--home-system',w/'home-system'] if a.native_alarms else []),*(['--foreground-idle'] if a.foreground_idle else []),*(['--include-files'] if a.include_files else []),*(['--native-daily'] if a.native_daily else []),*(['--native-alarms'] if a.native_alarms else []),*(['--native-alarm-geometry'] if a.native_alarm_geometry else []),*(['--quick-actions'] if a.quick_actions else []),*(['--native-wifi-geometry'] if a.native_wifi_geometry else []),*(['--include-timecard','--productivity',w/'productivity-prototype'] if a.include_timecard else []),*successor_args)
 for mode in ['normal','sanitized']:
  san=['--sanitize'] if mode=='sanitized' else []
  external(w/'runtime-prototype/test/run_native_appdata_sleep_guard_test.py',*san)
  run('test_badge_prototype_services.py','--runtime',w/'runtime-prototype',*san)
  run('validate_badge_home.py','--prototype','--runtime',w/'runtime-prototype','--controllers',w/'controllers','--store',target/'store','--native-elf',native/'build/riscrte_native.elf','--output',out/('admission-'+mode),*san)
  run('test_badge_prototype_execution.py','--runtime',w/'runtime-prototype','--watch',w/'watch','--watch-bringup',w/'watch-prototype','--controllers',w/'controllers','--system',w/'system-prototype','--system-baseline',w/'system','--utilities',w/'utilities-prototype','--utilities-baseline',w/'utilities','--store',target/'store','--output',out/('execution-'+mode+'.json'),*san,'--compress-rasters',*(['--foreground'] if a.foreground_idle else []))
  if a.springboard_quick_actions:
   run('qualification_fixture.py','--profile','local-quick','--source',ROOT,'--source-revision',subprocess.check_output(['git','-C',ROOT,'rev-parse','HEAD'],text=True).strip(),'--output',out/('local-quick-fixture-'+mode),'--run','--',*execution_args(w,target,out/('local-quick-'+mode+'.json'),mode=='sanitized',a.foreground_idle),'--compress-rasters')
  if a.native_alarms:
   run('test_badge_hybrid_execution.py',*execution_args(w,target,out/('hybrid-short-'+mode+'.json'),mode=='sanitized',a.foreground_idle),'--short-prepare')
   run('test_badge_springboard_execution.py',*execution_args(w,target,out/('springboard-'+mode+'.json'),mode=='sanitized',a.foreground_idle))
   run('test_badge_picker_execution.py',*execution_args(w,target,out/('picker-'+mode+'.json'),mode=='sanitized',a.foreground_idle))
   run('test_badge_settings_execution.py',*execution_args(w,target,out/('settings-timers-'+mode+'.json'),mode=='sanitized',a.foreground_idle),'--settings-ui')
   if a.native_wifi_geometry:run('test_badge_wifi_gap_execution.py',*execution_args(w,target,out/('wifi-gap-'+mode+'.json'),mode=='sanitized',a.foreground_idle))
   if a.include_timecard:run('test_badge_timecard_execution.py',*execution_args(w,target,out/('timecard-'+mode+'.json'),mode=='sanitized',a.foreground_idle),'--productivity',w/'productivity-prototype')
 if a.time_server_sync:
  for mode in ['normal','sanitized']:
   external(ROOT/'test/test_time_sync_package.py','--target',target,'--native',native,'--execution',out/('execution-'+mode+'.json'))
 revision=subprocess.check_output(['git','-C',ROOT,'rev-parse','HEAD'],text=True).strip()
 external(ROOT/'tests/test_badge_prototype_package.py','--revision',revision,'--native',native,'--target',target,'--normal',out/'admission-normal/admission.json','--sanitized',out/'admission-sanitized/admission.json')
 if a.native_alarms:
  external(ROOT/'test/test_hybrid_evidence_gate.py')
  external(ROOT/'test/test_springboard_evidence_gate.py')
  external(ROOT/'test/test_settings_evidence_gate.py')
  external(ROOT/'test/test_picker_evidence_gate.py')
  if a.native_wifi_geometry:external(ROOT/'test/test_wifi_gap_evidence_gate.py')
 evidence=out/'software-evidence';evidence.mkdir()
 if a.native_alarms:copy_recovery_receipts(target,out,evidence)
 checks=[]
 for log in sorted(out.glob('*.log')):
  shutil.copyfile(log,evidence/log.name);checks.append({'name':log.stem,'result':'passed','log':log.name,'sha256':hashlib.sha256(log.read_bytes()).hexdigest()})
 (evidence/'commands.json').write_text(json.dumps(calls,indent=2)+'\n')
 (evidence/'summary.json').write_text(json.dumps({'hardware_tested':False,'source_pins':json.loads((target/'build.json').read_text())['source_pins'],'checks':checks,'ci':({'run_url':os.environ.get('GITHUB_SERVER_URL','https://github.com')+'/'+os.environ.get('GITHUB_REPOSITORY','michaelrolphone-cmyk/RiscRTE-Drivers')+'/actions/runs/'+os.environ['GITHUB_RUN_ID'],'status':'current run; inspect live conclusion'} if os.environ.get('GITHUB_RUN_ID') else {'run_url':None,'status':'local offline checks passed; no hosted CI run claimed'})},indent=2)+'\n')
 run('package_badge_prototype.py','--drivers-revision',revision,'--software-evidence',evidence,'--runtime',w/'runtime-prototype','--idf',a.idf,'--native',native,'--target',target,'--admission-normal',out/'admission-normal/admission.json','--admission-sanitized',out/'admission-sanitized/admission.json','--execution-normal',out/'execution-normal.json','--execution-sanitized',out/'execution-sanitized.json','--output',out/'badge-round-prototype')
 run('verify_badge_prototype_image.py','--package',out/'badge-round-prototype','--output',out/'image-verification.json')
 for mode in ['normal','sanitized']:
  external(w/'runtime-prototype/test/run_native_app_data_geometry_test.py','--appdata-image',out/'badge-round-prototype/appdata.bin','--output',out/('packaged-geometry-'+mode),*(['--sanitize'] if mode=='sanitized' else []))
 (out/'commands.json').write_text(json.dumps(calls,indent=2)+'\n')
if __name__=='__main__':main()

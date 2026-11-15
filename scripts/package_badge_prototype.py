#!/usr/bin/env python3
"""Seal one exact locally qualified prototype cohort. Never accesses a device."""
from execution_source_inputs import validated_inputs
from execution_case_inventory import case_ids
import time_sync_evidence_gate
import argparse,hashlib,importlib.util,json,shutil,struct,subprocess,sys,tempfile
from pathlib import Path
from hybrid_evidence_gate import qualify_hybrid
from springboard_evidence_gate import qualify_springboard
from settings_evidence_gate import qualify_settings
from wifi_gap_evidence_gate import qualify_wifi_gap
from picker_evidence_gate import qualify_picker
import local_quick_evidence_gate as local_quick
from execution_source_equivalence import verify as verify_execution_sources
from package_badge_flash import require,sha,record,inventory,git,IDF,FLASH,BOOTFS
ROOT=Path(__file__).resolve().parents[1];PROFILE=ROOT/'boards/waveshare-esp32-s3-touch-amoled-1.75c/prototype'
def read(p):return json.loads(p.read_text())
def write(p,j):p.write_text(json.dumps(j,indent=2)+'\n')
def qualify(native,target,normal,sanitized,revision,target_root=None,target_revision=None):
 require(len(revision)==40 and git(ROOT,'rev-parse','HEAD')==revision and not git(ROOT,'status','--porcelain'),'Clean exact Drivers source required')
 proof=read(native/'build-proof.json');store=target/'store';built=read(target/'build.json');lock=read(PROFILE/'sources.lock.json');cfg=read(native/'build/config/sdkconfig.json')
 require(proof['full_link'] and proof['idf_revision']==IDF and proof['runtime_revision']==lock['runtime']['commit'],'Native source cohort mismatch')
 require(built['source_pins']==lock and built['profile']=='round-prototype','Store source cohort mismatch')
 if proof.get('spiffs_lookup',{}).get('enabled'):
  import stage_spiffs_lookup
  require(stage_spiffs_lookup.verify_compiled(native/'build',proof['spiffs_lookup'])==proof['spiffs_lookup']['compiled_units'],'SPIFFS modified source compile proof mismatch')
 for n,e in proof['artifacts'].items():require(record(native/'build'/n)==e,'Native artifact mismatch: '+n)
 for n,digest in proof['sources_sha256'].items():require(sha(ROOT/n)==digest,'Native source changed: '+n)
 from target_source_provenance import verify as verify_target_sources
 verify_target_sources(target_root or ROOT,target_revision or revision,built,PROFILE.relative_to(ROOT))
 files=inventory(store);require(files==built['store_files'],'Store inventory mismatch')
 time_sync_evidence_gate.qualify_product(built,proof,read(target/'wifi/wifi_settings-build-record.json'),read(store/'wifi_settings.json'),read(store/'boot.json'),(ROOT/'sdk/driver/RiscTimeSampleV1.h').read_bytes() if time_sync_evidence_gate.selected(built) else b'')
 require(sha(store/'default.elf')==built['home']['sha256']==sha(target/'shared-home/default.elf'),'Shared Home bytes changed')
 qualify_quick_native(built,proof,read(target/'shared-home/build.json'),read(store/'boot.json'),
                      {n:read(store/n) for n in files if n.endswith('.json')},
                      read(target/'springboard/springboard-build-record.json') if local_quick.selection(built) else None)
 for receipt,mode in [(normal,False),(sanitized,True)]:
  r=read(receipt);qualify_quick_admission(built,r);require(r['real_runtime_graph_admission'] and r['target_images_checked'] and r['sanitize']==mode,'Missing complete admission')
  require(r['store_files']==files and r['native_elf_sha256']==sha(native/'build/riscrte_native.elf'),'Mixed native/store admission')
  require(r['positive']['elf_count']==sum(n.endswith('.elf') for n in files) and len(r['negative_cases'])>=15,'Incomplete image/mutation admission')
  if built.get('clock_edit_authority'):require('insufficient-bound-kv-capacity' in r['negative_cases'],'Missing editor KV capacity refusal proof')
 require(all(cfg[k]==v for k,v in {'ESPTOOLPY_FLASHSIZE':'32MB','SPIFFS_PAGE_SIZE':512,'SPIFFS_OBJ_NAME_LEN':32,'SPIFFS_META_LENGTH':4,'SPIFFS_USE_MAGIC':True,'SPIFFS_USE_MAGIC_LENGTH':True}.items()),'Wrong native geometry')
 raw=(native/'build/partition_table/partition-table.bin').read_bytes();rows=[];md5=False
 for at in range(0,len(raw),32):
  item=raw[at:at+32]
  if item[:2]==b'\xeb\xeb':require(item[16:]==hashlib.md5(raw[:at]).digest(),'Partition MD5 mismatch');md5=True;break
  require(item[:2]==b'\xaa\x50','Bad partition magic');_,t,s,o,n,label,f=struct.unpack('<HBBII16sI',item);rows.append((label.rstrip(b'\0').decode(),t,s,o,n,f))
 require(md5 and rows==[('nvs',1,2,0x9000,0x6000,0),('factory',0,0,0x10000,0x300000,0),('bootfs',1,0x82,BOOTFS,0x1bf0000,0),('appdata',1,0x41,0x1f00000,0x100000,0)],'Wrong full image layout')
 for n in ['riscrte_native.bin','bootloader/bootloader.bin']:
  b=(native/'build'/n).read_bytes();require(len(b)>24 and b[0]==0xe9 and b[2]==2 and b[3]==0x5f,'Unexpected SDK-emitted DIO/80MHz/32MB image header: '+n)
  info=subprocess.check_output([sys.executable,'-m','esptool','--chip','esp32s3','image-info',str(native/'build'/n)],text=True)
  require(any(line.startswith('Checksum:') and '(valid)' in line for line in info.splitlines()),'Invalid image checksum: '+n)
  require(any(line.startswith('Validation hash:') and '(valid)' in line for line in info.splitlines()),'Invalid image validation hash: '+n)
 return proof,built,cfg,files

from alarm_geometry_profile import ALARM_NATIVE_FLAGS,qualify_alarm_geometry_recipe
from wifi_geometry_profile import qualify_wifi_geometry_recipe
ALARM_PROFILE='native-utc-schedules-clock-edit'
ALARM_MEMBERS={'alarm-service/manifest.json','alarm-service/driver.elf',
               'alarms.json','alarms.elf','countdown.json','countdown.elf'}
ALARM_FOREGROUND_APPS={'settings','springboard','wifi_settings','file_browser',
                       'calculator','stopwatch','battery','alarms','countdown'}

QUICK_FLAGS={'-DPORTABLE_QUICK_CAPABILITY_SAFE','-DPORTABLE_QUICK_SAFE_CONTENT'}
QUICK_PREFERENCE_FLAGS={'-DPORTABLE_PREFERENCE_STORE','-DWATCH_ATOMIC_PREFERENCES','-DPORTABLE_QUICK_ATOMIC_PREFERENCES'}
def quick_selection(built):
 quick=built.get('quick_actions',False)
 require(type(quick) is bool,'QuickActions selection must be boolean')
 require(not quick or built.get('native_alarms') is True and built.get('clock_edit_authority') is True,
         'QuickActions requires native alarms and editor authority')
 return quick

def qualify_quick_admission(built,receipt):
 quick=quick_selection(built);selected=receipt.get('quick_actions',False)
 require(type(selected) is bool and selected==quick,'Admission QuickActions selection mismatch')
 if quick:
  require({'missing-preference-broker','missing-preference-grant','unsupported-preference-api'}.issubset(receipt.get('negative_cases',[])),
          'Missing QuickActions admission refusal evidence')
 if timecard_selection(built):
  require({'timecard-missing-appdata','timecard-zero-private-namespace','timecard-private-namespace-alias','timecard-unsupported-appdata-api','timecard-missing-preference-grant'}.issubset(receipt.get('negative_cases',[])),'Missing Timecard admission refusal evidence')

def qualify_quick_home(quick,selected,recipe,home_sha):
 require(type(selected) is bool and selected==quick,'Home QuickActions selection mismatch')
 flags=recipe.get('defines',[])
 require(isinstance(flags,list),'Malformed Home flags')
 require(all((flag in flags)==quick for flag in QUICK_FLAGS),'Home QuickActions flags mismatch')
 if quick:
  require(QUICK_PREFERENCE_FLAGS.issubset(flags),'Home lost strong copied preference client')
  require(recipe.get('compile_inputs',{}).get('definitions')==[f for f in flags if f.startswith('-D')],
          'Home exact compile definitions mismatch')
  require(recipe.get('sha256')==home_sha,'Home execution artifact mismatch')

TIMECARD_QUICK_FLAGS={'-DPORTABLE_QUICK_PENDING_FRAME','-DPORTABLE_QUICK_ACTIONS','-DPORTABLE_QUICK_NATIVE_GEOMETRY','-DPORTABLE_QUICK_SAFE_CONTENT','-DPORTABLE_QUICK_CAPABILITY_SAFE','-DPORTABLE_QUICK_ATOMIC_PREFERENCES','-DPORTABLE_PREFERENCE_STORE','-DPORTABLE_NATIVE_SCENE','-DTIMECARD_CURRENT_PROFILE'}
def timecard_selection(built):
 selected=built.get('include_timecard',False)
 require(type(selected) is bool,'Timecard selection must be boolean')
 members={'timecard.elf','timecard.json'}.intersection(built.get('store_files',{}))
 require(members==({'timecard.elf','timecard.json'} if selected else set()),'Timecard selection/store mismatch')
 require(not selected or quick_selection(built) and built.get('foreground_idle',{}).get('enabled') is True,'Timecard requires existing Quick and returning foreground cohort')
 return selected

def qualify_timecard_profile(built,manifest,grants):
 require(timecard_selection(built),'Unselected Timecard authority')
 r=built.get('timecard_profile',{});profile=r.get('profile',{})
 require(profile.get('profile')=='timecard-current-badge' and profile.get('quick_actions_selected') is True and profile.get('snapshot_selected') is False,'Wrong Timecard profile')
 require(profile.get('quick_preference_store',{}).get('completed_pending_frame_before_submit') is True,'Timecard pending-frame policy missing')
 require(r.get('manifest')==manifest and manifest.get('id')=='timecard' and manifest.get('file_name')=='timecard.elf','Timecard owning manifest mismatch')
 flags=r.get('build_defines',[])
 require(TIMECARD_QUICK_FLAGS.issubset(flags) and '-DPORTABLE_QUICK_RADIOS' not in flags,'Timecard lost exact Quick composition')
 require(r.get('compile_inputs',{}).get('definitions')==flags,'Timecard exact compile definitions mismatch')
 require(r.get('foreground_idle',{}).get('enabled') is True and r.get('clock_edit_authority') is False,'Timecard returning sleep/authority mismatch')
 for name in ['productivity','system','runtime','utilities']:
  identity=r.get('source_repositories',{}).get(name,{})
  require(identity.get('commit')==built.get('source_pins',{}).get(name,{}).get('commit') and identity.get('dirty') is False and isinstance(identity.get('commit'),str) and len(identity['commit'])==40,'Timecard exact source identity mismatch: '+name)
 expected={'storage.key-value':[1],'storage.app-data':[1],'storage.preferences':[1]}
 require(r.get('grant_bindings')==expected and all(type(i) is int for values in r['grant_bindings'].values() for i in values),'Timecard recipe private/preference namespaces mismatch')
 for cap in expected:
  declarations=[d for d in manifest.get('requires',[]) if d.get('capability')==cap]
  require(declarations==[{'capability':cap,'api':1}] and type(declarations[0]['api']) is int,'Timecard mandatory declaration mismatch: '+cap)
  require(not any(d.get('capability')==cap for d in manifest.get('optional',[])),'Timecard private/preference dependency cannot be optional')
  actual=[g for g in grants if g.get('capability')==cap]
  require(actual==[{'capability':cap,'api':1,'instance_id':1}] and type(actual[0]['api']) is int and type(actual[0]['instance_id']) is int,'Timecard installed namespace mismatch: '+cap)
 require(manifest.get('optional')==[{'capability':'board.battery','api':1}],'Timecard optional battery telemetry missing')
 battery=[g for g in grants if g.get('capability')=='board.battery']
 require(battery==[{'capability':'board.battery','api':1,'instance_id':4}] and type(battery[0]['api']) is int and type(battery[0]['instance_id']) is int,'Badge Timecard telemetry grant must preserve selected battery4')
 require(not any(d.get('capability') in ('storage.key-value.atomic','runtime.realtime-control','runtime.realtime.control','net.wifi') for d in manifest.get('requires',[])+manifest.get('optional',[])+grants),'Unexpected Timecard radio/clock/atomic authority')
 return r

def qualify_quick_native(built,proof,home,boot,manifests,springboard_recipe=None):
 quick=quick_selection(built);timecard=timecard_selection(built);native=proof.get('quick_actions',False)
 require(type(native) is bool and native==quick,'Native/store QuickActions mode mismatch')
 pref=proof.get('preference_store',{})
 if quick or pref:
  require(pref.get('enabled') is quick and pref.get('atomic_key_value') is False,
          'QuickActions requires exact synchronous preference broker mode')
 qualify_quick_home(quick,home.get('native_quick_actions',False),home.get('artifacts',{}).get('default',{}),built['home']['sha256'])
 apps=boot.get('app_capabilities',[])
 homes=[a for a in apps if a.get('manifest')=='default.json']
 require(len(homes)==1,'QuickActions requires unique Home grant policy')
 def prefs(items):return [g for g in items if g.get('capability')=='storage.preferences']
 def exact(items,expected):
  return items==[expected] and all(type(items[0].get(k)) is int for k in ('api','instance_id') if k in expected)
 expected={'capability':'storage.preferences','api':1,'instance_id':1}
 springboard=local_quick.selection(built)
 springboard_rows=[a for a in apps if a.get('manifest')=='springboard.json']
 if springboard:
  require(len(springboard_rows)==1 and isinstance(springboard_recipe,dict),'Missing exact Springboard Quick recipe/grants')
  local_quick.profile(built,springboard_recipe,manifests.get('springboard.json',{}),springboard_rows[0].get('grants',[]))
 timecard_rows=[a for a in apps if a.get('manifest')=='timecard.json']
 if timecard:
  require(len(timecard_rows)==1,'Timecard needs one exact app grant policy')
  qualify_timecard_profile(built,manifests.get('timecard.json',{}),timecard_rows[0].get('grants',[]))
 for app in apps:
  grants=prefs(app.get('grants',[]))
  if quick and app.get('manifest')=='default.json':require(exact(grants,expected),'Wrong Home preference authority')
  elif springboard and app.get('manifest')=='springboard.json':require(exact(grants,expected),'Wrong Springboard preference authority')
  elif timecard and app.get('manifest')=='timecard.json':require(exact(grants,expected),'Wrong Timecard preference authority')
  else:require(not grants,'Unselected/foreground preference authority')
 for name,manifest in manifests.items():
  if manifest.get('type')!='application':continue
  required=prefs(manifest.get('requires',[]));optional=prefs(manifest.get('optional',[]))
  if quick and name=='default.json':
   require(exact(required,{'capability':'storage.preferences','api':1}) and not optional,'Home preferences must be mandatory API1')
  elif springboard and name=='springboard.json':require(exact(required,{'capability':'storage.preferences','api':1}) and not optional,'Springboard preferences must be mandatory API1')
  elif timecard and name=='timecard.json':require(exact(required,{'capability':'storage.preferences','api':1}) and not optional,'Timecard preferences must be mandatory API1')
  elif quick:require(not required and not optional,'Unexpected foreground preference declaration')
  elif name=='default.json':require(not required,'Unselected mandatory Home preference dependency')
 if quick:
  require(manifests.get('default.json',{}).get('type')=='application','Missing Home manifest')
  for name in ('default.json','settings.json'):
   rows=[a for a in apps if a.get('manifest')==name]
   require(len(rows)==1,'Missing shared preference owner '+name)
   kv=[g for g in rows[0].get('grants',[]) if g.get('capability')=='storage.key-value']
   require(exact(kv,{'capability':'storage.key-value','api':1,'instance_id':1}),'Raw shared KV1 authority changed: '+name)

def execution_case_count(built):
 quick=quick_selection(built)
 wifi=built.get('native_wifi_geometry',False)
 require(type(wifi) is bool and (not wifi or quick),'Native WiFi requires qualified QuickActions cohort')
 foreground=built.get('foreground_idle',{}).get('enabled',False)
 files='file_browser.elf' in built['store_files']
 require(bool(built.get('supported_app_increment',{}).get('files'))==files,'Files profile/store mismatch')
 require(not files or foreground and 'file_browser' in built['foreground_idle']['apps'],'Files returning idle qualification missing')
 alarms=built.get('native_alarms',False)
 geometry=built.get('native_alarm_geometry',False)
 require(type(geometry) is bool and (not geometry or alarms),'Native alarm geometry requires selected native alarms')
 require(type(alarms) is bool,'Native alarms selection must be boolean')
 members=ALARM_MEMBERS.intersection(built['store_files'])
 if alarms:
  require(files and foreground and built.get('native_daily') is True,'Native alarms require Files, native daily and foreground cohort')
  require(built.get('clock_edit_authority') is True,'Native alarm package requires explicit clock-edit authority')
  require(built.get('alarm_profile')==ALARM_PROFILE,'Wrong native alarm profile')
  apps=built['foreground_idle'].get('apps',[])
  expected_apps=ALARM_FOREGROUND_APPS|({'timecard'} if timecard_selection(built) else set())
  require(len(apps)==len(expected_apps) and set(apps)==expected_apps,'Native alarms require exact selected foreground apps')
  require(members==ALARM_MEMBERS,'Incomplete native alarm service/client store members')
  require(built.get('providers',{}).get('alarm-service',{}).get('profile')==ALARM_PROFILE,'Wrong native alarm provider profile')
  return len(case_ids(built))
 require(not members and built.get('alarm_profile') is None,'Unselected native alarm members/profile')
 return len(case_ids(built))

def qualify_execution_receipt(built,r,mode,files):
 foreground=built.get('foreground_idle',{}).get('enabled',False)
 wifi=built.get('native_wifi_geometry',False)
 require(type(r.get('native_wifi_geometry',False)) is bool and r.get('native_wifi_geometry',False)==wifi,'Execution native WiFi selection mismatch')
 time_sync_evidence_gate.qualify_execution(built,r)
 expected=execution_case_count(built);alarms=built.get('native_alarms',False);quick=quick_selection(built)
 require(type(r.get('quick_actions',False)) is bool and r.get('quick_actions',False)==quick,'Execution QuickActions selection mismatch')
 if quick:
  require(type(r.get('case_count')) is int and all(type(item.get('target_index')) is int for item in r.get('results',[])),'QuickActions execution counts must be integers')
  require(type(r.get('host_preference_store')) is int and r.get('host_preference_store')==1 and type(r.get('host_atomic_key_value')) is int and r.get('host_atomic_key_value')==0,'Execution preference broker mode mismatch')
  qualify_quick_home(quick,r.get('native_quick_actions'),r.get('home_recipe',{}),built['home']['sha256'])
 elif 'home_recipe' in r:
  qualify_quick_home(False,r.get('native_quick_actions',False),r['home_recipe'],built.get('home',{}).get('sha256'))
 if not quick:
  require(type(r.get('host_preference_store',0)) is int and r.get('host_preference_store',0)==0 and type(r.get('host_atomic_key_value',0)) is int and r.get('host_atomic_key_value',0)==0,'Unselected execution preference broker')
 require(r['sanitize']==mode and r['source_pins']==built['source_pins'] and r['passed'] and r['store_files']==files and r.get('foreground',False)==foreground and r.get('native_daily',False)==built.get('native_daily',False) and sorted(x['target_index'] for x in r['results'])==list(range(expected)),'Missing complete navigation, deep-entry and selected foreground cohort evidence')
 require(type(r.get('native_alarms',False)) is bool and r.get('native_alarms',False)==alarms,'Execution native alarms selection mismatch')
 require(type(r.get('native_alarm_geometry',False)) is bool and r.get('native_alarm_geometry',False)==built.get('native_alarm_geometry',False),'Execution native alarm geometry mismatch')
 if alarms:
  require(r.get('clock_edit_authority') is True,'Execution lacks explicit clock-edit authority')
  require(r.get('alarm_profile')==built['alarm_profile'],'Execution native alarm profile mismatch')
  require(r.get('case_count')==expected,'Incomplete native alarm execution count')
  service=r.get('alarm_service_recipe',{})
  require(service.get('profile')==ALARM_PROFILE and service.get('points_selected') is False,'Execution alarm service profile mismatch')
  require(set(r.get('foreground_recipes',{}))==ALARM_FOREGROUND_APPS,'Execution native alarm foreground recipes incomplete')
  if wifi:require(type(r['foreground_recipes']['wifi_settings'].get('compile_inputs')) is dict,'Native WiFi production compile_inputs required')
  qualify_wifi_geometry_recipe(r['foreground_recipes']['wifi_settings'],wifi)
  for name in ('alarms','countdown'):
   qualify_alarm_geometry_recipe(r['foreground_recipes'][name],built.get('native_alarm_geometry',False))
 else:
  require(r.get('alarm_profile') is None,'Execution claims unselected alarm profile')

def write_quick_actions_docs(out,built):
 if not quick_selection(built):return
 (out/'QUICK-ACTIONS.md').write_text('''# Home Quick Actions — software qualification, device checks pending

## Installation and selected authority

This is an explicit native Runtime rebuild, not an app-ELF-only update. Use the
matching native/store image and exact hashes in package.json; follow INSTALL.md
and RECOVERY.md. The combined image overwrites all32MiB, including NVS and user
data. Do not install only Home onto an older native image lacking the broker.
Reproduction requires product --quick-actions and native-build --quick-actions,
plus the existing selected native-alarm/editor prerequisites. Watch selects
--native-quick-actions through its separate pinned source profile.

The existing copied synchronous preference broker is enabled; atomic key-value
mode is NOT enabled. Only Home receives mandatory storage.preferences API1,
namespace1. Its existing raw storage.key-value API1 namespace1 and Settings'
raw namespace1 access remain. No foreground app receives a preference-broker
grant. Values share the existing backend: no namespace migration, automatic
copy/default overwrite, or atomic multi-key publication guarantee is implied.
Raw Settings writes advance broker revision; a stale captured Quick operation
must be refused rather than overwriting a concurrent change.

## Controls and truthful availability

- Wi-Fi is an independently checked policy permission. On does not mean
  associated, connected to the internet, or time-synchronized. Off uses checked
  disconnect. Use the existing Wi-Fi app for network association.
- Missing Bluetooth and Contexts providers remain unavailable. Airplane cannot
  claim every radio Off when a required provider is absent; its tile is disabled
  without both checked radio controls. A preference does not create a provider.
- Torch is a bright screen, not a hardware flashlight. Tap the screen to turn it
  off; saved brightness must be restored.
- Volume/Silent are preferences only in this visual-alarm profile; they do not
  create audio or haptics. DND does not cancel schedules or hide visual alerts.
- The466×466 display reports69-pixel safe insets, giving328×328 content. Shared
  render/hit rectangles use that provider rectangle in full physical coordinates.
  Opening still starts at the full screen's top edge, not the content edge.
  Other dimensions use their declared bounds, not a guessed device shape.

## Preview and pre-write presentation

Brightness/volume drags are labeled PREVIEW - NOT SAVED. Every selected save,
including fast-tap toggles, submits SAVE NOT CONFIRMED and waits for display
PRESENT_COMPLETE before commit-turn preference capture/validation or mutators.
An earlier read-only preview capture keeps its original revision: this barrier
never rebases stale intent. Completion is a software display-protocol result,
not proof that the physical panel visibly showed the frame.

After that completed frame, checked input/power/alert cancellation causes no
preference mutation and restores confirmed values. Failed or unknown pending
presentation causes no preference write and preserves conservative custody;
there is no speculative release or repaint after retained display uncertainty.
Successful acknowledgment clears pending on the next normal completed frame.
A pending caption, including one that remains frozen, is never a saved receipt.

## Manual checks (all physical results pending)

Record exact candidate hash, power source, serial log and first failed transition.
These are checks to perform, not completed device observations.

1. From Home, pull down from the physical top edge. A top-edge tap or rejected
   diagonal must not change a preference; a gesture starting away from the edge
   must not suddenly open Quick. Check all controls fit the round aperture.
2. Drag brightness while keeping contact: it previews. Releasing normally is
   the commit/Save action; there is no separate Save button. Expect a possibly
   brief SAVE NOT CONFIRMED frame before the normal confirmed frame; fast-tap
   toggles use the same barrier. A frozen pending frame is not confirmation.
   Close using the
   bottom handle, reopen, and verify the saved level. A fresh Home invocation
   should reload it without writing defaults.
3. To check cancellation, start a brightness drag and interrupt it with a brief
   PWR press before touch release. The panel closes/cancels before normal Home
   power handling; release the contact. After any sleep/wake, reopen and verify
   the previous committed level, not the interrupted preview. Never hold PWR
   through the PMIC hard-off interval. Contact-loss/multitouch cancellation is
   also modeled by software tests, not established by this manual instruction.
4. Activate Torch, tap to turn it off, and verify saved brightness restoration.
   Close Quick with its handle before the ordinary Home→Apps→Back sequence;
   Back from an app and brief PWR on Springboard return through existing routes.
5. With Quick open, use a brief PWRON/PWR button press (SYS_OUT GPIO3, not BOOT)
   for the existing Home sleep action. Verify the selected Light/Deep behavior
   from diagnostics, then use brief PWR to wake and reopen Quick. No stale touch
   release may commit after wake. Screen darkness alone is not proof of sleep.
6. Use actual Settings fields: date/time, time zone,12/24-hour format and selected
   sleep/idle choices. Return Home and check time formatting and policy remain
   intact across Quick actions. Settings has no brightness editor in this build;
   do not look for one or infer brightness readback from an unrelated field.
7. On an owner-approved network, exercise Wi-Fi policy independently; absent BT
   and Contexts must remain disabled. Test DND with a deliberately created visual
   alarm, dismiss it, and verify no duplicate occurrence. No recurring schedules
   or Points defaults are introduced by Quick.

## Error and evidence boundaries

Ordinary non-retained save errors can display SAVE UNCONFIRMED; that is not
success. Retained unknown results forbid further drawing: the pre-operation
SAVE NOT CONFIRMED frame remains untouched, without a post-retention repaint.
It is not confirmation, even if storage changed before the unknown result.
A modeled error-state renderer preview does not prove that a live
retained-unknown path can paint feedback.
Copied preference unknown/retained outcomes
stop further controller callbacks and unsafe cleanup/sleep; partial-group
publication can exist and must not be silently treated as a committed group.
Radio-provider bool/status failures retain their historical release-then-fail
cleanup; that is distinct from copied preference custody and does not establish
physical peripheral safety after an invalid response.

Source/renderer and modeled-provider tests are not physical validation. The
full42-case normal and sanitized product cohort is a separate required gate,
not a result claimed by this document; inspect included execution receipts for
actual completion. All physical radio, brightness, touch and sleep results
remain pending. Stop on unexpected heating, reset loops or unsafe power behavior
and preserve logs rather than repeatedly retrying an uncertain operation.
''')
 if timecard_selection(built):
  path=out/'QUICK-ACTIONS.md'
  text=path.read_text().replace('Only Home receives mandatory storage.preferences API1,','Home and the explicitly selected current Timecard receive mandatory storage.preferences API1,').replace('No foreground app receives a preference-broker\ngrant.','Timecard alone receives its separately admitted preference-broker grant; other foreground apps do not.')
  path.write_text(text+'\nTimecard preserves its native Quick panel. Wi-Fi Setup navigates to wifi_settings.elf; Timecard has no radio-control grant. Its private AppData1 ledger remains independent of shared preference namespace1. Separate Timecard normal/sanitized Runtime receipts are required.\n')
 if local_quick.selection(built):
  path=out/'QUICK-ACTIONS.md'
  path.write_text('> Explicit temporary Springboard local Quick selection: Springboard additionally receives mandatory copied storage.preferences API1 namespace1. This is app-local ownership, not completed resident architecture. Wi-Fi is setup navigation only; no radio grants. Separate local 34/37/38/40 normal/sanitized receipts are required for Save, retained-result fault, cancelled pending frames/returning Light, and alarm restoration.\n\n'+path.read_text().replace('Only Home receives mandatory storage.preferences API1,','Home and the explicitly selected temporary Springboard receive mandatory storage.preferences API1,').replace('No foreground app receives a preference-broker\ngrant.','Only explicitly selected foreground Quick profiles receive a preference-broker grant.'))
 if built.get('native_wifi_geometry'):
  path=out/'QUICK-ACTIONS.md'
  path.write_text('> Native WiFi successor: this presentation adds no new native kernel capability. Consult the exact native build/reuse provenance for this artifact; reuse is not implied by the UI selection. The full selected execution cohort is46 cases per lane.\n\n'+path.read_text().replace('full42-case','full46-case'))
 for name in ('INSTALL.md','TEST-ON-DEVICE.md','CAPABILITIES.md','REPRODUCE.md'):
  path=out/name
  if path.exists():path.write_text('> Explicit Home Quick successor: read QUICK-ACTIONS.md for matching native broker installation, supported controls and pending physical checks.\n\n'+path.read_text())

def package(a):
 out=a.output.resolve();require(not out.exists() or not any(out.iterdir()),'Choose an empty output')
 native,target,idf,runtime=[getattr(a,n).resolve() for n in ['native','target','idf','runtime']]
 require(git(idf,'rev-parse','HEAD')==IDF and not git(idf,'status','--porcelain'),'Clean pinned IDF required')
 require(not any(line.startswith(('-','+','U')) for line in git(idf,'submodule','status','--recursive').splitlines()),'Unpinned IDF submodule')
 proof,built,cfg,files=qualify(native,target,a.admission_normal,a.admission_sanitized,a.drivers_revision,a.target_driver_source,a.target_driver_revision)
 if proof.get('performance_trace',{}).get('enabled'):
  from build_badge_native import verify_performance_selection
  verify_performance_selection(read(native/'build/compile_commands.json'),runtime,True)
 require(git(runtime,'rev-parse','HEAD')==proof['runtime_revision'] and not git(runtime,'status','--porcelain'),'Clean exact Runtime required')
 for f,mode in [(a.execution_normal,False),(a.execution_sanitized,True)]:
  r=read(f);qualify_execution_receipt(built,r,mode,files)
  equivalence=a.software_evidence/('execution-'+('sanitized' if mode else 'normal')+'-source-equivalence.json')
  verify_execution_sources(f,r,read(equivalence) if equivalence.exists() else None)
 evidence=a.software_evidence.resolve();summary=read(evidence/'summary.json')
 if local_quick.selection(built):
  for name,mode in [('local-quick-normal.json',False),('local-quick-sanitized.json',True)]:
   local_quick.qualify(built,read(evidence/name),mode,ROOT,validated_inputs(read(a.execution_sanitized)))
 if built.get('native_alarms'):
  for name,mode in [('hybrid-short-normal.json',False),('hybrid-short-sanitized.json',True)]:
   qualify_hybrid(built,read(evidence/name),mode,files,sha(target/'build.json'),ROOT)
  for name,mode in [('springboard-normal.json',False),('springboard-sanitized.json',True)]:
   qualify_springboard(built,read(evidence/name),mode,files,sha(target/'build.json'))
  for name,mode in [('picker-normal.json',False),('picker-sanitized.json',True)]:
   qualify_picker(built,read(evidence/name),mode,files,sha(target/'build.json'),ROOT)
  for name,mode in [('settings-timers-normal.json',False),('settings-timers-sanitized.json',True)]:
   qualify_settings(built,read(evidence/name),mode,files,sha(target/'build.json'),ROOT)
 if timecard_selection(built):
  from timecard_evidence_gate import qualify_timecard
  for name,mode in [('timecard-normal.json',False),('timecard-sanitized.json',True)]:
   qualify_timecard(built,read(evidence/name),mode,files,sha(target/'build.json'),ROOT)
 if built.get('native_wifi_geometry'):
  for name,mode in [('wifi-gap-normal.json',False),('wifi-gap-sanitized.json',True)]:
   qualify_wifi_gap(built,read(evidence/name),mode,files,sha(target/'build.json'),ROOT)
 require(summary['hardware_tested'] is False and summary['source_pins']==built['source_pins'] and summary['checks'],'Missing software evidence summary')
 for item in summary['checks']:
  require(item['result'] in ['passed','inherited_failure','unobserved'],'Unknown test outcome')
  if item.get('log'):
   require(Path(item['log']).name==item['log'] and sha(evidence/item['log'])==item['sha256'],'Software evidence log mismatch')
 out.mkdir(parents=True,exist_ok=True)
 tool=idf/'components/spiffs/spiffsgen.py';spec=importlib.util.spec_from_file_location('prototype_spiffs',tool);sp=importlib.util.module_from_spec(spec);spec.loader.exec_module(sp)
 config=sp.SpiffsBuildConfig(512,2,4096,2,4,32,2,2,True,True,'little',True,True,False)
 def generate():
  fs=sp.SpiffsFS(0x1bf0000,config)
  for n in files:require(len(('/'+n).encode())<32,'Unrepresentable SPIFFS name');fs.create_file('/'+n,str(target/'store'/n))
  return fs.to_binary()
 image=generate();require(image==generate() and len(image)==0x1bf0000,'Nonreproducible SPIFFS');(out/'bootfs.bin').write_bytes(image)
 # Exact source-owned LittleFS code; verify vendor identity before preparing it.
 pin=runtime/'experimental/native_replica/pinned_littlefs'
 with tempfile.TemporaryDirectory(prefix='badge-appdata-') as d:
  d=Path(d);vendor=d/'vendor';subprocess.run([sys.executable,str(pin/'prepare_idf6.py'),str(vendor)],check=True)
  command=['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-DLFS_NO_DEBUG','-DLFS_NO_WARN','-DLFS_NO_ERROR','-I'+str(vendor/'littlefs'),str(ROOT/'test/badge_appdata_image.c'),str(vendor/'littlefs/lfs.c'),str(vendor/'littlefs/lfs_util.c'),'-o',str(d/'image')];subprocess.run(command,check=True)
  subprocess.run([str(d/'image'),'create',str(out/'appdata.bin')],check=True);subprocess.run([str(d/'image'),'verify',str(out/'appdata.bin')],check=True)
  subprocess.run([str(d/'image'),'create',str(d/'again.bin')],check=True);require((out/'appdata.bin').read_bytes()==(d/'again.bin').read_bytes(),'Nonreproducible AppData image')
  littlefs_sources={str(f.relative_to(vendor)):sha(f) for f in [vendor/'littlefs/lfs.c',vendor/'littlefs/lfs_util.c',vendor/'littlefs/lfs.h',vendor/'littlefs/lfs_util.h']}
 components=[(0,'bootloader.bin',native/'build/bootloader/bootloader.bin',0x8000),(0x8000,'partition-table.bin',native/'build/partition_table/partition-table.bin',0x1000),(0x10000,'riscrte_native.bin',native/'build/riscrte_native.bin',0x300000),(BOOTFS,'bootfs.bin',out/'bootfs.bin',0x1bf0000),(0x1f00000,'appdata.bin',out/'appdata.bin',0x100000)]
 combined=bytearray(b'\xff'*FLASH);regions=[]
 for offset,n,source,capacity in components:
  b=source.read_bytes();require(len(b)<=capacity,'Partition overflow: '+n)
  if source!=out/n:shutil.copyfile(source,out/n)
  combined[offset:offset+len(b)]=b;regions.append({'offset':offset,'capacity':capacity,'file':n,**record(out/n)})
 require(combined[0x9000:0xf000]==b'\xff'*0x6000,'NVS must be explicitly blank')
 (out/'badge-prototype-combined.bin').write_bytes(combined);shutil.copytree(target/'store',out/'store');shutil.copyfile(native/'build/config/sdkconfig.json',out/'sdkconfig.json');shutil.copyfile(native/'build/flasher_args.json',out/'flasher_args.json')
 for n,f in [('native-proof.json',native/'build-proof.json'),('store-proof.json',target/'build.json'),('admission-normal.json',a.admission_normal),('admission-sanitized.json',a.admission_sanitized),('execution-normal.json',a.execution_normal),('execution-sanitized.json',a.execution_sanitized)]:shutil.copyfile(f,out/n)
 from target_source_provenance import verify as verify_target_sources
 target_provenance=verify_target_sources(a.target_driver_source or ROOT,a.target_driver_revision or a.drivers_revision,built,PROFILE.relative_to(ROOT))
 receipt={'target_owning_source':target_provenance,'schema':1,'profile':'software-qualified-prototype-device-pending','source_head':a.drivers_revision,'source_tree':git(ROOT,'rev-parse','HEAD^{tree}'),'source_pins':built['source_pins'],'shared_home_pins':built['shared_home_pins'],'native_proof':record(out/'native-proof.json'),'store_files':files,'flash_bytes':FLASH,'regions':regions,'combined':record(out/'badge-prototype-combined.bin'),'spiffsgen_sha256':sha(tool),'littlefs_sources':littlefs_sources,'byte_reproducible_filesystems':True,'hardware_flash_performed':False,'physical_boot_verified':False,'full_image_consequence':'Overwrites all32MiB including NVS, firmware and all user data; no erase-first or security overrides.'}
 write(out/'package.json',receipt)
 subprocess.run([sys.executable,str(ROOT/'scripts/verify_badge_spiffs.py'),'--idf',str(idf),'--package',str(out),'--output',str(out/'spiffs-readback')],check=True)
 shutil.copyfile(PROFILE/'SETTINGS-TIMERS.md',out/'SETTINGS-TIMERS.md')
 shutil.copyfile(PROFILE/'PICKER-ARITHMETIC.md',out/'PICKER-ARITHMETIC.md')
 for doc in ['INSTALL.md','RECOVERY.md','TEST-ON-DEVICE.md','CAPABILITIES.md']:
  require((PROFILE/doc).is_file(),'Missing owner handoff: '+doc);shutil.copyfile(PROFILE/doc,out/doc)
 if built.get('native_alarms'):
  shutil.copyfile(PROFILE/'NATIVE-ALARMS.md',out/'NATIVE-ALARMS.md')
  for doc in ['CAPABILITIES.md','TEST-ON-DEVICE.md','REPRODUCE.md']:
   if (out/doc).exists():(out/doc).write_text('> Explicit native alarm successor: see NATIVE-ALARMS.md. Its selected profile supersedes inherited baseline exclusions below.\n\n'+(out/doc).read_text())
 shutil.copyfile(PROFILE/'QMI_SOURCES.json',out/'QMI-SOURCES.json')
 shutil.copytree(evidence,out/'software-evidence')
 shutil.copyfile(PROFILE/'REPRODUCE.md',out/'REPRODUCE.md')
 if built.get('native_alarms'):
  (out/'REPRODUCE.md').write_text('> This selected successor additionally requires --native-alarms with --native-daily --include-files --foreground-idle; use exact source-package.json pins. See NATIVE-ALARMS.md.\n\n'+(out/'REPRODUCE.md').read_text())
 if built.get('native_alarm_geometry'):
  shutil.copyfile(PROFILE/'NATIVE-ALARM-GEOMETRY.md',out/'NATIVE-ALARM-GEOMETRY.md')
  (out/'REPRODUCE.md').write_text('> Native alarm presentation additionally requires --native-alarm-geometry; see NATIVE-ALARM-GEOMETRY.md.\n\n'+(out/'REPRODUCE.md').read_text())
 if built.get('foreground_idle',{}).get('enabled'):
  for doc in ['FOREGROUND-IDLE.md','FOREGROUND-DEEP-FOLLOWUP.md']:shutil.copyfile(PROFILE/doc,out/doc)
 require((target/'APPLICATION-INVENTORY.json').is_file() and sha(target/'APPLICATION-INVENTORY.json')==built.get('application_inventory_sha256'),'Missing or changed source-profile application inventory')
 shutil.copyfile(target/'APPLICATION-INVENTORY.json',out/'APPLICATION-INVENTORY.json')
 write(out/'source-package.json',{'source_head':a.drivers_revision,'sources':built['source_pins'],'hardware_tested':False})
 if built.get('native_wifi_geometry'):
  shutil.copyfile(PROFILE/'NATIVE-WIFI-GEOMETRY.md',out/'NATIVE-WIFI-GEOMETRY.md')
  (out/'REPRODUCE.md').write_text('> Native WiFi presentation additionally requires --native-wifi-geometry; see NATIVE-WIFI-GEOMETRY.md.\n\n'+(out/'REPRODUCE.md').read_text())
 write_quick_actions_docs(out,built)
 sums=''.join(sha(f)+'  '+str(f.relative_to(out))+'\n' for f in sorted(out.rglob('*')) if f.is_file());(out/'SHA256SUMS').write_text(sums)
 archive=out.with_suffix('.zip');require(not archive.exists(),'Do not overwrite a delivered archive');shutil.make_archive(str(out),'zip',out);print(json.dumps({'zip':str(archive),**record(archive),'combined':receipt['combined']},indent=2))

def main():
 p=argparse.ArgumentParser(description=__doc__)
 for n in ['native','target','idf','runtime','admission-normal','admission-sanitized','execution-normal','execution-sanitized','software-evidence','output']:p.add_argument('--'+n,required=True,type=Path)
 p.add_argument('--drivers-revision',required=True)
 p.add_argument('--target-driver-source',type=Path,help='Explicit clean owning tree for reused store build inputs; default current Drivers')
 p.add_argument('--target-driver-revision',help='Exact owning commit when target-driver-source differs')
 a=p.parse_args()
 if bool(a.target_driver_source)!=bool(a.target_driver_revision):p.error('Target source and exact revision must be supplied together')
 package(a)
if __name__=='__main__':main()

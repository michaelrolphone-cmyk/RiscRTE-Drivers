#!/usr/bin/env python3
"""Recover selected source roles and immutable SDK anchors; never use latest refs."""
import argparse,ast,json,re,subprocess,tempfile
from badge_source_pack import restore
from pathlib import Path
ROLES=[('runtime','runtime','home'),('watch','watch','home'),('controllers','controllers','home'),('system','system','home'),('utilities','utilities','home'),('runtime-prototype','runtime','prototype'),('watch-prototype','watch_bringup','prototype'),('watch-alpha-baseline','watch_alpha_baseline','prototype'),('system-prototype','system','prototype'),('home-system','home_system','prototype'),('utilities-prototype','utilities','prototype')]
# Read pin values from their owning selected builders, not duplicated SDK versions.
ANCHORS=[('runtime','system-prototype/scripts/build_portable_settings.py',['NATIVE_TIME_RUNTIME_COMMIT']),('runtime-prototype','system-prototype/scripts/build_portable_settings.py',['NATIVE_TIME_RUNTIME_COMMIT']),('runtime-prototype','system-prototype/scripts/portable_foreground_idle_build.py',['RUNTIME_COMMIT']),('utilities-prototype','system-prototype/scripts/portable_alarm_build.py',['UTILITIES_COMMIT','CLOCK_EDIT_COMMIT','CATALOG_COMMIT']),('utilities-prototype','utilities-prototype/scripts/build_native_schedules_alarm.py',['BASE']),('watch-prototype','watch-prototype/scripts/test_meridiem_artwork.py',['baseline'])]
def exact_pin(value):
 if not isinstance(value,str) or not re.fullmatch('[0-9a-f]{40}',value):raise ValueError('Expected exact commit pin')
 return value
def constants(path,names):
 values={}
 for node in ast.parse(path.read_text()).body:
  if isinstance(node,ast.Assign):
   for target in node.targets:
    if isinstance(target,ast.Name) and target.id in names:values[target.id]=exact_pin(ast.literal_eval(node.value))
 if set(values)!=set(names):raise ValueError('Missing owning SDK constants: '+str(path))
 return values
def fetch_declared_successor(path,pin,spec,run=subprocess.run,output=subprocess.check_output):
 # Explicit new publications are separate from immutable recovered identities.
 # An absent old object never authorizes this path or a latest-ref fallback.
 exact_pin(pin)
 entry=spec.get('published_successors',{}).get(pin)
 if not isinstance(entry,dict):raise ValueError('Undeclared source successor')
 tree=exact_pin(entry.get('tree'))
 run(['git','-C',str(path),'fetch','--no-tags','--depth=1','origin',pin],check=True)
 actual=output(['git','-C',str(path),'rev-parse',pin+'^{tree}'],text=True).strip()
 if actual!=tree:raise ValueError('Published successor tree mismatch')
 return {'commit':pin,'tree':tree,'role':path.name,'method':'explicit-published-successor','recovery_original':spec['source_commit']}

def scene_qualification_anchors(profile,proof):
 if set(proof)!={'schema','tools','scene','before_scene'} or type(proof['schema']) is not int or proof['schema']!=1:raise ValueError('Invalid scene qualification lock')
 pins={key:exact_pin(proof[key]) for key in ['tools','scene','before_scene']}
 if pins['scene']!=profile['scene_contract_basis'] or len(set(pins.values()))!=3:raise ValueError('Conflated scene qualification roles')
 return [('scene-qualification-tools','system',pins['tools']),('timecard-pre-scene-baseline','system',pins['before_scene'])]

def recover(root,run=subprocess.run,output=subprocess.check_output,include_timecard=False):
 root=Path(root).resolve();recovery_lock=json.loads((root/'drivers/docs/badge-source-recovery/sources.json').read_text());restored={};pack_receipts=[];published_receipts=[];board=root/'drivers/boards/waveshare-esp32-s3-touch-amoled-1.75c';locks={n:json.loads((board/n/'sources.lock.json').read_text()) for n in ['home','prototype']};records=[]
 def fetch(path,pin):
  exact_pin(pin)
  repo=output(['git','-C',str(path),'config','--get','remote.origin.url'],text=True).strip().removesuffix('.git').removeprefix('https://github.com/')
  if repo in recovery_lock:
   if str(path) not in restored:
    spec=recovery_lock[repo];publication=exact_pin(spec['publication_commit'])
    run(['git','-C',str(path),'fetch','--no-tags','--depth=1','origin',publication],check=True)
    with tempfile.TemporaryDirectory(prefix='badge-source-pack-') as temp:
     folder=Path(temp)
     manifest=output(['git','-C',str(path),'show',publication+':docs/badge-source-recovery/manifest.json'])
     (folder/'manifest.json').write_bytes(manifest)
     parsed=json.loads(manifest)
     for part in parsed['parts']:
      name=part['path']
      if not re.fullmatch(r'objects-[0-9]{3}\.packpart',name):raise ValueError('Invalid recovery part path')
      (folder/name).write_bytes(output(['git','-C',str(path),'show',publication+':docs/badge-source-recovery/'+name]))
     record=restore(path,folder,repo,run,output)
    if record['source_commit']!=spec['source_commit'] or record['source_tree']!=spec['source_tree']:raise ValueError('Recovery lock identity mismatch')
    record.update({'role':path.name,'publication_commit':publication});pack_receipts.append(record);restored[str(path)]=True
   spec=recovery_lock[repo]
   if pin in spec.get('published_successors',{}):
    published_receipts.append(fetch_declared_successor(path,pin,spec,run,output))
   # All other pins must exist in the declared exact pack; no speculative fetch.
   run(['git','-C',str(path),'cat-file','-e',pin+'^{commit}'],check=True)
  else:run(['git','-C',str(path),'fetch','--no-tags','--depth=1','origin',pin],check=True)
  actual=output(['git','-C',str(path),'rev-parse',pin+'^{commit}'],text=True).strip()
  if actual!=pin:raise ValueError('Fetched dependency identity mismatch')
 roles=ROLES+([('productivity-prototype','productivity','prototype')] if include_timecard else [])
 reserved=[label for label,_,_ in roles]+(['timecard-productivity-baseline','timecard-system-baseline','timecard-alarm-sdk','scene-qualification-tools','timecard-pre-scene-baseline'] if include_timecard else [])+['spiffs']
 if any((root/label).exists() for label in reserved):raise ValueError('Fresh workspace roles required; preserve existing checkouts and prior failure logs')
 for label,key,kind in roles:
  pin=locks[kind][key];repo=pin['repository'];commit=exact_pin(pin['commit']);path=root/label
  if not re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+',repo):raise ValueError('Invalid repository identity')
  run(['git','init','--quiet',str(path)],check=True);run(['git','-C',str(path),'remote','add','origin','https://github.com/'+repo+'.git'],check=True);fetch(path,commit)
  run(['git','-C',str(path),'checkout','--quiet','--detach',commit],check=True);records.append({'role':label,'repository':repo,'commit':commit})
 if include_timecard:
  profile=json.loads((root/'productivity-prototype/sdk/timecard-current-profile.json').read_text())
  anchors=[('timecard-productivity-baseline','productivity',profile['authoritative_model_base']),('timecard-system-baseline','system',profile['scene_contract_basis']),('timecard-alarm-sdk','utilities',profile['alarm_sdk'])]
  proof=json.loads((root/'drivers/docs/badge-source-recovery/native-scene.json').read_text())
  anchors+=scene_qualification_anchors(profile,proof)
  for label,key,pin in anchors:
   path=root/label;repo=locks['prototype'][key]['repository'];pin=exact_pin(pin)
   if not re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+',repo):raise ValueError('Invalid repository identity')
   run(['git','init','--quiet',str(path)],check=True);run(['git','-C',str(path),'remote','add','origin','https://github.com/'+repo+'.git'],check=True);fetch(path,pin)
   run(['git','-C',str(path),'checkout','--quiet','--detach',pin],check=True);records.append({'role':label,'repository':repo,'commit':pin,'purpose':'owning selector parity or canonical alarm SDK'})
 extras=set()
 # Recover the current Drivers pack before resolving its historical SDK anchor.
 fetch(root/'drivers',locks['prototype']['drivers_base'])
 # A separate original snapshot supplies the untimed parity source. Import
 # before reading its owning pin; it is not substituted for any selected role.
 baseline=constants(root/'drivers/scripts/test_portable_alarm_light_sleep.py',['UNTIMED_BASELINE'])['UNTIMED_BASELINE']
 record=restore(root/'drivers',root/'drivers/docs/badge-source-recovery/untimed-baseline','michaelrolphone-cmyk/RiscRTE-Drivers',run,output)
 if record['source_commit']!=baseline:raise ValueError('Untimed baseline recovery identity mismatch')
 record['role']='drivers-untimed-baseline';pack_receipts.append(record);extras.add(('drivers',baseline))
 for label,path,names in ANCHORS:
  for name,pin in constants(root/path,names).items():extras.add((label,pin))
 spiffs=locks['prototype']['spiffs_lookup'];path=root/'spiffs'
 run(['git','init','--quiet',str(path)],check=True)
 record=restore(path,root/'drivers/docs/badge-source-recovery/spiffs',spiffs['repository'],run,output)
 if record['source_commit']!=spiffs['commit'] or record['shallow_boundaries']!=[spiffs['base']]:raise ValueError('SPIFFS source identity mismatch')
 run(['git','-C',str(path),'checkout','--quiet','--detach',spiffs['commit']],check=True);records.append({'role':'spiffs','repository':spiffs['repository'],'commit':spiffs['commit']});pack_receipts.append(record)
 helper=locks['prototype']['shared_clock_helper'];fetch(root/'runtime-prototype',helper['commit'])
 import hashlib
 if hashlib.sha256(output(['git','-C',str(root/'runtime-prototype'),'show',helper['commit']+':'+helper['path']])).hexdigest()!=helper['sha256']:raise ValueError('Shared clock helper identity mismatch')
 usb=json.loads((root/'drivers/docs/usb-hid-checked-source-lock.json').read_text());extras.add(('drivers',exact_pin(usb['cohorts']['pr40']['commit'])))
 for label,pin in sorted(extras):fetch(root/label,pin)
 receipt={'include_timecard':include_timecard,'roles':records,'verified_packs':pack_receipts,'published_sources':published_receipts,'extra_refs':[{'role':label,'commit':pin} for label,pin in sorted(extras)],'latest_ref_fallback':False}
 (root/'source-recovery.json').write_text(json.dumps(receipt,indent=2)+'\n');return receipt
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--workspace',type=Path,required=True);p.add_argument('--include-timecard',action='store_true');a=p.parse_args();recover(a.workspace,include_timecard=a.include_timecard)

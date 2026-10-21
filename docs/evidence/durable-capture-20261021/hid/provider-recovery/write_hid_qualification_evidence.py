from pathlib import Path
import hashlib,json,subprocess
root=Path.cwd(); evidence=root/'docs/evidence/usb-hid-host-20261021'
def git(repo,*args):return subprocess.check_output(['git','-C',str(repo),*args])
def snapshot(repo,commit,paths):
 files={}
 for p in paths:
  body=git(repo,'show',f'{commit}:{p}')
  files[p]={'git_blob':git(repo,'rev-parse',f'{commit}:{p}').decode().strip(),'bytes':len(body),'sha256':hashlib.sha256(body).hexdigest()}
 return {'commit':commit,'tree':git(repo,'rev-parse',f'{commit}^{{tree}}').decode().strip(),'files':files}
drivers=['usb_host_v2','usb_hid','usb_hid_keyboard','usb_hid_gamepad','usb_hid_text_input','usb_ui_navigation','usb_xinput_gamepad']
paths=[p for p in git(root,'ls-files','Drivers','sdk/driver').decode().splitlines() if p.startswith('sdk/driver/') or any(p.startswith('Drivers/'+d+'/') for d in drivers)]
paths += [f'test/drivers/{s}_test.c' for s in drivers+['usb_hid_quiesce','usb_hid_gamepad_descriptor']]
paths += ['sdk/driver/RiscUsbVbusV1.h','Drivers/usb_controller_esp32s3/manifest.json','dist/xteink-x4-pro/load-order.json','manifest/released-drivers.json']
paths=sorted(set(paths)); d=snapshot(root,'a7ae39994603c4545a3c014f09051ecfc12084d2',paths)
x4=root.parents[1]/'watch-battery-quick-actions/RiscRTE-XTEINK-X4-PRO'
x=snapshot(x4,'6b7d1abe9b2dcc236f6086d07c0f619244c907cb',['profiles/xteink-x4-pro.json','minimal/scripts/generate_profile.py','minimal/drivers/x4pro_board_power/manifest.json','minimal/drivers/x4pro_power/manifest.json','Drivers/x4pro_battery/README.md','minimal/docs/USB_PREPARATION_034.md','minimal/docs/USB_PREPARATION_036.md','docs/native-idf61/PACKAGING.md','docs/native-idf61/released-baseline.json','docs/native-idf61/released-profile.json'])
runtime=root.parents[1]/'acoustic-neural/RiscRTE'
r=snapshot(runtime,'13eca114cb58469cf6004fafc3da84fbd9d29beb',['docs/USB_CONTROLLER_IDF61_POLICY.md'])
base=json.loads(git(x4,'show','6b7d1abe9b2dcc236f6086d07c0f619244c907cb:docs/native-idf61/released-baseline.json'))
released={k:base[k] for k in ['release_tag','release_source_revision','runtime_revision','factory_bytes','factory_sha256','native_bytes','native_sha256','store_file_count','elf_count','historical_composition_recovered','historical_native_elf_recovered']}
released['usb_or_hid_internal_files']={k:v for k,v in base['files'].items() if 'usb' in k or 'hid' in k}
released['usb_or_hid_sd_files']={k:v for k,v in base['sd']['files'].items() if 'usb' in k or 'hid' in k}
ledger={'schema':1,'observed_at':'2026-10-21T10:50:56Z','method':'Read-only remote main verification with git ls-remote; immutable Git source reads; local C fixture execution. Released binary identities below are recorded product evidence, not freshly downloaded/rehashed releases.','repositories':{'Drivers':d,'X4':x,'Runtime':r},'recorded_x4_release':released,'results':{'plain':'8/8 passed','asan_ubsan':'8/8 passed with ASAN_OPTIONS=detect_leaks=0','lsan':'not run successfully: ptrace unsupported','physical_usb':'not run','target_build':'not run','publication':'not performed','production_source_changed':False},'coverage_note':'The initial pass executed nine paths; usb_hid_gamepad_test.c and usb_hid_gamepad_descriptor_test.c are byte-identical. Final runner executes eight distinct fixture bodies per mode.','failed_attempts':['Rhizome export limit=100 rejected by parameter schema; limit=50 succeeded.','Rhizome start omitted requestId and was rejected before execution; corrected requestId start succeeded once.','LeakSanitizer refused ptrace; ASan/UBSan rerun with leak detection disabled passed.','One read-only shell launch failed before process creation with exec-server transport disconnected; equivalent local reads then succeeded.']}
(evidence/'source-composition.json').write_text(json.dumps(ledger,indent=2)+'\n')
print('Recorded',len(d['files']),'Drivers files and',len(x['files']),'X4 files')

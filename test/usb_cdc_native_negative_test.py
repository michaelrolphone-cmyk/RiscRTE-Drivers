#!/usr/bin/env python3
"""Reject ownership/deadline/discovery regressions using actual class source."""
import hashlib, os, pathlib, shutil, subprocess, tempfile
root=pathlib.Path(__file__).resolve().parents[1]
original=(root/'Drivers/usb_cdc_native/driver.c').read_text()
mutations={
 'budget-reset':('uint32_t ms = remaining(b);','uint32_t ms = b->ms;','budget-open','control_budget==5'),
 'premature-queue-close':('if (!finish_queues(s, RISC_STREAM_CANCELLED, b))', 'if (s->rx) (void)streams->close(streams->context, s->rx);\n    if (!finish_queues(s, RISC_STREAM_CANCELLED, b))','delayed-ack','!claim_live[0]&&!claim_live[1]'),
 'claim-forget':('if (!remaining(b) || !discovery->release_checked(host->context, s->data_claim)) return RISC_STREAM_RETAINED;', '(void)discovery->release_checked(host->context, s->data_claim);','delayed-ack','!claim_live[0]&&!claim_live[1]'),
 'unknown-inventory-pump':(' || s->paused','', 'inventory-failure','!reads&&!writes'),
 'daemon-discovery':('uint64_t tokens[RISC_USB_HOST_MAX_DEVICES] = {0};','size_t pumped = 0; (void)discovery->poll(host->context, 16, &pumped);\n    uint64_t tokens[RISC_USB_HOST_MAX_DEVICES] = {0};','budget-open','daemon discovery must not run'),
}
flags=['-std=c11','-Wall','-Wextra','-Werror','-g']
if os.environ.get('SANITIZE')=='1':flags+=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer']
with tempfile.TemporaryDirectory() as td:
 d=pathlib.Path(td);shutil.copy(root/'Drivers/usb_cdc_native/Descriptors.inc',d)
 subprocess.run(['cc',*flags,'-I'+str(root/'sdk/driver'),str(root/'test/drivers/usb_cdc_native_test.c'),'-ldl','-o',str(d/'test')],check=True)
 for name,(old,new,mode,reason) in mutations.items():
  assert old in original
  (d/'driver.c').write_text(original.replace(old,new,1))
  subprocess.run(['cc',*flags,'-fPIC','-fvisibility=hidden','-shared','-I'+str(root/'sdk/driver'),str(d/'driver.c'),'-o',str(d/'cdc.so')],check=True)
  r=subprocess.run([str(d/'test'),str(d/'cdc.so'),mode],capture_output=True,text=True)
  assert r.returncode and reason in r.stderr,(name,r.returncode,r.stdout,r.stderr)
  print('PASS rejected mutation',name,':',r.stderr.strip())

# Reproduce the review finding against the exact immutable prerequisite host.
with tempfile.TemporaryDirectory() as td:
 d=pathlib.Path(td)
 # The original b4334b7 checkpoint is preserved in the recovery bundle.
 # Its published checked-cleanup equivalent has the identical Git blob below.
 baseline_ref='ad943a4a514c910c777a427c8cad6d6841f48fd1:Drivers/usb_host_v2/driver.c'
 raw=subprocess.check_output(['git','-C',str(root),'show',baseline_ref])
 assert hashlib.sha1(b'blob '+str(len(raw)).encode()+b'\0'+raw).hexdigest()=='ed93499c915ba86f2764d82df6f69ea219d32319', 'prerequisite host bytes changed'
 baseline=raw.decode()
 (d/'host.c').write_text(baseline)
 subprocess.run(['cc',*flags,'-fPIC','-fvisibility=hidden','-shared','-I'+str(root/'sdk/driver'),str(d/'host.c'),'-o',str(d/'host.so')],check=True)
 subprocess.run(['cc',*flags,'-fPIC','-fvisibility=hidden','-shared','-I'+str(root/'sdk/driver'),str(root/'Drivers/usb_cdc_native/driver.c'),'-o',str(d/'cdc.so')],check=True)
 subprocess.run(['cc',*flags,'-DUSE_REAL_HOST','-I'+str(root/'sdk/driver'),str(root/'test/drivers/usb_cdc_native_test.c'),'-ldl','-o',str(d/'test')],check=True)
 r=subprocess.run([str(d/'test'),str(d/'cdc.so'),'real-dirty-claim',str(d/'host.so')],capture_output=True,text=True)
 assert r.returncode and 'RISC_STREAM_RETAINED' in r.stderr,(r.returncode,r.stdout,r.stderr)
 print('PASS rejected prerequisite partial-claim loss:',r.stderr.strip())

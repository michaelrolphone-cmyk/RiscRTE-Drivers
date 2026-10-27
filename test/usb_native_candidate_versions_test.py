#!/usr/bin/env python3
"""Actual pinned Runtime identity parser; not target ELF/loader admission."""
import json, pathlib, subprocess, tempfile, sys
root=pathlib.Path(__file__).resolve().parents[1];runtime=pathlib.Path(sys.argv[1]).resolve()
assert subprocess.check_output(['git','-C',str(runtime),'rev-parse','HEAD'],text=True).strip()=='6a3b3d46f5b2857eacc6e9863799dec1f466b036'
paths=['Drivers/usb_cdc_native/manifest.json','candidates/usb-native-stream/usb-host-v2.json','candidates/usb-native-stream/usb-controller-esp32s3.json']
old=['Drivers/usb_host_v2/manifest.json','Drivers/usb_controller_esp32s3/manifest.json']
source='#include "runtime/packages/PackageIdentity.h"\n#include <cassert>\nint main(){ RuntimePackages::Identity out;\n'
for p,want in [(p,True) for p in paths]+[(p,False) for p in old]:
 m=json.loads((root/p).read_text())
 if p==paths[0]:
  assert m['id']=='usb-cdc-acm' and tuple(map(int,m['version'].split('.')))>(0,1,8)
 expr='RuntimePackages::makeIdentity(RuntimePackages::Kind::Driver,'+','.join(json.dumps(m[k]) for k in ['id','version','file_name'])+',false,&out)'
 source+='assert('+('' if want else '!')+expr+');\n'
source+='}\n'
with tempfile.TemporaryDirectory() as d:
 d=pathlib.Path(d);(d/'test.cpp').write_text(source)
 subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-I'+str(runtime/'src'),str(d/'test.cpp'),'-o',str(d/'test')],check=True)
 subprocess.run([str(d/'test')],check=True)
print('PASS actual Runtime package identity: canonical CDC 0.1.9, host 0.1.7, controller 0.1.27 accepted; both preserved prerelease manifests rejected')

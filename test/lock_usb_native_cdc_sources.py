#!/usr/bin/env python3
"""Record exact source inputs; no network, builds or working-tree mutation."""
import hashlib,json,pathlib,subprocess,sys
root=pathlib.Path(__file__).resolve().parents[1]
runtime,reader,system,usb=(pathlib.Path(x).resolve() for x in sys.argv[1:5])
def git(p,*args):return subprocess.check_output(['git','-C',str(p),*args],text=True).strip()
def identity(p):return {'commit':git(p,'rev-parse','HEAD'),'tree':git(p,'rev-parse','HEAD^{tree}'),'tracked_clean':not git(p,'status','--porcelain','--untracked-files=no')}
def digest(p):b=p.read_bytes();return {'sha256':hashlib.sha256(b).hexdigest(),'bytes':len(b)}
locked={'drivers_prerequisite':'b4334b76124f80d1252eb7dda9e903fd1b7b4d39','drivers_implementation':'d4c9fe436c5bd6b73e1feac87ac0df7e04ba0bf3','runtime':identity(runtime),'reader':identity(reader),'system':identity(system),'esp_usb':identity(usb),'files':{}}
assert locked['runtime']['commit']=='6a3b3d46f5b2857eacc6e9863799dec1f466b036'
assert locked['reader']['commit']=='c98e4222048244b1e729869ed47aa99130025fd3'
assert locked['system']['commit']=='f9d55dbeb941ba40fb965f53172ce3e1b85af376'
assert locked['esp_usb']['commit']=='0f6820940f9e8075c0cc1483d9c2f5e89cc5b97a'
paths=set(git(root,'diff','--name-only',locked['drivers_prerequisite']).splitlines())
paths.update(git(root,'ls-files','--others','--exclude-standard').splitlines())
paths.update(['Drivers/usb_controller_esp32s3/driver_base.cpp','sdk/driver/RiscUsbProviderV1.h','sdk/driver/RiscUsbControllerV1.h','sdk/driver/RiscPlatformClockV1.h'])
for p in sorted(paths):
 if p and not p.startswith('docs/') and (root/p).is_file():locked['files']['Drivers/'+p]=digest(root/p)
for prefix,r,items in [('Runtime',runtime,['sdk/driver/RiscProviderV2.h','sdk/driver/RiscStreamProviderV1.h','sdk/driver/RiscStreamSessionProviderV1.h','sdk/driver/RiscStreamResultV1.h','sdk/driver/RiscPlatformClockV1.h','sdk/app/RiscRuntimeV1.h','sdk/app/RiscStreamClientV1.h','sdk/app/RiscSerialStreamSessionV1.h','src/runtime/packages/PackageIdentity.h','src/runtime/streams/AppStreamSessions.cpp','src/runtime/streams/ProviderQueueHost.cpp','src/runtime/drivers/ProviderGraphV2.cpp','src/runtime/drivers/ProviderModuleV2.cpp','src/bootstrap/Runtime.cpp','src/bootstrap/Board.cpp','src/bootstrap/Json.cpp']),('Reader',reader,['sdk/driver/RiscSerialPortV1.h','Drivers/usb_cdc_v2/driver.c']),('System',system,['lib/PortableApps/src/PortableSerialClient.c','lib/PortableApps/include/PortableSerialClient.h','lib/PortableApps/SERIAL_CLIENT_PROVENANCE.json','scripts/test_portable_serial_client.py'])]:
 for p in items:locked['files'][prefix+'/'+p]=digest(r/p)
for p in ['RiscProviderV2.h','RiscStreamProviderV1.h','RiscStreamSessionProviderV1.h','RiscStreamResultV1.h','RiscPlatformClockV1.h']:
 assert (root/'sdk/driver'/p).read_bytes()==(runtime/'sdk/driver'/p).read_bytes()
assert (root/'sdk/driver/RiscSerialPortV1.h').read_bytes()==(reader/'sdk/driver/RiscSerialPortV1.h').read_bytes()
assert (root/'sdk/driver/RiscSerialStreamSessionV1.h').read_bytes()==(runtime/'sdk/app/RiscSerialStreamSessionV1.h').read_bytes()
source=(reader/'Drivers/usb_cdc_v2/driver.c').read_text();a=source.index('static bool parse(');b=source.index('/* Provider-owned matching',a)
assert (root/'Drivers/usb_cdc_native/Descriptors.inc').read_text().split('\n',1)[1]==source[a:b]
locked['tools']={name:subprocess.check_output([name,'--version'],text=True).splitlines()[0] for name in ['cc','c++','python3','bash']}
print(json.dumps(locked,indent=2,sort_keys=True))

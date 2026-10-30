#!/usr/bin/env python3
"""Build the current TPS lease/profile/lifecycle family, never historical bytes."""
import argparse,hashlib,json,shlex,subprocess,tempfile
from pathlib import Path
from native_driver_toolchain import compiler
from normalize_xtensa_relocations import normalize
from cst9217_sdk import stage_sdk
ROOT=Path(__file__).resolve().parents[1]
PACKAGES={
 'tps65185_power':('tps65185-power','0.1.1',['hardware.device','i2c.bus','gpio.expander','platform.clock','platform.sync','board.display.tps65185.profile'],'display.panel-lease'),
 't5s3_panel_profile':('t5s3-panel-profile','0.1.0',[],'board.display.tps65185.profile'),
 'panel_lease_lifecycle':('panel-lease-lifecycle','0.1.0',['display.panel-lease'],'display.power')}
OWN_HEADERS={'RiscPanelLeaseV1.h','RiscTps65185ProfileV1.h'}
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def expected_manifest(name):
 id,version,requires,capability=PACKAGES[name]
 m={'type':'driver','id':id,'version':version,'driver_abi':2,'architecture':'xtensa-esp32s3','file_name':'driver.elf','requires':[{'capability':x,'api':1} for x in requires],'provides':[{'capability':capability,'api':1}],'status':'experimental-unpublished'}
 if name=='tps65185_power':m['hardware_compatibility']=[{'compatible':'ti,tps65185','revisions':['unspecified'],'config_type':'peripheral.i2c','config_version':1}]
 if name=='t5s3_panel_profile':m['board']='t5s3-pro'
 return m
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--runtime',required=True,type=Path);p.add_argument('--output',type=Path,default=ROOT/'dist/panel-lease');a=p.parse_args()
 manifests={}
 for name in PACKAGES:
  manifest=json.loads((ROOT/'Drivers'/name/'manifest.json').read_text())
  if manifest!=expected_manifest(name):raise ValueError('Current panel manifest mismatch: '+name)
  manifests[name]=manifest
 lock=json.loads((ROOT/'qualification/panel-lease-source.json').read_text())['dependency_inputs'];runtime=a.runtime.resolve()
 cc=compiler();readelf=str(Path(cc).with_name(Path(cc).name.replace('gcc','readelf')))
 libgcc=subprocess.check_output([cc,'-print-libgcc-file-name'],text=True).strip()
 a.output.mkdir(parents=True,exist_ok=True);inputs={};commands=[];packages={}
 with tempfile.TemporaryDirectory(prefix='panel-sdk-') as tmp:
  sdk=stage_sdk(ROOT,runtime,Path(tmp));origins={}
  for label,base,relative in [('drivers',ROOT,'sdk/driver'),('runtime',runtime,'sdk/app'),('runtime',runtime,'sdk/driver'),('runtime',runtime,'sdk/hardware')]:
   for header in (base/relative).glob('*.h'):origins[header.name]=(label+'/'+str(header.relative_to(base)),header)
  common=[cc,'-std=c11','-Os','-Wall','-Wextra','-Werror','-fPIC','-mtext-section-literals','-mlongcalls','-fvisibility=hidden','-nostdlib','-nostartfiles','-I'+str(sdk)]
  for name,manifest in manifests.items():
   src=ROOT/'Drivers'/name/'driver.c';out=a.output/name;out.mkdir(parents=True,exist_ok=True);elf=out/'driver.elf'
   scan=[*common,'-M','-MT','dependencies',str(src)];commands.append(scan);raw=subprocess.check_output(scan,text=True)
   for file in shlex.split(raw.split(':',1)[1].replace('\\\n',' ')):
    path=Path(file).resolve()
    if path.parent==sdk:
     key,original=origins[path.name]
     if sha(path)!=sha(original):raise ValueError('Staged SDK bytes changed: '+file)
     if path.name not in OWN_HEADERS and (key not in lock or sha(original)!=lock[key]['sha256']):raise ValueError('Unpinned selected SDK header: '+key)
     inputs[key]=sha(original)
    elif path==src:inputs['drivers/'+str(src.relative_to(ROOT))]=sha(path)
    else:
     toolroot=Path(cc).resolve().parents[1]
     if not path.is_relative_to(toolroot):raise ValueError('Unexpected external build dependency: '+file)
     inputs['toolchain/'+str(path.relative_to(toolroot))]=sha(path)
   command=[*common,'-shared','-Wl,--hash-style=sysv','-Wl,--exclude-libs,ALL',str(src),'-lgcc','-o',str(elf)];commands.append(command);subprocess.run(command,check=True);normalize(elf)
   symbols=subprocess.check_output([readelf,'--dyn-syms','--wide',str(elf)],text=True)
   exports={f[7] for line in symbols.splitlines() if len((f:=line.split()))>=8 and f[4]=='GLOBAL' and f[6]!='UND' and f[3]=='FUNC'}
   imports={f[7] for line in symbols.splitlines() if len((f:=line.split()))>=8 and f[6]=='UND' and f[7]}
   if exports!={'t5_driver_get'} or not imports<={'memcpy','memset','strcmp'}:raise ValueError('Unexpected exports/imports: '+repr((name,exports,imports)))
   data=elf.read_bytes()
   if not 52<=len(data)<=256*1024 or data[:7]!=b'\x7fELF\x01\x01\x01' or int.from_bytes(data[16:18],'little')!=3 or int.from_bytes(data[18:20],'little')!=94:raise ValueError('Invalid Xtensa shared provider: '+name)
   manifest.update(size_bytes=len(data),sha256=sha(elf));(out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n');packages[name]={'bytes':len(data),'sha256':sha(elf),'exports':sorted(exports),'imports':sorted(imports)}
 for path in [*[ROOT/'Drivers'/name/'manifest.json' for name in PACKAGES],ROOT/'qualification/panel-lease-source.json',ROOT/'scripts/build_panel_lease.py',ROOT/'scripts/native_driver_toolchain.py',ROOT/'scripts/normalize_xtensa_relocations.py',ROOT/'scripts/cst9217_sdk.py']:
  inputs['drivers/'+str(path.relative_to(ROOT))]=sha(path)
 receipt={'hardware_tested':False,'deep_sleep_qualified':False,'historical_outputs_modified':False,'inputs':dict(sorted(inputs.items())),'commands':commands,'tools':{k:{'path':v,'sha256':sha(v)} for k,v in [('compiler',cc),('readelf',readelf),('libgcc',libgcc)]},'compiler_version':subprocess.check_output([cc,'--version'],text=True),'packages':packages}
 (a.output/'build-provenance.json').write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps(packages,indent=2))
if __name__=='__main__':main()

#!/usr/bin/env python3
"""Build current scoped PCA9535 only; retain original source/release evidence."""
import argparse,hashlib,json,shlex,subprocess,tempfile
from pathlib import Path
from native_driver_toolchain import compiler
from normalize_xtensa_relocations import normalize
from cst9217_sdk import stage_sdk
ROOT=Path(__file__).resolve().parents[1]
SRC=ROOT/'Drivers/pca9535_gpio'
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def expected_manifest():
 return {'type':'driver','id':'pca9535-gpio','version':'0.1.1','driver_abi':2,'architecture':'xtensa-esp32s3','file_name':'driver.elf','requires':[{'capability':x,'api':1} for x in ['hardware.device','i2c.bus','platform.sync']],'provides':[{'capability':'gpio.expander','api':1}],'hardware_compatibility':[{'compatible':'nxp,pca9535','revisions':['unspecified'],'config_type':'peripheral.i2c','config_version':1}],'status':'experimental-unpublished'}
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--runtime',required=True,type=Path);p.add_argument('--output',type=Path,default=ROOT/'dist/pca9535-gpio');a=p.parse_args()
 manifest=json.loads((SRC/'manifest.json').read_text())
 if manifest!=expected_manifest():raise ValueError('PCA9535 current manifest mismatch')
 lock=json.loads((SRC/'source-map.json').read_text())['dependency_inputs'];runtime=a.runtime.resolve()
 cc=compiler();readelf=str(Path(cc).with_name(Path(cc).name.replace('gcc','readelf')))
 libgcc=subprocess.check_output([cc,'-print-libgcc-file-name'],text=True).strip()
 a.output.mkdir(parents=True,exist_ok=True);elf=a.output/'driver.elf';inputs={};commands=[]
 with tempfile.TemporaryDirectory(prefix='pca-sdk-') as tmp:
  sdk=stage_sdk(ROOT,runtime,Path(tmp))
  origins={}
  for label,base,relative in [('drivers',ROOT,'sdk/driver'),('runtime',runtime,'sdk/app'),('runtime',runtime,'sdk/driver'),('runtime',runtime,'sdk/hardware')]:
   for header in (base/relative).glob('*.h'):origins[header.name]=(label+'/'+str(header.relative_to(base)),header)
  common=[cc,'-std=c11','-Os','-Wall','-Wextra','-Werror','-fPIC','-mtext-section-literals','-mlongcalls','-fvisibility=hidden','-nostdlib','-nostartfiles','-I'+str(sdk)]
  scan=[*common,'-M','-MT','dependencies',str(SRC/'driver.c')];commands.append(scan)
  raw=subprocess.check_output(scan,text=True)
  for name in shlex.split(raw.split(':',1)[1].replace('\\\n',' ')):
   path=Path(name).resolve()
   if path.parent==sdk:
    key,original=origins[path.name]
    if sha(path)!=sha(original):raise ValueError('Staged SDK bytes changed: '+name)
    if path.name!='RiscGpioExpanderV1.h' and (key not in lock or sha(original)!=lock[key]['sha256']):raise ValueError('Unpinned selected SDK header: '+key)
    inputs[key]=sha(original)
   elif path==SRC/'driver.c':inputs['drivers/Drivers/pca9535_gpio/driver.c']=sha(path)
   else:
    # Compiler dependency files are recorded individually, not called SDK source.
    toolroot=Path(cc).resolve().parents[1]
    if not path.is_relative_to(toolroot):raise ValueError('Unexpected external build dependency: '+name)
    inputs['toolchain/'+str(path.relative_to(toolroot))]=sha(path)
  command=[*common,'-shared','-Wl,--hash-style=sysv','-Wl,--exclude-libs,ALL',str(SRC/'driver.c'),'-lgcc','-o',str(elf)];commands.append(command)
  subprocess.run(command,check=True)
 normalize(elf)
 symbols=subprocess.check_output([readelf,'--dyn-syms','--wide',str(elf)],text=True)
 exports={f[7] for line in symbols.splitlines() if len((f:=line.split()))>=8 and f[4]=='GLOBAL' and f[6]!='UND' and f[3]=='FUNC'}
 imports={f[7] for line in symbols.splitlines() if len((f:=line.split()))>=8 and f[6]=='UND' and f[7]}
 if exports!={'t5_driver_get'} or not imports<={'memcpy','memset','strcmp'}:raise ValueError('Unexpected PCA exports/imports: '+repr((exports,imports)))
 data=elf.read_bytes()
 if not 52<=len(data)<=256*1024 or data[:7]!=b'\x7fELF\x01\x01\x01' or int.from_bytes(data[16:18],'little')!=3 or int.from_bytes(data[18:20],'little')!=94:raise ValueError('Invalid Xtensa shared provider')
 manifest.update(size_bytes=len(data),sha256=sha(elf));(a.output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
 for path in [SRC/'manifest.json',SRC/'source-map.json',ROOT/'scripts/build_pca9535_gpio.py',ROOT/'scripts/native_driver_toolchain.py',ROOT/'scripts/normalize_xtensa_relocations.py',ROOT/'scripts/cst9217_sdk.py']:
  inputs['drivers/'+str(path.relative_to(ROOT))]=sha(path)
 receipt={'hardware_tested':False,'historical_outputs_modified':False,'inputs':dict(sorted(inputs.items())),'commands':commands,'tools':{k:{'path':v,'sha256':sha(v)} for k,v in [('compiler',cc),('readelf',readelf),('libgcc',libgcc)]},'compiler_version':subprocess.check_output([cc,'--version'],text=True),'imports':sorted(imports),'exports':sorted(exports),'elf':{'bytes':len(data),'sha256':sha(elf)}}
 (a.output/'build-provenance.json').write_text(json.dumps(receipt,indent=2)+'\n');print('PCA9535 target build:',len(data),sha(elf))
if __name__=='__main__':main()

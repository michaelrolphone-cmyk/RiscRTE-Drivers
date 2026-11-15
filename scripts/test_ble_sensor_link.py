#!/usr/bin/env python3
"""Actual passive provider receiver-link semantics with endpoint doubles."""
import argparse,hashlib,json,os,pathlib,subprocess
R=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--output',type=pathlib.Path,required=True);p.add_argument('--sanitize',action='store_true');a=p.parse_args()
a.output.mkdir(parents=True,exist_ok=True)
flags=['-std=c11','-O1','-g0','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-I'+str(R/'sdk/driver')]
if a.sanitize:flags+=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-no-pie']
src=R/'test/ble_sensor_link_test.c';binary=a.output/'test';dep=a.output/'inputs.d'
cmd=[os.environ.get('CC','cc'),*flags,'-MMD','-MF',str(dep),str(src),'-o',str(binary)]
subprocess.run(cmd,check=True)
text=dep.read_text().replace('\\\n',' ');names=text.split(':',1)[1].split()
inputs={str(pathlib.Path(n).resolve()):hashlib.sha256(pathlib.Path(n).read_bytes()).hexdigest() for n in names}
result=subprocess.run([str(binary)],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
(a.output/'run.log').write_text(result.stdout);print(result.stdout,end='')
for n,h in inputs.items():assert hashlib.sha256(pathlib.Path(n).read_bytes()).hexdigest()==h,n
receipt={'cases':32,'passed':result.returncode==0,'sanitize':a.sanitize,'command':cmd,'inputs':inputs,
'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'scope':'actual provider plus controller/clock endpoint doubles; no physical radio',
'compiler':subprocess.check_output([cmd[0],'--version'],text=True).splitlines()[0]}
(a.output/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
raise SystemExit(result.returncode)

#!/usr/bin/env python3
import argparse,os,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--wave',type=Path,required=True);p.add_argument('--watch',type=Path,required=True);p.add_argument('--sanitize',action='store_true');a=p.parse_args()
r=Path(__file__).resolve().parents[1];w=a.watch.resolve();v=a.wave.resolve()
common=['-std=c11','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation']
if a.sanitize:common+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
common+=['-I'+str(x) for x in [r/'sdk/driver',w/'sdk/driver',w/'include',v/'include',v/'tests']]
with tempfile.TemporaryDirectory() as td:
 objects=[]
 for name,file in [('codec',v/'drivers/codec_es83xx/driver.c'),('input',r/'Drivers/pcm_audio_input/driver.c'),('output',r/'Drivers/pcm_audio_output/driver.c')]:
  target=Path(td)/(name+'.o');objects.append(str(target))
  subprocess.run([os.environ.get('CC','cc'),*common,'-Dt5_driver_get='+name+'_driver_get','-c',str(file),'-o',str(target)],check=True)
 target=Path(td)/'test'
 subprocess.run([os.environ.get('CC','cc'),*common,str(r/'tests/test_audio_integration.c'),*objects,'-o',str(target)],check=True)
 subprocess.run([str(target)],check=True)

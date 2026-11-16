#!/usr/bin/env python3
import argparse,json,os,subprocess,hashlib
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--native-command',type=Path,required=True);p.add_argument('--sdk',type=Path,required=True);p.add_argument('--stripe-rows',type=int,default=4);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
out=a.output;out.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[1]
base=json.loads(a.native_command.read_text());source=next(x for x in base if x.endswith('/tests/co5300_native/test.cpp'));old=source[:-len('/tests/co5300_native/test.cpp')]
base=[x.replace(old,str(root)) for x in base if not x.startswith('-fsanitize=') and x not in ('-fno-sanitize-recover=all','-fno-omit-frame-pointer')]
base[base.index('-o')+1]=str(out/'native.o');base[base.index(str(root/'tests/co5300_native/test.cpp'))]=str(root/'tests/co5300_native/facade_reuse_native.cpp');base+=['-c','-g0','-Wno-return-type']
receipts=[]
for sanitize in (False,True):
 suffix='sanitized' if sanitize else 'normal';san=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-no-pie'] if sanitize else []
 c=['cc','-std=c11','-O1','-g0','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-variable','-Wno-unused-function','-I'+str(root/'components/co5300/include'),'-I'+str(a.sdk),'-I'+str(root/'tests'),'-include',str(root/'boards/waveshare-esp32-s3-touch-amoled-1.75c/display-profile.h'),'-DRISC_CO5300_STRIPE_ROWS='+str(a.stripe_rows),'-c',str(root/'tests/co5300_native/facade_reuse.c'),'-o',str(out/'facade.o'),*san]
 commands=[c,base+san,['c++',str(out/'facade.o'),str(out/'native.o'),'-o',str(out/'composition'),*san],[str(out/'composition')]]
 (out/(suffix+'-commands.json')).write_text(json.dumps(commands,indent=2)+'\n')
 with (out/(suffix+'.log')).open('w') as log:
  for cmd in commands:
   q=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,env={**os.environ,'TMPDIR':str(out),'ASAN_OPTIONS':'detect_leaks=0'})
   if q.returncode:raise SystemExit(suffix+' failed '+str(q.returncode))
 receipts.append({'lane':suffix,'passed':True})
(out/'receipt.json').write_text(json.dumps({'lanes':receipts,'source_pin':subprocess.check_output(['git','-C',str(root),'rev-parse','HEAD'],text=True).strip(),'sources':{str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [root/'tests/co5300_native/facade_reuse.c',root/'tests/co5300_native/facade_reuse_native.cpp',root/'components/co5300/facade/driver.c',root/'components/co5300/include/common/display.h',root/'components/co5300/native/Co5300DisplayIdf6.inc']},'limits':'Actual C facade/native C++ composition with host SDK/provider/SPI mocks; not real DMA timing, display or physical sleep acceptance.'},indent=2)+'\n')
print('normal and ASan/UBSan reuse sleep/Deep rollback composition PASS')

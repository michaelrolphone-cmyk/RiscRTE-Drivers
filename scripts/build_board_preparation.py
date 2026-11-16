#!/usr/bin/env python3
"""Build a PIC Xtensa archive for later provider integration, not firmware."""
import hashlib
import json
from pathlib import Path
import subprocess
from native_driver_toolchain import compiler
from board_preparation import admit_badge175, header
ROOT=Path(__file__).resolve().parents[1]
BOARD=ROOT/'boards/waveshare-esp32-s3-touch-amoled-1.75c'
OUT=ROOT/'dist/badge-board-preparation'

def main():
    p=admit_badge175(json.loads((BOARD/'preparation.json').read_text()))
    if header(p)!=(BOARD/'startup-config.h').read_text():raise SystemExit('stale generated config')
    cc=compiler(); OUT.mkdir(parents=True,exist_ok=True)
    objects=[]
    for source in (ROOT/'boards/common/prepare.c',BOARD/'startup.c'):
        target=OUT/(source.stem+'.o');objects.append(target)
        subprocess.run([cc,'-std=c11','-Wall','-Wextra','-Werror','-Os','-fPIC','-mtext-section-literals',
            '-mlongcalls','-ffreestanding','-fno-builtin','-I'+str(ROOT/'sdk/driver'),
            '-I'+str(ROOT/'boards/common'),'-I'+str(BOARD),'-c',str(source),'-o',str(target)],check=True)
        data=target.read_bytes()
        if data[:7]!=b'\x7fELF\x01\x01\x01' or int.from_bytes(data[16:18],'little')!=1 or int.from_bytes(data[18:20],'little')!=94:
            raise SystemExit('not a little-endian Xtensa relocatable object')
    ar=cc.removesuffix('gcc')+'ar'
    archive=OUT/'libbadge-board-preparation.a'
    subprocess.run([ar,'rcsD',str(archive),*map(str,objects)],check=True)
    receipt={'schema':1,'artifact_kind':'static integration library, not driver ELF or firmware',
       'board_id':p['board_id'],'hardware_verified':False,'installable':False,
       'compiler':subprocess.check_output([cc,'--version'],text=True).splitlines()[0],
       'profile_sha256':hashlib.sha256((BOARD/'preparation.json').read_bytes()).hexdigest(),
       'files':{f.name:{'bytes':f.stat().st_size,'sha256':hashlib.sha256(f.read_bytes()).hexdigest()} for f in [*objects,archive]}}
    (OUT/'build-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps(receipt,indent=2))

if __name__=='__main__': main()

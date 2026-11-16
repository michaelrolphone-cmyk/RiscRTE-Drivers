#!/usr/bin/env python3
"""Read back every packaged file using the pinned target SPIFFS C implementation."""
import argparse
import json
from pathlib import Path
import subprocess
from package_badge_flash import IDF, git, require, inventory, read

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--idf',required=True,type=Path);p.add_argument('--package',required=True,type=Path);p.add_argument('--output',required=True,type=Path)
    a=p.parse_args();idf=a.idf.resolve();package=a.package.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
    require(git(idf,'rev-parse','HEAD')==IDF and not git(idf,'status','--porcelain'),'Pinned clean IDF required')
    require(inventory(package/'store')==read(package/'package.json')['store_files'],'Packaged store changed')
    cfg=read(package/'sdkconfig.json')
    (out/'sdkconfig.h').write_text('\n'.join('#define CONFIG_'+k+' '+('1' if v is True else str(v)) for k,v in cfg.items() if k.startswith('SPIFFS_') and v is not False)+'\n')
    (out/'esp_log.h').write_text('#define ESP_LOGD(...) ((void)0)\n')
    (out/'esp_assert.h').write_text('#define ESP_STATIC_ASSERT _Static_assert\n')
    src=idf/'components/spiffs/spiffs/src';root=Path(__file__).resolve().parents[1]
    command=['cc','-std=c11','-O2','-Wall','-Wextra','-Wno-unused-parameter','-I'+str(out),'-I'+str(idf/'components/spiffs/include'),'-I'+str(src),str(root/'test/badge_spiffs_readback.c'),*[str(src/n) for n in ('spiffs_cache.c','spiffs_check.c','spiffs_gc.c','spiffs_hydrogen.c','spiffs_nucleus.c')],'-o',str(out/'readback')]
    subprocess.run(command,check=True)
    expected=read(package/'package.json');count=len(expected['store_files']);size=(package/'bootfs.bin').stat().st_size
    result=subprocess.run([str(out/'readback'),str(package/'bootfs.bin'),str(package/'store'),str(size),str(count)],check=True,text=True,capture_output=True)
    (out/'readback.log').write_text(result.stdout);print(result.stdout,end='')
    (out/'readback.json').write_text(json.dumps({'passed':True,'files':count,'image_bytes':size,'write_calls':0,'physical_device_operations':0,'idf_revision':IDF,'command':command},indent=2)+'\n')
if __name__=='__main__':main()

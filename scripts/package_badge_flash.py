#!/usr/bin/env python3
"""Package the exact qualified PR44 artifact. Offline only: never opens a device."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
HEAD = 'f1b207ac6553609e4a313d1ed7f4ca09aeebd89c'
TREE = 'd2a8e915c28e78d412498dc89a8341f27b448be5'
ARCHIVE_SHA = '526df53016f054169a56f71227f121a8d2e455da68b9051e97dcc099a8999dbb'
IDF = 'fff9895c82d744c7237be8847347bdd1b07c6643'
FLASH = 0x2000000
BOOTFS = 0x310000
SIZE = FLASH - BOOTFS

def sha(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def read(p): return json.loads(Path(p).read_text())
def require(value, message):
    if not value: raise ValueError(message)
def record(p): return {'bytes': Path(p).stat().st_size, 'sha256': sha(p)}
def inventory(path): return {str(p.relative_to(path)): record(p) for p in sorted(path.rglob('*')) if p.is_file()}
def git(path, *args): return subprocess.check_output(['git','-C',str(path),*args],text=True).strip()

def package(archive, idf, out):
    require(not out.exists() or not any(out.iterdir()), 'Output must be empty')
    require(sha(archive) == ARCHIVE_SHA, 'CI archive checksum mismatch')
    require(git(idf,'rev-parse','HEAD') == IDF and not git(idf,'status','--porcelain'), 'Pinned clean ESP-IDF required')
    # Verify submodule source bytes too, including the host SPIFFS reader.
    require(not any(line.startswith(('-', '+', 'U')) for line in git(idf,'submodule','status','--recursive').splitlines()), 'IDF submodules not initialized/pinned')
    out.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='badge-package-') as tmp:
        tmp=Path(tmp)
        with zipfile.ZipFile(archive) as z:
            require(all(not Path(n).is_absolute() and '..' not in Path(n).parts for n in z.namelist()), 'Unsafe archive path')
            z.extractall(tmp)
        native=tmp/'evidence'; home=tmp/'home-evidence'; build=native/'build'; store=home/'target/store'
        require((home/'drivers-source.txt').read_text().strip()==HEAD, 'Wrong qualified Drivers head')
        proof=read(native/'build-proof.json'); target=read(home/'target/build.json'); config=read(build/'config/sdkconfig.json')
        require(proof['idf_revision']==IDF and proof['full_link'], 'Native provenance invalid')
        for name, expected in proof['artifacts'].items(): require(record(build/name)==expected, 'Native artifact mismatch: '+name)
        for group in (proof['sources_sha256'], target['source_sha256']):
            for name,digest in group.items(): require(sha(ROOT/name)==digest, 'Qualified source changed: '+name)
        files=inventory(store)
        require(files==target['store_files'], 'Store inventory mismatch')
        require(sha(store/'default.elf')==sha(home/'target/shared-home/default.elf')==target['home']['sha256'], 'Shared Home bytes changed')
        for mode in ('normal','sanitized'):
            admission=read(home/f'admission-{mode}/admission.json')
            require(admission['real_runtime_graph_admission'] and admission['target_images_checked'] and admission['positive']['elf_count']==5, 'Missing graph admission')
            require(admission['store_files']==files and admission['native_elf_sha256']==sha(build/'riscrte_native.elf'), 'Admission provenance mismatch')
        expected={'SPIFFS_PAGE_SIZE':512,'SPIFFS_OBJ_NAME_LEN':32,'SPIFFS_META_LENGTH':4,'SPIFFS_USE_MAGIC':True,'SPIFFS_USE_MAGIC_LENGTH':True,'ESPTOOLPY_FLASHSIZE':'32MB'}
        require(all(config[k]==v for k,v in expected.items()), 'Incompatible native flash/SPIFFS configuration')
        partitions=(build/'partition_table/partition-table.bin').read_bytes()
        rows=[]; md5_at=None
        for at in range(0,len(partitions),32):
            entry=partitions[at:at+32]
            if entry[:2]==b'\xeb\xeb':
                require(entry[16:]==hashlib.md5(partitions[:at]).digest(), 'Partition MD5 mismatch'); md5_at=at; break
            require(entry[:2]==b'\xaa\x50', 'Partition magic mismatch')
            magic,typ,sub,offset,size,label,flags=struct.unpack('<HBBII16sI',entry)
            rows.append((label.rstrip(b'\0').decode(),typ,sub,offset,size,flags))
        require(md5_at is not None and rows==[('nvs',1,2,0x9000,0x6000,0),('factory',0,0,0x10000,0x300000,0),('bootfs',1,0x82,BOOTFS,SIZE,0)],'Unexpected partition layout')
        tool=idf/'components/spiffs/spiffsgen.py'
        spec=importlib.util.spec_from_file_location('spiffsgen',tool); spiffs=importlib.util.module_from_spec(spec); spec.loader.exec_module(spiffs)
        cfg=spiffs.SpiffsBuildConfig(512,2,4096,2,4,32,2,2,True,True,'little',True,True,False)
        def generate():
            fs=spiffs.SpiffsFS(SIZE,cfg)
            for name in files:
                require(len(('/'+name).encode())<32, 'SPIFFS filename too long')
                fs.create_file('/'+name,str(store/name))
            return fs.to_binary()
        image=generate(); require(image==generate() and len(image)==SIZE,'Nonreproducible SPIFFS image')
        (out/'bootfs.bin').write_bytes(image)
        components=[(0,'bootloader.bin',build/'bootloader/bootloader.bin',0x8000),(0x8000,'partition-table.bin',build/'partition_table/partition-table.bin',0x1000),(0x10000,'riscrte_native.bin',build/'riscrte_native.bin',0x300000),(BOOTFS,'bootfs.bin',out/'bootfs.bin',SIZE)]
        combined=bytearray(b'\xff'*FLASH); regions=[]
        for offset,name,source,capacity in components:
            data=source.read_bytes(); require(len(data)<=capacity,'Partition overflow: '+name)
            if source!=out/name: shutil.copyfile(source,out/name)
            combined[offset:offset+len(data)]=data
            regions.append({'offset':offset,'file':name,**record(out/name)})
        (out/'badge-combined.bin').write_bytes(combined)
        require(combined[0x9000:0xf000]==b'\xff'*0x6000, 'NVS is not blank')
        for region in regions:
            require(hashlib.sha256(combined[region['offset']:region['offset']+region['bytes']]).hexdigest()==region['sha256'],'Combined region mismatch')
        shutil.copytree(store,out/'store')
        shutil.copyfile(build/'config/sdkconfig.json',out/'sdkconfig.json')
        for name,p in [('native-proof.json',native/'build-proof.json'),('store-proof.json',home/'target/build.json'),('admission-normal.json',home/'admission-normal/admission.json'),('admission-sanitized.json',home/'admission-sanitized/admission.json')]: shutil.copyfile(p,out/name)
        receipt={'schema':1,'device':'Waveshare ESP32-S3 Touch AMOLED 1.75C aluminum The-Badge','source_head':HEAD,'source_tree':TREE,'ci_run':40126483360,'ci_artifact':12573657768,'ci_archive_sha256':ARCHIVE_SHA,'idf_revision':IDF,'spiffsgen_sha256':sha(tool),'flash_bytes':FLASH,'psram_bytes':8388608,'panel':[466,466],'spiffs':{'page_size':512,'block_size':4096,'name_length':32,'metadata_length':4,'magic':True,'magic_length':True,'aligned_object_index_tables':False,'file_order':'lexicographic','metadata':'zeroed by pinned generator'},'regions':regions,'combined':record(out/'badge-combined.bin'),'store_files':files,'byte_reproducible_spiffs':True,'native_bytes_preserved':True,'physical_boot_verified':False,'hardware_flash_performed':False,'limits':'Factory candidate, not OTA. Full image overwrites all 32 MiB including NVS and existing user data. No physical qualification, merge, release or publication.'}
        (out/'package.json').write_text(json.dumps(receipt,indent=2)+'\n')
        print(json.dumps({'combined':receipt['combined'],'output':str(out)},indent=2))

def main():
    p=argparse.ArgumentParser(description=__doc__); p.add_argument('--artifact',required=True,type=Path); p.add_argument('--idf',required=True,type=Path); p.add_argument('--output',required=True,type=Path); a=p.parse_args()
    package(a.artifact.resolve(),a.idf.resolve(),a.output.resolve())
if __name__=='__main__': main()
